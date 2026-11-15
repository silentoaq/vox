#pragma once
#include <windows.h>
#include <wrl.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "WebView2.h"
#include "accounts.h"
#include "apiServer.h"
#include "bridge.h"
#include "desktopLyrics.h"
#include "install.h"
#include "karaoke.h"
#include "text.h"
#include "tray.h"
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
  void syncWebView();
  void activate();
  void hideToTray();
  void onTray(TrayCommand command);
  void control(const std::wstring& action);
  void loadSettings();
  void saveSettings();
  void playPending();
  bool runOnUi(std::function<void()> task);
  void forwardApi(const std::string& kind, const std::string& body, ApiServer::Reply reply);
  void dropApiPending(const char* reason);
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
  bool webViewVisible_ = true;
  bool closeToTray_ = true;
  bool trayHinted_ = false;
  bool hasSong_ = false;
  bool playing_ = false;
  unsigned fontSerial_ = 0;
  std::optional<std::wstring> pendingVideo_;
  std::map<double, ApiServer::Reply> apiPending_;
  double nextApiId_ = 0;
  std::wstring apiError_;
  std::unique_ptr<ApiServer> api_;
  std::unique_ptr<Updater> updater_;
  std::unique_ptr<Tray> tray_;
};
