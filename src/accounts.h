#pragma once
#include <windows.h>
#include <wrl.h>

#include <functional>
#include <string>
#include <vector>

#include "WebView2.h"
#include "http.h"

struct Site;

struct BrowserCookie {
  Microsoft::WRL::ComPtr<ICoreWebView2Cookie> handle;
  std::wstring name;
  std::wstring value;
  std::wstring domain;
  std::wstring path;
  bool secure = false;
  bool httpOnly = false;
  bool session = false;
  double expires = 0;
};

class Accounts {
public:
  using Changed = std::function<void(const std::wstring& site)>;
  using Jar = std::vector<BrowserCookie>;

  Accounts(HINSTANCE instance, HWND owner, Changed changed);
  ~Accounts();

  void attach(ICoreWebView2Environment* environment, ICoreWebView2CookieManager* cookies);
  void signedIn(const std::wstring& site, std::function<void(bool)> done);
  void login(const std::wstring& site);
  void logout(const std::wstring& site, std::function<void()> done);
  void decorate(HttpRequest request, std::function<void(HttpRequest)> next);
  void netscapeCookies(const std::wstring& site, std::function<void(std::string)> done);

private:
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  void cookiesOf(const std::wstring& url, std::function<void(Jar)> done);
  void sessionOf(const Site& site, std::function<void(std::wstring)> done);
  void openLoginWindow();
  void poll();

  HINSTANCE instance_;
  HWND owner_;
  Changed changed_;
  Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
  Microsoft::WRL::ComPtr<ICoreWebView2CookieManager> cookies_;
  HWND loginWindow_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> loginController_;
  const Site* loginSite_ = nullptr;
  std::wstring staleSession_;
};
