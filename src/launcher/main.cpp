#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <string>

#include "../files.h"
#include "../install.h"
#include "../text.h"
#include "setup.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

struct Started {
  HANDLE process = nullptr;
  std::wstring error;
};

Started startApp(const std::wstring& root, const std::wstring& version, int show) {
  const std::wstring dir = versionDir(root, version);
  const std::wstring exe = dir + L"\\" APP_NAME L".exe";
  std::wstring commandLine = L"\"" + exe + L"\" " + PathGetArgsW(GetCommandLineW());
  STARTUPINFOW startup{sizeof startup};
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = WORD(show);
  PROCESS_INFORMATION process{};
  AllowSetForegroundWindow(ASFW_ANY);
  if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &startup, &process))
    return {nullptr, tr(Text::CantStart, {exe, std::to_wstring(GetLastError())})};
  CloseHandle(process.hThread);
  return {process.hProcess, {}};
}

bool reject(const std::wstring& root, const std::wstring& version) {
  removeMarker(root, kTryingMarker);
  if (readMarker(root, kCurrentMarker) == version) return false;
  writeMarker(root, kRejectedMarker, version);
  removeTree(versionDir(root, version));
  return true;
}

bool trialFailed(const std::wstring& root, HANDLE process) {
  HANDLE change = FindFirstChangeNotificationW(root.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
  if (change == INVALID_HANDLE_VALUE) return false;
  bool failed = false;
  for (;;) {
    if (!readMarker(root, kTryingMarker)) break;
    const HANDLE handles[] = {process, change};
    if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) {
      failed = readMarker(root, kTryingMarker).has_value();
      break;
    }
    FindNextChangeNotification(change);
  }
  FindCloseChangeNotification(change);
  return failed;
}

int launch(HINSTANCE instance, int show) {
  const std::wstring root = exeDir();
  DeleteFileW((root + L"\\" APP_NAME L".exe.old").c_str());
  auto current = readMarker(root, kCurrentMarker);
  if (!current) {
    showError(instance, tr(Text::CantOpen, {APP_NAME}), tr(Text::InstallIncomplete));
    return 1;
  }
  std::optional<std::wstring> trial;
  if (!appRunning()) {
    if (auto trying = readMarker(root, kTryingMarker)) reject(root, *trying);
    else if (auto pending = readMarker(root, kPendingMarker)) {
      const std::wstring from = root + L"\\" + kPendingMarker;
      const std::wstring to = root + L"\\" + kTryingMarker;
      if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) trial = pending;
    }
  }

  Started started = startApp(root, trial.value_or(*current), show);
  if (trial && (!started.process || trialFailed(root, started.process)) && reject(root, *trial)) {
    if (started.process) CloseHandle(started.process);
    started = startApp(root, *current, show);
  }
  if (!started.process) {
    showError(instance, tr(Text::CantOpen, {APP_NAME}), started.error);
    return 1;
  }
  CloseHandle(started.process);
  return 0;
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (auto payload = findPayload()) return runSetup(instance, *payload);

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const std::wstring command = argc > 1 ? argv[1] : L"";
  const DWORD parent = argc > 2 ? wcstoul(argv[2], nullptr, 10) : 0;
  const std::wstring root = argc > 3 ? argv[3] : L"";
  const bool everything = argc == 5 && std::wstring(argv[4]) == kRemoveEverything;
  LocalFree(argv);

  if (argc == 2 && command == L"--uninstall") return runUninstall(instance);
  if ((argc == 4 || everything) && command == L"--remove") return runRemove(instance, parent, root, everything);
  return launch(instance, show);
}
