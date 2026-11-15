#include "install.h"

#include <shlobj.h>

#include <filesystem>
#include <regex>

#include "files.h"
#include "text.h"

namespace {

constexpr wchar_t kVersionPrefix[] = L"app-";
constexpr DWORD kCloseWaitMs = 10000;

}

std::wstring installRoot() {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS) {
    std::wstring root(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, root.data(), &size) ==
        ERROR_SUCCESS) {
      root.resize(wcslen(root.c_str()));
      return root;
    }
  }
  PWSTR base = nullptr;
  SHGetKnownFolderPath(FOLDERID_UserProgramFiles, KF_FLAG_CREATE, nullptr, &base);
  std::wstring root = std::wstring(base) + L"\\" APP_NAME;
  CoTaskMemFree(base);
  return root;
}

std::wstring removeInstall(const std::wstring& root) {
  if (std::wstring error = removeVersionsExcept(root, {}); !error.empty()) return error;
  for (const wchar_t* name : {kCurrentMarker, kPendingMarker, kTryingMarker, kRejectedMarker, APP_NAME L".exe", APP_NAME L".exe.old"}) {
    const std::wstring path = root + L"\\" + name;
    if (DeleteFileW(path.c_str())) continue;
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) return tr(Text::CantDelete, {path, std::to_wstring(error)});
  }
  RemoveDirectoryW(root.c_str());
  return {};
}

std::wstring versionDir(const std::wstring& root, const std::wstring& version) {
  return root + L"\\" + kVersionPrefix + version;
}

bool isVersionText(const std::wstring& text) {
  static const std::wregex pattern(L"^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$");
  return std::regex_match(text, pattern);
}

std::optional<std::wstring> readMarker(const std::wstring& root, const wchar_t* name) {
  HANDLE file = CreateFileW((root + L"\\" + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return std::nullopt;
  LARGE_INTEGER size{};
  std::string bytes;
  DWORD read = 0;
  bool ok = GetFileSizeEx(file, &size);
  if (ok) {
    bytes.resize(size_t(size.QuadPart));
    ok = ReadFile(file, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size();
  }
  CloseHandle(file);
  if (!ok) return std::nullopt;
  std::wstring text(bytes.begin(), bytes.end());
  if (!isVersionText(text)) return std::nullopt;
  return text;
}

void writeMarker(const std::wstring& root, const wchar_t* name, const std::wstring& version) {
  writeUtf8File(root + L"\\" + name, version);
}

void removeMarker(const std::wstring& root, const wchar_t* name) { DeleteFileW((root + L"\\" + name).c_str()); }

std::wstring removeTree(const std::wstring& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  if (!error) return {};
  return tr(Text::CantDelete, {path, std::to_wstring(error.value())});
}

std::wstring removeVersionsExcept(const std::wstring& root, const std::wstring& keep) {
  const std::wstring kept = std::wstring(kVersionPrefix) + keep;
  std::error_code error;
  for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
    const std::wstring name = entry.path().filename().wstring();
    if (!entry.is_directory() || name.rfind(kVersionPrefix, 0) != 0 || name == kept) continue;
    std::wstring failed = removeTree(entry.path().wstring());
    if (!failed.empty()) return failed;
  }
  if (error) return tr(Text::CantRead, {root, std::to_wstring(error.value())});
  return {};
}

std::wstring recordInstalledVersion(const std::wstring& version) {
  LSTATUS status = RegSetKeyValueW(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", REG_SZ, version.c_str(),
                                   DWORD((version.size() + 1) * sizeof(wchar_t)));
  if (status == ERROR_SUCCESS) return {};
  return tr(Text::CantWriteRegistry, {kUninstallKey, std::to_wstring(status)});
}

std::wstring installLauncher(const std::wstring& root, const std::wstring& version) {
  const std::wstring launcher = root + L"\\" APP_NAME L".exe";
  const std::wstring retired = launcher + L".old";
  DeleteFileW(retired.c_str());
  if (GetFileAttributesW(launcher.c_str()) != INVALID_FILE_ATTRIBUTES &&
      !MoveFileExW(launcher.c_str(), retired.c_str(), MOVEFILE_REPLACE_EXISTING))
    return tr(Text::CantReplace, {launcher, std::to_wstring(GetLastError())});
  const std::wstring fresh = versionDir(root, version) + L"\\launcher\\" APP_NAME L".exe";
  if (!CopyFileW(fresh.c_str(), launcher.c_str(), FALSE))
    return tr(Text::CantCopy, {fresh, std::to_wstring(GetLastError())});
  DeleteFileW(retired.c_str());
  return {};
}

bool appRunning() {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kInstanceMutex);
  if (!mutex) return false;
  CloseHandle(mutex);
  return true;
}

std::wstring closeRunningApp() {
  HWND window = FindWindowW(kWindowClass, nullptr);
  if (!window) return {};
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (!process) return tr(Text::RunningNotFound, {APP_NAME, std::to_wstring(GetLastError())});
  PostMessageW(window, WM_CLOSE, 0, 0);
  const bool exited = WaitForSingleObject(process, kCloseWaitMs) == WAIT_OBJECT_0;
  CloseHandle(process);
  return exited ? std::wstring() : tr(Text::CantClose, {APP_NAME});
}
