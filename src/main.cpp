#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>

#include <winrt/base.h>

#include "install.h"
#include "mainWindow.h"

namespace {

HWND findRunningWindow() {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (HWND hwnd = FindWindowW(kWindowClass, nullptr)) return hwnd;
    Sleep(100);
  }
  return nullptr;
}

void forwardToRunning(const std::wstring& arg) {
  HWND hwnd = findRunningWindow();
  if (!hwnd) return;
  COPYDATASTRUCT data{};
  if (arg.empty()) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    AllowSetForegroundWindow(pid);
    data.dwData = ULONG_PTR(CopyKind::Activate);
  } else {
    data.dwData = ULONG_PTR(CopyKind::Link);
    data.cbData = DWORD(arg.size() * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(arg.data());
  }
  SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring arg = argc > 1 ? argv[1] : L"";
  LocalFree(argv);

  HANDLE mutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    forwardToRunning(arg);
    return 0;
  }

  SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);
  winrt::init_apartment(winrt::apartment_type::single_threaded);

  MainWindow window(instance);
  if (!arg.empty()) window.openLink(arg);
  window.show(arg.empty() ? show : SW_SHOWNOACTIVATE);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  CloseHandle(mutex);
  return 0;
}
