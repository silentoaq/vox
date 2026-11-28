#import "karaoke.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#import "common.h"
#import "files.h"
#import "http.h"

extern char** environ;

namespace {

NSString* const kYtDlpUrl = @"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos";
NSString* const kModelFile = @"demucs-hdemucs_mmi-10s.onnx";
constexpr int kSeparateThreads = 1;
constexpr int kUrgentThreads = 4;

bool exists(NSString* path) { return [NSFileManager.defaultManager fileExistsAtPath:path]; }

NSString* toolsDir() {
  NSString* dir = appDataPath(@"tools");
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  return dir;
}

NSString* modelPath() { return [[resourcesDir() stringByAppendingPathComponent:@"models"] stringByAppendingPathComponent:kModelFile]; }

NSString* separatorPath() {
  return [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent
    stringByAppendingPathComponent:[ns(std::wstring(APP_ID)) stringByAppendingString:@"-separate"]];
}

struct Child {
  pid_t pid = 0;
  int output = -1;
};

Child spawn(NSArray<NSString*>* argv, int input, int data) {
  int pipeEnds[2];
  if (pipe(pipeEnds) != 0) return {};
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (input >= 0) posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
  else posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_adddup2(&actions, data >= 0 ? data : pipeEnds[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDERR_FILENO);
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_set_qos_class_np(&attributes, QOS_CLASS_UTILITY);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
  std::vector<std::string> storage;
  for (NSString* arg in argv) storage.emplace_back(arg.UTF8String);
  std::vector<char*> raw;
  for (auto& arg : storage) raw.push_back(arg.data());
  raw.push_back(nullptr);
  pid_t pid = 0;
  const int failed = posix_spawn(&pid, raw[0], &actions, &attributes, raw.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  close(pipeEnds[1]);
  if (failed) {
    close(pipeEnds[0]);
    return {};
  }
  return {pid, pipeEnds[0]};
}

template <typename OnLine>
int drain(const Child& child, OnLine onLine, std::string& tail) {
  std::string pending;
  char buffer[4096];
  for (;;) {
    const ssize_t read = ::read(child.output, buffer, sizeof buffer);
    if (read <= 0) break;
    pending.append(buffer, size_t(read));
    for (size_t end; (end = pending.find('\n')) != std::string::npos;) {
      std::string line = pending.substr(0, end);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      pending.erase(0, end + 1);
      tail = line;
      onLine(line);
    }
  }
  close(child.output);
  int status = 0;
  while (waitpid(child.pid, &status, 0) < 0 && errno == EINTR) {
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

}

Karaoke::Karaoke(Report report) : report_(std::move(report)) {
  for (NSString* name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:appDataDir() error:nil])
    if ([name hasPrefix:@"yt-cookies-"]) [NSFileManager.defaultManager removeItemAtPath:appDataPath(name) error:nil];
}

Karaoke::~Karaoke() { cancel(); }

void Karaoke::cancel() {
  std::lock_guard lock(state_->lock);
  state_->cancelled = true;
  if (state_->process) kill(state_->process, SIGTERM);
  if (state_->download) kill(state_->download, SIGTERM);
}

NSString* Karaoke::stemsDir() {
  NSString* dir = appDataPath(@"stems");
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  return dir;
}

void Karaoke::prepare(NSString* id, double fromSeconds, bool urgent, NSString* cookies) {
  if (running_ && [running_->id isEqualToString:id]) return;
  std::erase_if(queue_, [&](const Job& job) { return [job.id isEqualToString:id] || (!urgent && !job.urgent); });
  if (urgent) queue_.push_front({id, fromSeconds, cookies, true});
  else queue_.push_back({id, fromSeconds, cookies, false});
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
    std::lock_guard lock(state_->lock);
    state_->cancelled = false;
  }
  Job job = *running_;
  std::thread([this, job] {
    @autoreleasepool {
      run(job);
    }
  }).detach();
}

void Karaoke::post(KaraokeStatus status) {
  onMain(^{
    finish(status);
  });
}

void Karaoke::finish(const KaraokeStatus& status) {
  report_(status);
  const bool terminal = [status.state isEqualToString:@"complete"] || [status.state isEqualToString:@"error"] ||
                        [status.state isEqualToString:@"stopped"];
  if (terminal && running_ && [running_->id isEqualToString:status.id]) {
    running_.reset();
    startNext();
  }
}

bool Karaoke::ensureYtDlp(NSString* path, NSString* id) {
  if (exists(path)) return true;
  post({id, @"installing"});
  NSString* partial = [path stringByAppendingString:@".part"];
  NSString* failure = httpDownloadNow(kYtDlpUrl, partial);
  if (!failure && chmod(partial.fileSystemRepresentation, 0755) != 0) failure = @(strerror(errno));
  if (!failure) {
    NSError* error = nil;
    if (![NSFileManager.defaultManager moveItemAtPath:partial toPath:path error:&error]) failure = error.localizedDescription;
  }
  if (failure) {
    [NSFileManager.defaultManager removeItemAtPath:partial error:nil];
    post({id, @"error", -1, 0, 0, 0, [@"安裝 yt-dlp 失敗：" stringByAppendingString:failure]});
    return false;
  }
  return true;
}

void Karaoke::run(Job job) {
  NSString* dir = [stemsDir() stringByAppendingPathComponent:job.id];
  [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
  if (exists([dir stringByAppendingPathComponent:@"segments.json"])) {
    post({job.id, @"complete"});
    return;
  }
  if (!exists(modelPath())) {
    post({job.id, @"error", -1, 0, 0, 0, @"伴唱模型還沒安裝"});
    return;
  }
  NSString* ytDlp = [toolsDir() stringByAppendingPathComponent:@"yt-dlp"];
  if (!ensureYtDlp(ytDlp, job.id)) return;

  NSString* source = [dir stringByAppendingPathComponent:@"source.m4a"];
  const bool cached = exists(source);
  int input = -1;
  Child download;
  std::thread downloadDrain;
  std::string downloadTail;
  int downloadCode = 0;
  std::atomic<bool> separating = false;
  auto state = state_;
  if (cached) {
    input = open(source.fileSystemRepresentation, O_RDONLY);
  } else {
    post({job.id, @"downloading"});
    NSString* cookieFile = appDataPath([NSString stringWithFormat:@"yt-cookies-%@.txt", job.id]);
    NSMutableArray* argv =
      [@[ ytDlp, @"-f", @"bestaudio[ext=m4a]", @"--no-playlist", @"--newline", @"-o", @"-" ] mutableCopy];
    if (job.cookies.length) {
      writeTextFile(cookieFile, job.cookies);
      [argv addObjectsFromArray:@[ @"--cookies", cookieFile ]];
    }
    [argv addObject:[@"https://www.youtube.com/watch?v=" stringByAppendingString:job.id]];
    int data[2];
    pipe(data);
    download = spawn(argv, -1, data[1]);
    close(data[1]);
    input = data[0];
    if (!download.pid) {
      close(input);
      if (job.cookies.length) [NSFileManager.defaultManager removeItemAtPath:cookieFile error:nil];
      post({job.id, @"error", -1, 0, 0, 0, @"無法啟動 yt-dlp"});
      return;
    }
    {
      std::lock_guard lock(state->lock);
      state->download = download.pid;
    }
    downloadDrain = std::thread([&, cookieFile] {
      downloadCode = drain(
        download,
        [&](const std::string& line) {
          double percent = 0;
          if (!separating && line.rfind("[download]", 0) == 0 && sscanf(line.c_str(), "[download] %lf%%", &percent) == 1) {
            KaraokeStatus status{job.id, @"downloading"};
            status.progress = percent / 100.0;
            post(status);
          }
        },
        downloadTail);
      if (job.cookies.length) [NSFileManager.defaultManager removeItemAtPath:cookieFile error:nil];
    });
  }

  NSArray* argv = @[
    separatorPath(), modelPath(), cached ? @"-" : source, dir, [@(kSeparateThreads) stringValue], [NSString stringWithFormat:@"%.3f", job.from],
    [@(job.urgent ? kUrgentThreads : kSeparateThreads) stringValue]
  ];
  Child child = spawn(argv, input, -1);
  close(input);
  std::string tail;
  bool done = false;
  int code = 1;
  if (child.pid) {
    {
      std::lock_guard lock(state->lock);
      state->process = child.pid;
    }
    code = drain(
      child,
      [&](const std::string& line) {
        int a = 0, b = 0, c = 0;
        if (sscanf(line.c_str(), "total %d %d %d", &a, &b, &c) == 3) {
          separating = true;
          post({job.id, @"separating", -1, a, b, c});
        } else if (sscanf(line.c_str(), "ready %d", &a) == 1) {
          post({job.id, @"segment", a});
        } else if (line == "done") {
          done = true;
        }
      },
      tail);
  } else if (download.pid) {
    kill(download.pid, SIGTERM);
  }
  if (downloadDrain.joinable()) downloadDrain.join();
  bool cancelled = false;
  {
    std::lock_guard lock(state->lock);
    state->process = 0;
    state->download = 0;
    cancelled = state->cancelled;
  }
  if (done && code == 0) post({job.id, @"complete"});
  else if (cancelled) post({job.id, @"stopped", -1, 0, 0, 0, @"讓給優先的歌"});
  else if (!child.pid) post({job.id, @"error", -1, 0, 0, 0, @"無法啟動分離程式"});
  else if (download.pid && downloadCode != 0) post({job.id, @"error", -1, 0, 0, 0, [@"下載音訊失敗：" stringByAppendingString:ns(downloadTail)]});
  else post({job.id, @"stopped", -1, 0, 0, 0, ns(tail)});
}
