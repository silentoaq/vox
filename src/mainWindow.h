#pragma once
#include <windows.h>
#include <wrl.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WebView2.h"
#include "accounts.h"
#include "bridge.h"
#include "desktopLyrics.h"
#include "install.h"
#include "karaoke.h"
#include "link.h"
#include "nowPlaying.h"
#include "text.h"
#include "updater.h"

inline constexpr UINT WM_APP_RUN = WM_APP + 30;

enum class CopyKind : ULONG_PTR { Link = 1, Activate = 2 };

class MainWindow {
public:
  explicit MainWindow(HINSTANCE instance);
  void show(int command);
  void openLink(const std::wstring& link);

private:
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);
  void registerMethods();
  void createWebView();
  void fitWebView();
  void syncShown();
  void activate();
  void control(const std::wstring& action);
  void sendPendingLinks();
  bool runOnUi(std::function<void()> task);
  Bridge::JsonObject desktopLyricsState() const;
  HRESULT fatal(Text step, HRESULT hr);
  Bridge::JsonObject languageState() const;
  Bridge::JsonObject windowState() const;

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  std::unique_ptr<Accounts> accounts_;
  std::unique_ptr<Bridge> bridge_;
  std::unique_ptr<DesktopLyrics> lyrics_;
  std::unique_ptr<Karaoke> karaoke_;
  bool pageReady_ = false;
  bool windowShown_ = true;
  unsigned fontSerial_ = 0;
  std::vector<LinkRequest> pendingLinks_;
  std::unique_ptr<NowPlaying> nowPlaying_;
  std::unique_ptr<Updater> updater_;
};
