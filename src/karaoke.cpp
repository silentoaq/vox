#include "karaoke.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "files.h"
#include "http.h"

namespace {

constexpr wchar_t kYtDlpUrl[] = L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_win.zip";
constexpr wchar_t kModelFile[] = L"demucs-hdemucs_mmi-10s.onnx";
constexpr int kSeparateThreads = 1;
constexpr int kUrgentThreads = 4;
constexpr DWORD kAudioPipeBytes = 1 << 20;

std::mutex processLock;

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring toolsDir() {
  std::wstring dir = appDataDir() + L"\\tools";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring modelPath() { return exeDir() + L"\\models\\" + kModelFile; }

std::wstring wide(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
  return out;
}

struct Child {
  HANDLE process = nullptr;
  HANDLE output = nullptr;
};

Child spawn(std::wstring commandLine, DWORD priority, HANDLE input = nullptr, HANDLE data = nullptr) {
  SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  CreatePipe(&read, &write, &inherit, 0);
  SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input;
  startup.hStdOutput = data ? data : write;
  startup.hStdError = write;
  PROCESS_INFORMATION info{};
  BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | priority, nullptr,
                           nullptr, &startup, &info);
  CloseHandle(write);
  if (!ok) {
    CloseHandle(read);
    return {};
  }
  CloseHandle(info.hThread);
  return {info.hProcess, read};
}

template <typename OnLine>
DWORD drain(const Child& child, OnLine onLine, std::string& tail) {
  std::string pending;
  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(child.output, buffer, sizeof buffer, &read, nullptr) && read > 0) {
    pending.append(buffer, read);
    for (size_t end; (end = pending.find('\n')) != std::string::npos;) {
      std::string line = pending.substr(0, end);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      pending.erase(0, end + 1);
      tail = line;
      onLine(line);
    }
  }
  WaitForSingleObject(child.process, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CloseHandle(child.output);
  return code;
}

}

Karaoke::Karaoke(HWND hwnd, Report report) : hwnd_(hwnd), report_(std::move(report)) {
  for (const auto& entry : std::filesystem::directory_iterator(appDataDir()))
    if (entry.path().filename().wstring().starts_with(L"yt-cookies-")) std::filesystem::remove(entry.path());
}

Karaoke::~Karaoke() { cancel(); }

void Karaoke::cancel() {
  std::lock_guard lock(processLock);
  cancelled_ = true;
  if (process_) TerminateProcess(process_, 1);
  if (download_) TerminateProcess(download_, 1);
}

std::wstring Karaoke::stemsDir() {
  std::wstring dir = appDataDir() + L"\\stems";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

void Karaoke::prepare(const std::wstring& id, double fromSeconds, bool urgent, std::string cookies) {
  if (running_ && running_->id == id) return;
  std::erase_if(queue_, [&](const Job& job) { return job.id == id || (!urgent && !job.urgent); });
  if (urgent) queue_.push_front({id, fromSeconds, std::move(cookies), true});
  else queue_.push_back({id, fromSeconds, std::move(cookies), false});
  if (urgent && running_) {
    queue_.push_back({running_->id, 0, running_->cookies, false});
    cancel();
    return;
  }
  if (!running_) startNext();
}

void Karaoke::startNext() {
  if (queue_.empty()) return;
  running_ = queue_.front();
  queue_.pop_front();
  {
    std::lock_guard lock(processLock);
    cancelled_ = false;
  }
  std::thread([this, job = *running_] { run(job); }).detach();
}

void Karaoke::post(KaraokeStatus status) {
  auto copy = std::make_unique<KaraokeStatus>(std::move(status));
  if (PostMessageW(hwnd_, WM_APP_KARAOKE, 0, reinterpret_cast<LPARAM>(copy.get()))) copy.release();
}

bool Karaoke::ensureYtDlp(const std::wstring& dir, const std::wstring& id) {
  if (exists(dir + L"\\yt-dlp.exe")) return true;
  post({id, L"installing"});
  HttpResponse response = httpFetch({L"GET", kYtDlpUrl, {}, {}});
  if (!response.error.empty() || response.status != 200) {
    post({id, L"error", -1, 0, 0, 0, L"下載 yt-dlp 失敗：" + (response.error.empty() ? std::to_wstring(response.status) : response.error)});
    return false;
  }
  const std::wstring partial = dir + L".part";
  const std::wstring archive = dir + L".zip";
  std::filesystem::remove_all(partial);
  std::filesystem::create_directories(partial);
  writeBytesFile(archive, response.body);
  std::wstring error = unzip(archive, partial);
  DeleteFileW(archive.c_str());
  if (error.empty() && !MoveFileExW(partial.c_str(), dir.c_str(), 0)) error = L"yt-dlp 存不起來，錯誤碼 " + std::to_wstring(GetLastError());
  if (!error.empty()) {
    std::filesystem::remove_all(partial);
    post({id, L"error", -1, 0, 0, 0, L"安裝 yt-dlp 失敗：" + error});
    return false;
  }
  return true;
}

void Karaoke::run(Job job) {
  const std::wstring dir = stemsDir() + L"\\" + job.id;
  std::filesystem::create_directories(dir);
  if (exists(dir + L"\\segments.json")) {
    post({job.id, L"complete"});
    return;
  }
  if (!exists(modelPath())) {
    post({job.id, L"error", -1, 0, 0, 0, L"伴唱模型還沒安裝"});
    return;
  }
  const std::wstring ytDlpDir = toolsDir() + L"\\yt-dlp";
  if (!ensureYtDlp(ytDlpDir, job.id)) return;
  const std::wstring ytDlp = ytDlpDir + L"\\yt-dlp.exe";

  const std::wstring source = dir + L"\\source.m4a";
  const bool cached = exists(source);
  SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
  HANDLE input = nullptr;
  Child download;
  std::thread downloadDrain;
  std::string downloadTail;
  DWORD downloadCode = 0;
  std::atomic<bool> separating = false;
  if (cached) {
    input = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, &inherit, OPEN_EXISTING, 0, nullptr);
  } else {
    post({job.id, L"downloading"});
    const std::wstring cookieFile = appDataDir() + L"\\yt-cookies-" + job.id + L".txt";
    std::wstring cookieArg;
    if (!job.cookies.empty()) {
      writeBytesFile(cookieFile, job.cookies);
      cookieArg = L" --cookies \"" + cookieFile + L"\"";
    }
    HANDLE dataWrite = nullptr;
    CreatePipe(&input, &dataWrite, &inherit, kAudioPipeBytes);
    SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
    download = spawn(L"\"" + ytDlp + L"\" -f \"bestaudio[ext=m4a]\" --no-playlist --newline" + cookieArg +
                       L" -o - \"https://www.youtube.com/watch?v=" + job.id + L"\"",
                     BELOW_NORMAL_PRIORITY_CLASS, nullptr, dataWrite);
    CloseHandle(dataWrite);
    SetHandleInformation(input, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (!download.process) {
      CloseHandle(input);
      if (!job.cookies.empty()) DeleteFileW(cookieFile.c_str());
      post({job.id, L"error", -1, 0, 0, 0, L"無法啟動 yt-dlp"});
      return;
    }
    {
      std::lock_guard lock(processLock);
      download_ = download.process;
    }
    downloadDrain = std::thread([&, cookieFile] {
      downloadCode = drain(
        download,
        [&](const std::string& line) {
          double percent = 0;
          if (!separating && line.rfind("[download]", 0) == 0 && sscanf_s(line.c_str(), "[download] %lf%%", &percent) == 1) {
            KaraokeStatus status{job.id, L"downloading"};
            status.progress = percent / 100.0;
            post(status);
          }
        },
        downloadTail);
      if (!job.cookies.empty()) DeleteFileW(cookieFile.c_str());
    });
  }

  wchar_t from[32];
  swprintf_s(from, L"%.3f", job.from);
  Child child = spawn(L"\"" + exeDir() + L"\\" APP_ID L"-separate.exe\" \"" + modelPath() + L"\" \"" +
                        (cached ? std::wstring(L"-") : source) + L"\" \"" + dir + L"\" " + std::to_wstring(kSeparateThreads) +
                        L" " + from + L" " + std::to_wstring(job.urgent ? kUrgentThreads : kSeparateThreads),
                      BELOW_NORMAL_PRIORITY_CLASS, input);
  CloseHandle(input);
  std::string tail;
  bool done = false;
  DWORD code = 1;
  if (child.process) {
    {
      std::lock_guard lock(processLock);
      process_ = child.process;
    }
    code = drain(
      child,
      [&](const std::string& line) {
        int a = 0, b = 0, c = 0;
        if (sscanf_s(line.c_str(), "total %d %d %d", &a, &b, &c) == 3) {
          separating = true;
          post({job.id, L"separating", -1, a, b, c});
        } else if (sscanf_s(line.c_str(), "ready %d", &a) == 1) {
          post({job.id, L"segment", a});
        } else if (line == "done") {
          done = true;
        }
      },
      tail);
    CloseHandle(child.process);
  } else if (download.process) {
    TerminateProcess(download.process, 1);
  }
  if (downloadDrain.joinable()) downloadDrain.join();
  {
    std::lock_guard lock(processLock);
    process_ = nullptr;
    download_ = nullptr;
  }
  if (download.process) CloseHandle(download.process);
  bool cancelled = false;
  {
    std::lock_guard lock(processLock);
    cancelled = cancelled_;
  }
  if (done && code == 0) post({job.id, L"complete"});
  else if (cancelled) post({job.id, L"stopped", -1, 0, 0, 0, L"讓給優先的歌"});
  else if (!child.process) post({job.id, L"error", -1, 0, 0, 0, L"無法啟動分離程式"});
  else if (download.process && downloadCode != 0) post({job.id, L"error", -1, 0, 0, 0, L"下載音訊失敗：" + wide(downloadTail)});
  else post({job.id, L"stopped", -1, 0, 0, 0, wide(tail)});
}

void Karaoke::finish(LPARAM lparam) {
  std::unique_ptr<KaraokeStatus> status(reinterpret_cast<KaraokeStatus*>(lparam));
  report_(*status);
  const bool terminal = status->state == L"complete" || status->state == L"error" || status->state == L"stopped";
  if (terminal && running_ && running_->id == status->id) {
    running_.reset();
    startNext();
  }
}
