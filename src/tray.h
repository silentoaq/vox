#pragma once
#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

enum class TrayCommand : UINT { Open = 1, Toggle, Skip, Lyrics, LockLyrics, Quit };

struct TrayState {
  bool hasSong;
  bool playing;
  bool lyricsVisible;
  bool lyricsLocked;
};

class Tray {
public:
  using State = std::function<TrayState()>;
  using Command = std::function<void(TrayCommand)>;

  Tray(HINSTANCE instance, State state, Command command);
  ~Tray();

  bool shown() const { return shown_; }
  void setTip(const std::wstring& tip);
  void notify(const std::wstring& title, const std::wstring& body);

private:
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);
  void add();
  void showMenu();

  HWND hwnd_ = nullptr;
  UINT taskbarCreated_ = 0;
  NOTIFYICONDATAW data_{};
  bool shown_ = false;
  State state_;
  Command command_;
};
