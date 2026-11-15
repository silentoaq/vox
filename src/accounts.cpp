#include "accounts.h"

#include "appIcon.h"
#include "lockdown.h"
#include "text.h"

#include <bcrypt.h>
#include <dwmapi.h>

#include <ctime>

#include <winrt/base.h>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct Site {
  const wchar_t* id;
  const wchar_t* name;
  const wchar_t* loginUrl;
  const wchar_t* home;
  std::vector<std::wstring> sessionCookies;
  std::vector<std::wstring> domains;
  std::vector<std::wstring> brands;
  bool signsRequests;
};

namespace {

constexpr wchar_t kLoginClass[] = APP_ID L".login";
constexpr UINT_PTR kPollTimer = 1;
constexpr UINT kPollMs = 1000;
constexpr int kLoginWidth = 520;
constexpr int kLoginHeight = 760;

const Site kSites[] = {
  {L"youtube", L"YouTube",
   L"https://accounts.google.com/ServiceLogin?service=youtube&continue=https%3A%2F%2Fmusic.youtube.com%2F",
   L"https://music.youtube.com", {L"SAPISID", L"__Secure-3PAPISID"}, {L"youtube.com"}, {L"google"}, true},
  {L"spotify", L"Spotify", L"https://accounts.spotify.com/login?continue=https%3A%2F%2Fopen.spotify.com%2F",
   L"https://open.spotify.com", {L"sp_dc"}, {L"spotify.com"}, {}, false},
};

const Site& siteOf(const std::wstring& id) {
  for (const Site& site : kSites)
    if (id == site.id) return site;
  throw winrt::hresult_invalid_argument(L"沒有這種帳號：" + id);
}

const Site* siteOfHost(const std::wstring& host) {
  for (const Site& site : kSites)
    if (hostOf(site.home) == host) return &site;
  return nullptr;
}

bool belongs(const Site& site, std::wstring domain) {
  if (domain.starts_with(L".")) domain.erase(0, 1);
  for (const auto& suffix : site.domains)
    if (domain == suffix || domain.ends_with(L"." + suffix)) return true;
  for (const auto& brand : site.brands)
    if (domain.starts_with(brand + L".") || domain.find(L"." + brand + L".") != std::wstring::npos) return true;
  return false;
}

std::wstring sessionValue(const Site& site, const Accounts::Jar& jar) {
  for (const auto& name : site.sessionCookies)
    for (const auto& cookie : jar)
      if (cookie.name == name && !cookie.value.empty()) return cookie.value;
  return {};
}

std::string utf8(const std::wstring& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring sha1Hex(const std::string& data) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) throw winrt::hresult_error(HRESULT_FROM_NT(status), L"開啟 SHA1 失敗");
  UCHAR digest[20];
  status = BCryptHash(algorithm, nullptr, 0, PUCHAR(data.data()), ULONG(data.size()), digest, sizeof digest);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!BCRYPT_SUCCESS(status)) throw winrt::hresult_error(HRESULT_FROM_NT(status), L"計算 SHA1 失敗");
  constexpr wchar_t kHex[] = L"0123456789abcdef";
  std::wstring out;
  for (UCHAR byte : digest) {
    out += kHex[byte >> 4];
    out += kHex[byte & 15];
  }
  return out;
}

template <typename Getter>
std::wstring textOf(ICoreWebView2Cookie* cookie, Getter getter) {
  LPWSTR value = nullptr;
  (cookie->*getter)(&value);
  std::wstring out = value ? value : L"";
  CoTaskMemFree(value);
  return out;
}

template <typename Getter>
bool flagOf(ICoreWebView2Cookie* cookie, Getter getter) {
  BOOL value = FALSE;
  (cookie->*getter)(&value);
  return value;
}

}

Accounts::Accounts(HINSTANCE instance, HWND owner, Changed changed)
    : instance_(instance), owner_(owner), changed_(std::move(changed)) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = kLoginClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = appIcon(instance, SM_CXICON);
  wc.hIconSm = appIcon(instance, SM_CXSMICON);
  wc.hbrBackground = HBRUSH(GetStockObject(BLACK_BRUSH));
  RegisterClassExW(&wc);
}

Accounts::~Accounts() {
  if (loginWindow_) DestroyWindow(loginWindow_);
}

void Accounts::attach(ICoreWebView2Environment* environment, ICoreWebView2CookieManager* cookies) {
  environment_ = environment;
  cookies_ = cookies;
}

void Accounts::cookiesOf(const std::wstring& url, std::function<void(Jar)> done) {
  winrt::check_hresult(cookies_->GetCookies(
    url.c_str(), Callback<ICoreWebView2GetCookiesCompletedHandler>(
                   [done](HRESULT result, ICoreWebView2CookieList* list) -> HRESULT {
                     Jar jar;
                     if (FAILED(result)) {
                       OutputDebugStringW((L"讀取 cookie 失敗：" + std::to_wstring(result) + L"\n").c_str());
                       done(std::move(jar));
                       return S_OK;
                     }
                     UINT count = 0;
                     list->get_Count(&count);
                     for (UINT i = 0; i < count; ++i) {
                       BrowserCookie cookie;
                       list->GetValueAtIndex(i, &cookie.handle);
                       ICoreWebView2Cookie* raw = cookie.handle.Get();
                       cookie.name = textOf(raw, &ICoreWebView2Cookie::get_Name);
                       cookie.value = textOf(raw, &ICoreWebView2Cookie::get_Value);
                       cookie.domain = textOf(raw, &ICoreWebView2Cookie::get_Domain);
                       cookie.path = textOf(raw, &ICoreWebView2Cookie::get_Path);
                       cookie.secure = flagOf(raw, &ICoreWebView2Cookie::get_IsSecure);
                       cookie.httpOnly = flagOf(raw, &ICoreWebView2Cookie::get_IsHttpOnly);
                       cookie.session = flagOf(raw, &ICoreWebView2Cookie::get_IsSession);
                       raw->get_Expires(&cookie.expires);
                       jar.push_back(std::move(cookie));
                     }
                     done(std::move(jar));
                     return S_OK;
                   })
                   .Get()));
}

void Accounts::sessionOf(const Site& site, std::function<void(std::wstring)> done) {
  cookiesOf(site.home, [&site, done](Jar jar) { done(sessionValue(site, jar)); });
}

void Accounts::signedIn(const std::wstring& id, std::function<void(bool)> done) {
  sessionOf(siteOf(id), [done](std::wstring value) { done(!value.empty()); });
}

void Accounts::login(const std::wstring& id) {
  const Site& site = siteOf(id);
  if (loginSite_) {
    if (loginWindow_) SetForegroundWindow(loginWindow_);
    return;
  }
  loginSite_ = &site;
  sessionOf(site, [this](std::wstring stale) {
    staleSession_ = std::move(stale);
    openLoginWindow();
  });
}

void Accounts::openLoginWindow() {
  UINT dpi = GetDpiForWindow(owner_);
  int width = MulDiv(kLoginWidth, dpi, 96);
  int height = MulDiv(kLoginHeight, dpi, 96);
  RECT owner;
  GetWindowRect(owner_, &owner);
  loginWindow_ = CreateWindowExW(0, kLoginClass, tr(Text::LoginTitle, {loginSite_->name}).c_str(), WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 owner.left + (owner.right - owner.left - width) / 2,
                                 owner.top + (owner.bottom - owner.top - height) / 2, width, height, owner_, nullptr,
                                 instance_, this);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(loginWindow_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
  HWND window = loginWindow_;
  std::wstring url = loginSite_->loginUrl;
  winrt::check_hresult(environment_->CreateCoreWebView2Controller(
    window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
              [this, window, url](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                if (window != loginWindow_) {
                  if (SUCCEEDED(result)) controller->Close();
                  return S_OK;
                }
                if (FAILED(result)) {
                  MessageBoxW(window, tr(Text::LoginWindowFailed, {hexText(result)}).c_str(), APP_NAME, MB_ICONERROR);
                  DestroyWindow(window);
                  return S_OK;
                }
                loginController_ = controller;
                RECT bounds;
                GetClientRect(window, &bounds);
                loginController_->put_Bounds(bounds);
                ComPtr<ICoreWebView2> webview;
                loginController_->get_CoreWebView2(&webview);
                lockDown(webview.Get());
                webview->Navigate(url.c_str());
                SetTimer(window, kPollTimer, kPollMs, nullptr);
                return S_OK;
              })
              .Get()));
}

void Accounts::poll() {
  if (!loginController_) return;
  ComPtr<ICoreWebView2> webview;
  loginController_->get_CoreWebView2(&webview);
  LPWSTR source = nullptr;
  webview->get_Source(&source);
  bool home = std::wstring_view(source).starts_with(loginSite_->home);
  CoTaskMemFree(source);
  if (!home) return;
  HWND window = loginWindow_;
  sessionOf(*loginSite_, [this, window](std::wstring value) {
    if (window == loginWindow_ && !value.empty() && value != staleSession_) DestroyWindow(window);
  });
}

void Accounts::logout(const std::wstring& id, std::function<void()> done) {
  const Site& site = siteOf(id);
  cookiesOf(L"", [this, &site, done](Jar jar) {
    for (const auto& cookie : jar)
      if (belongs(site, cookie.domain)) cookies_->DeleteCookie(cookie.handle.Get());
    done();
  });
}

void Accounts::netscapeCookies(const std::wstring& id, std::function<void(std::string)> done) {
  const Site& site = siteOf(id);
  cookiesOf(L"", [&site, done](Jar jar) {
    std::wstring out = L"# Netscape HTTP Cookie File\n";
    for (const auto& cookie : jar) {
      if (!belongs(site, cookie.domain)) continue;
      const long long expires = cookie.session ? 0 : static_cast<long long>(cookie.expires);
      out += (cookie.httpOnly ? L"#HttpOnly_" : L"") + cookie.domain + L"\t" +
             (cookie.domain.starts_with(L".") ? L"TRUE" : L"FALSE") + L"\t" + cookie.path + L"\t" +
             (cookie.secure ? L"TRUE" : L"FALSE") + L"\t" + std::to_wstring(expires) + L"\t" + cookie.name + L"\t" +
             cookie.value + L"\n";
    }
    done(utf8(out));
  });
}

void Accounts::decorate(HttpRequest request, std::function<void(HttpRequest)> next) {
  const Site* site = cookies_ ? siteOfHost(hostOf(request.url)) : nullptr;
  if (!site) {
    next(std::move(request));
    return;
  }
  cookiesOf(site->home, [site, request, next](Jar jar) mutable {
    std::wstring header;
    for (const auto& cookie : jar) header += (header.empty() ? L"" : L"; ") + cookie.name + L"=" + cookie.value;
    if (!header.empty()) request.headers.emplace_back(L"Cookie", header);
    const std::wstring session = sessionValue(*site, jar);
    if (site->signsRequests && !session.empty()) {
      const std::wstring stamp = std::to_wstring(std::time(nullptr));
      const std::wstring origin = site->home;
      request.headers.emplace_back(L"Authorization",
                                   L"SAPISIDHASH " + stamp + L"_" + sha1Hex(utf8(stamp + L" " + session + L" " + origin)));
      request.headers.emplace_back(L"X-Origin", origin);
      request.headers.emplace_back(L"X-Goog-AuthUser", L"0");
    }
    next(std::move(request));
  });
}

LRESULT CALLBACK Accounts::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto self = static_cast<Accounts*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto self = reinterpret_cast<Accounts*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->handle(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT Accounts::handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      if (loginController_) {
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        loginController_->put_Bounds(bounds);
      }
      return 0;
    case WM_TIMER:
      if (wparam == kPollTimer) poll();
      return 0;
    case WM_DESTROY: {
      KillTimer(hwnd, kPollTimer);
      if (loginController_) loginController_->Close();
      loginController_.Reset();
      const std::wstring id = loginSite_->id;
      loginWindow_ = nullptr;
      loginSite_ = nullptr;
      changed_(id);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}
