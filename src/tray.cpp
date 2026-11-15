#include "tray.h"

#include <algorithm>
#include <utility>

#include "appIcon.h"
#include "text.h"

namespace {

constexpr wchar_t kClass[] = APP_ID L".tray";
constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT kIconId = 1;

template <size_t N>
void copyText(wchar_t (&target)[N], const std::wstring& text) {
  size_t length = std::min(text.size(), N - 1);
  if (length && IS_HIGH_SURROGATE(text[length - 1])) --length;
  text.copy(target, length);
  target[length] = 0;
}

}

Tray::Tray(HINSTANCE instance, State state, Command command) : state_(std::move(state)), command_(std::move(command)) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  CreateWindowExW(WS_EX_TOOLWINDOW, kClass, APP_NAME, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");

  data_.cbSize = sizeof data_;
  data_.hWnd = hwnd_;
  data_.uID = kIconId;
  data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data_.uCallbackMessage = WM_APP_TRAY;
  data_.hIcon = appIcon(instance, SM_CXSMICON);
  data_.hBalloonIcon = appIcon(instance, SM_CXICON);
  copyText(data_.szTip, APP_NAME);
  add();
}

Tray::~Tray() {
  if (shown_) Shell_NotifyIconW(NIM_DELETE, &data_);
  DestroyWindow(hwnd_);
}

void Tray::add() { shown_ = Shell_NotifyIconW(NIM_ADD, &data_) || Shell_NotifyIconW(NIM_MODIFY, &data_); }

void Tray::setTip(const std::wstring& tip) {
  copyText(data_.szTip, tip);
  if (shown_) Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void Tray::notify(const std::wstring& title, const std::wstring& body) {
  NOTIFYICONDATAW info = data_;
  info.uFlags = NIF_INFO;
  info.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON | NIIF_NOSOUND;
  copyText(info.szInfoTitle, title);
  copyText(info.szInfo, body);
  Shell_NotifyIconW(NIM_MODIFY, &info);
}

void Tray::showMenu() {
  const TrayState state = state_();
  HMENU menu = CreatePopupMenu();
  auto item = [menu](TrayCommand id, const std::wstring& label, UINT flags) {
    AppendMenuW(menu, MF_STRING | flags, UINT_PTR(id), label.c_str());
  };
  const UINT needsSong = state.hasSong ? 0 : MF_GRAYED;
  item(TrayCommand::Open, tr(Text::TrayOpen, {APP_NAME}), 0);
  SetMenuDefaultItem(menu, UINT(TrayCommand::Open), FALSE);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  item(TrayCommand::Toggle, tr(state.playing ? Text::LyricsPause : Text::LyricsPlay), needsSong);
  item(TrayCommand::Skip, tr(Text::LyricsSkip), needsSong);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  item(TrayCommand::Lyrics, tr(Text::TrayLyrics), state.lyricsVisible ? MF_CHECKED : 0);
  item(TrayCommand::LockLyrics, tr(Text::TrayLockLyrics),
       (state.lyricsLocked ? MF_CHECKED : 0) | (state.lyricsVisible ? 0 : MF_GRAYED));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  item(TrayCommand::Quit, tr(Text::TrayQuit, {APP_NAME}), 0);

  POINT point;
  GetCursorPos(&point);
  SetForegroundWindow(hwnd_);
  const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (choice) command_(TrayCommand(choice));
}

LRESULT CALLBACK Tray::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self = static_cast<Tray*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<Tray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->handle(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Tray::handle(UINT message, WPARAM wparam, LPARAM lparam) {
  if (taskbarCreated_ && message == taskbarCreated_) {
    add();
    return 0;
  }
  if (message == WM_APP_TRAY) {
    switch (LOWORD(lparam)) {
      case WM_LBUTTONUP:
      case NIN_BALLOONUSERCLICK:
        command_(TrayCommand::Open);
        break;
      case WM_RBUTTONUP:
        showMenu();
        break;
    }
    return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}
