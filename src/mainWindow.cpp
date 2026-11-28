#include "mainWindow.h"

#include <dwmapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <thread>
#include <utility>

#include "WebView2EnvironmentOptions.h"
#include "appIcon.h"
#include "files.h"
#include "fonts.h"
#include "link.h"
#include "lockdown.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using namespace winrt::Windows::Data::Json;

namespace {

constexpr wchar_t kHostName[] = APP_ID L".localhost";
constexpr wchar_t kStemsHostName[] = L"stems." APP_ID L".localhost";
constexpr wchar_t kPageUrl[] = L"https://" APP_ID L".localhost/index.html";
constexpr wchar_t kPageOrigin[] = L"https://" APP_ID L".localhost/";
constexpr wchar_t kPitchWorkletPath[] = L"/" APP_ID L"-pitch-worklet.js";
constexpr wchar_t kPitchWorkletFile[] = L"\\" APP_ID L"-pitch-worklet.js";
constexpr const wchar_t* kPitchWorkletOrigins[] = {L"https://www.youtube.com", L"https://open.spotify.com"};
constexpr COREWEBVIEW2_COLOR kBackground = {255, 0x0d, 0x0d, 0x10};
constexpr int kMinWidth = 960;
constexpr int kMinHeight = 620;
constexpr int kStartWidth = 1360;
constexpr int kStartHeight = 860;

std::wstring hexOfColor(UINT32 rgb) {
  wchar_t text[8];
  swprintf_s(text, L"#%06X", rgb & 0xFFFFFF);
  return text;
}

UINT32 colorOfHex(const std::wstring& hex) {
  if (hex.size() != 7 || hex[0] != L'#' || hex.find_first_not_of(L"0123456789abcdefABCDEF", 1) != std::wstring::npos)
    throw winrt::hresult_invalid_argument(L"顏色格式不對：" + hex);
  return UINT32(std::stoul(hex.substr(1), nullptr, 16));
}

std::vector<LyricLine> lyricLinesOf(const JsonArray& items) {
  std::vector<LyricLine> lines;
  for (const auto& item : items) {
    auto json = item.GetObject();
    LyricLine line{json.GetNamedNumber(L"start"), std::wstring(json.GetNamedString(L"text")), {}};
    auto chars = json.GetNamedValue(L"chars");
    if (chars.ValueType() == JsonValueType::Array) {
      for (const auto& value : chars.GetArray()) {
        auto c = value.GetObject();
        line.chars.push_back({c.GetNamedNumber(L"t"), c.GetNamedNumber(L"e"), UINT32(c.GetNamedNumber(L"pos")),
                              UINT32(c.GetNamedNumber(L"len"))});
      }
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

}

MainWindow::MainWindow(HINSTANCE instance) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance;
  wc.lpszClassName = kWindowClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = appIcon(instance, SM_CXICON);
  wc.hIconSm = appIcon(instance, SM_CXSMICON);
  wc.hbrBackground = CreateSolidBrush(RGB(kBackground.R, kBackground.G, kBackground.B));
  RegisterClassExW(&wc);

  UINT dpi = GetDpiForSystem();
  RECT work;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int width = std::min(MulDiv(kStartWidth, dpi, 96), int(work.right - work.left));
  const int height = std::min(MulDiv(kStartHeight, dpi, 96), int(work.bottom - work.top));
  CreateWindowExW(0, kWindowClass, APP_NAME, WS_OVERLAPPEDWINDOW, work.left + (work.right - work.left - width) / 2,
                  work.top + (work.bottom - work.top - height) / 2, width, height, nullptr, nullptr, instance, this);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

  accounts_ = std::make_unique<Accounts>(instance, hwnd_, [this](const std::wstring& site) {
    JsonObject params;
    params.SetNamedValue(L"site", JsonValue::CreateStringValue(site));
    bridge_->emit(L"account", params);
  });
  bridge_ = std::make_unique<Bridge>(
    hwnd_,
    [this](const std::wstring& json) {
      if (webview_) webview_->PostWebMessageAsJson(json.c_str());
    },
    [this](HttpRequest request, std::function<void(HttpRequest)> next) {
      accounts_->decorate(std::move(request), std::move(next));
    });
  lyrics_ = std::make_unique<DesktopLyrics>(instance, [this](const std::wstring& action) {
    if (action == L"state") {
      bridge_->emit(L"desktopLyricsState", desktopLyricsState());
      return;
    }
    control(action);
  });
  karaoke_ = std::make_unique<Karaoke>(hwnd_, [this](const KaraokeStatus& status) {
    JsonObject params;
    params.SetNamedValue(L"id", JsonValue::CreateStringValue(status.id));
    params.SetNamedValue(L"state", JsonValue::CreateStringValue(status.state));
    params.SetNamedValue(L"segment", JsonValue::CreateNumberValue(status.segment));
    params.SetNamedValue(L"total", JsonValue::CreateNumberValue(status.total));
    params.SetNamedValue(L"stride", JsonValue::CreateNumberValue(status.stride));
    params.SetNamedValue(L"samples", JsonValue::CreateNumberValue(status.samples));
    params.SetNamedValue(L"message", JsonValue::CreateStringValue(status.message));
    params.SetNamedValue(L"progress", JsonValue::CreateNumberValue(status.progress));
    bridge_->emit(L"karaoke", params);
  });
  nowPlaying_ = std::make_unique<NowPlaying>(hwnd_, [this](const std::wstring& action) {
    runOnUi([this, action] { control(action); });
  });
  updater_ = std::make_unique<Updater>([this](const UpdateStatus& status) {
    runOnUi([this, status] {
      JsonObject params;
      params.SetNamedValue(L"state", JsonValue::CreateStringValue(status.state));
      params.SetNamedValue(L"version", JsonValue::CreateStringValue(status.version));
      params.SetNamedValue(L"message", JsonValue::CreateStringValue(status.message));
      bridge_->emit(L"update", params);
    });
  });
  registerMethods();
  createWebView();
}

void MainWindow::control(const std::wstring& action) {
  JsonObject params;
  params.SetNamedValue(L"action", JsonValue::CreateStringValue(action));
  bridge_->emit(L"control", params);
}

Bridge::JsonObject MainWindow::desktopLyricsState() const {
  JsonObject state;
  state.SetNamedValue(L"visible", JsonValue::CreateBooleanValue(lyrics_->visible()));
  state.SetNamedValue(L"locked", JsonValue::CreateBooleanValue(lyrics_->locked()));
  const LyricLook& look = lyrics_->look();
  state.SetNamedValue(L"sung", JsonValue::CreateStringValue(hexOfColor(look.sung)));
  state.SetNamedValue(L"unsung", JsonValue::CreateStringValue(hexOfColor(look.unsung)));
  state.SetNamedValue(L"outline", JsonValue::CreateStringValue(hexOfColor(look.outline)));
  state.SetNamedValue(L"outlineWidth", JsonValue::CreateNumberValue(look.outlineWidth));
  state.SetNamedValue(L"weight", JsonValue::CreateNumberValue(look.weight));
  state.SetNamedValue(L"backgroundOn", JsonValue::CreateBooleanValue(look.backgroundOn));
  state.SetNamedValue(L"background", JsonValue::CreateStringValue(hexOfColor(look.background)));
  return state;
}

Bridge::JsonObject MainWindow::languageState() const {
  JsonObject state;
  state.SetNamedValue(L"setting", JsonValue::CreateStringValue(languageSetting()));
  state.SetNamedValue(L"active", JsonValue::CreateStringValue(languageId()));
  return state;
}

Bridge::JsonObject MainWindow::windowState() const {
  JsonObject state;
  state.SetNamedValue(L"shown", JsonValue::CreateBooleanValue(windowShown_));
  return state;
}

void MainWindow::syncShown() {
  const bool shown = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
  if (shown == windowShown_) return;
  windowShown_ = shown;
  if (pageReady_) bridge_->emit(L"window", windowState());
}

void MainWindow::registerMethods() {
  bridge_->on(L"ready", [this](const JsonObject&) {
    pageReady_ = true;
    sendPendingLinks();
    updater_->started();
    JsonObject result;
    result.SetNamedValue(L"desktopLyrics", desktopLyricsState());
    result.SetNamedValue(L"window", windowState());
    result.SetNamedValue(L"stems", JsonValue::CreateStringValue(std::wstring(L"https://") + kStemsHostName));
    return result;
  });
  bridge_->on(L"language.get", [this](const JsonObject&) { return languageState(); });
  bridge_->on(L"language.set", [this](const JsonObject& params) {
    const std::wstring setting(params.GetNamedString(L"setting"));
    if (!chooseLanguage(setting)) throw winrt::hresult_invalid_argument(L"沒有這個語言：" + setting);
    lyrics_->languageChanged();
    return languageState();
  });
  bridge_->on(L"nowPlaying.track", [this](const JsonObject& params) {
    const std::wstring title(params.GetNamedString(L"title"));
    if (title.empty()) nowPlaying_->clearTrack();
    else
      nowPlaying_->setTrack({title, std::wstring(params.GetNamedString(L"artist")), std::wstring(params.GetNamedString(L"album")),
                             std::wstring(params.GetNamedString(L"cover"))});
    return JsonObject();
  });
  bridge_->on(L"nowPlaying.playback", [this](const JsonObject& params) {
    nowPlaying_->setPlayback(std::wstring(params.GetNamedString(L"state")), params.GetNamedNumber(L"position"),
                             params.GetNamedNumber(L"duration"));
    return JsonObject();
  });
  bridge_->on(L"nowPlaying.lyrics", [this](const JsonObject& params) {
    nowPlaying_->setLyrics(std::wstring(params.GetNamedString(L"text")));
    return JsonObject();
  });
  bridge_->onAsync(L"karaoke.prepare", [this](const JsonObject& params, Bridge::Respond respond, Bridge::Fail) {
    std::wstring id(params.GetNamedString(L"id"));
    double from = params.GetNamedNumber(L"from");
    bool urgent = params.GetNamedBoolean(L"urgent");
    accounts_->signedIn(L"youtube", [this, id, from, urgent, respond](bool signedIn) {
      if (!signedIn) {
        karaoke_->prepare(id, from, urgent, {});
        respond(JsonObject());
        return;
      }
      accounts_->netscapeCookies(L"youtube", [this, id, from, urgent, respond](std::string cookies) {
        karaoke_->prepare(id, from, urgent, std::move(cookies));
        respond(JsonObject());
      });
    });
  });
  bridge_->onAsync(L"account.state", [this](const JsonObject& params, Bridge::Respond respond, Bridge::Fail) {
    accounts_->signedIn(std::wstring(params.GetNamedString(L"site")), [respond](bool signedIn) {
      JsonObject result;
      result.SetNamedValue(L"signedIn", JsonValue::CreateBooleanValue(signedIn));
      respond(result);
    });
  });
  bridge_->on(L"account.login", [this](const JsonObject& params) {
    accounts_->login(std::wstring(params.GetNamedString(L"site")));
    return JsonObject();
  });
  bridge_->onAsync(L"account.logout", [this](const JsonObject& params, Bridge::Respond respond, Bridge::Fail) {
    accounts_->logout(std::wstring(params.GetNamedString(L"site")), [respond] { respond(JsonObject()); });
  });
  bridge_->on(L"library.load", [](const JsonObject&) {
    auto text = readUtf8File(appDataDir() + L"\\library.json");
    JsonObject result;
    result.SetNamedValue(L"text", JsonValue::CreateStringValue(text ? *text : std::wstring()));
    return result;
  });
  bridge_->on(L"library.save", [](const JsonObject& params) {
    writeUtf8File(appDataDir() + L"\\library.json", std::wstring(params.GetNamedString(L"text")));
    return JsonObject();
  });
  bridge_->on(L"desktopLyrics.song", [this](const JsonObject& params) {
    lyrics_->setSong(std::wstring(params.GetNamedString(L"title")), std::wstring(params.GetNamedString(L"artist")),
                     lyricLinesOf(params.GetNamedArray(L"lines")));
    return JsonObject();
  });
  bridge_->on(L"desktopLyrics.clock", [this](const JsonObject& params) {
    lyrics_->setClock(params.GetNamedNumber(L"ms"), params.GetNamedBoolean(L"playing"));
    return JsonObject();
  });
  bridge_->onAsync(L"desktopLyrics.font", [this](const JsonObject& params, Bridge::Respond respond, Bridge::Fail fail) {
    unsigned serial = ++fontSerial_;
    if (!params.HasKey(L"id")) {
      lyrics_->setFont({});
      respond(JsonObject());
      return;
    }
    FontRequest request{std::wstring(params.GetNamedString(L"id")), std::wstring(params.GetNamedString(L"weight")),
                        std::wstring(params.GetNamedString(L"url", L"")), std::wstring(params.GetNamedString(L"gfont", L"")),
                        std::wstring(params.GetNamedString(L"zipPath", L""))};
    std::thread([this, serial, request, respond, fail] {
      FontFile file = fetchFontFile(request);
      runOnUi([this, serial, file, respond, fail] {
        if (!file.error.empty()) {
          fail(file.error);
          return;
        }
        if (serial == fontSerial_) {
          try {
            lyrics_->setFont(file.path);
          } catch (const winrt::hresult_error& e) {
            DeleteFileW(file.path.c_str());
            fail(L"字檔讀不出來：" + std::wstring(e.message()));
            return;
          }
        }
        respond(JsonObject());
      });
    }).detach();
  });
  bridge_->on(L"desktopLyrics.look", [this](const JsonObject& params) {
    LyricLook look;
    look.sung = colorOfHex(std::wstring(params.GetNamedString(L"sung")));
    look.unsung = colorOfHex(std::wstring(params.GetNamedString(L"unsung")));
    look.outline = colorOfHex(std::wstring(params.GetNamedString(L"outline")));
    look.outlineWidth = float(params.GetNamedNumber(L"outlineWidth"));
    look.weight = int(params.GetNamedNumber(L"weight"));
    look.backgroundOn = params.GetNamedBoolean(L"backgroundOn");
    look.background = colorOfHex(std::wstring(params.GetNamedString(L"background")));
    lyrics_->setLook(look, params.GetNamedBoolean(L"save"));
    return JsonObject();
  });
  bridge_->on(L"desktopLyrics.set", [this](const JsonObject& params) {
    if (params.HasKey(L"visible")) lyrics_->setVisible(params.GetNamedBoolean(L"visible"));
    if (params.HasKey(L"locked")) lyrics_->setLocked(params.GetNamedBoolean(L"locked"));
    return desktopLyricsState();
  });
}

bool MainWindow::runOnUi(std::function<void()> task) {
  auto owned = std::make_unique<std::function<void()>>(std::move(task));
  if (!PostMessageW(hwnd_, WM_APP_RUN, 0, reinterpret_cast<LPARAM>(owned.get()))) return false;
  owned.release();
  return true;
}

void MainWindow::show(int command) { ShowWindow(hwnd_, command); }

void MainWindow::openLink(const std::wstring& link) {
  auto request = requestOfLink(link);
  if (!request) {
    OutputDebugStringW((L"看不懂的連結：" + link + L"\n").c_str());
    return;
  }
  pendingLinks_.push_back(*request);
  if (pageReady_) sendPendingLinks();
}

void MainWindow::sendPendingLinks() {
  for (const auto& link : pendingLinks_) {
    JsonObject params;
    params.SetNamedValue(L"kind", JsonValue::CreateStringValue(link.kind));
    params.SetNamedValue(L"request", link.request);
    bridge_->emit(L"link", params);
  }
  pendingLinks_.clear();
}

void MainWindow::activate() {
  if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  SetForegroundWindow(hwnd_);
}

HRESULT MainWindow::fatal(Text step, HRESULT hr) {
  MessageBoxW(hwnd_, tr(Text::StartFailed, {APP_NAME, tr(step), hexText(hr)}).c_str(), APP_NAME, MB_ICONERROR);
  DestroyWindow(hwnd_);
  return hr;
}

void MainWindow::createWebView() {
  auto pitchWorklet = readUtf8File(exeDir() + L"\\ui" + kPitchWorkletFile);
  auto effectsGraph = readUtf8File(exeDir() + L"\\ui\\js\\effectsGraph.js");
  auto frameBody = readUtf8File(exeDir() + L"\\ui\\inject\\embedFrame.js");
  if (!pitchWorklet || !effectsGraph || !frameBody) {
    fatal(Text::StepReadPlayerScript, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    return;
  }
  constexpr std::wstring_view kExport = L"\nexport ";
  std::wstring graph = L"\n" + *effectsGraph;
  if (graph.find(kExport) == std::wstring::npos) {
    fatal(Text::StepParseEffects, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    return;
  }
  for (size_t at; (at = graph.find(kExport)) != std::wstring::npos;) graph.replace(at, kExport.size(), L"\n");
  const std::string worklet = winrt::to_string(*pitchWorklet);
  const std::wstring frameScript = L"(() => {\nconst pitchWorkletUrl = location.origin + '" + std::wstring(kPitchWorkletPath) +
                                   L"';\n" + graph + L"\n" + *frameBody + L"\n})();";
  clearDebugArguments();
  auto options = Make<CoreWebView2EnvironmentOptions>();
  options->put_AdditionalBrowserArguments(
    L"--autoplay-policy=no-user-gesture-required --disable-features=HardwareMediaKeyHandling");
  HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
    nullptr, (appDataDir() + L"\\WebView2").c_str(), options.Get(),
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
      [this, script = frameScript, worklet](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(result)) return fatal(Text::StepStartWebView, result);
        ComPtr<ICoreWebView2Environment> environment(env);
        return env->CreateCoreWebView2Controller(
          hwnd_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                   [this, script, worklet, environment](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                     if (FAILED(result)) return fatal(Text::StepCreateWebView, result);
                     controller_ = controller;
                     controller_->get_CoreWebView2(&webview_);
                     lockDown(webview_.Get());

                     ComPtr<ICoreWebView2_2> webview2;
                     webview_.As(&webview2);
                     ComPtr<ICoreWebView2CookieManager> cookies;
                     webview2->get_CookieManager(&cookies);
                     accounts_->attach(environment.Get(), cookies.Get());

                     ComPtr<ICoreWebView2Controller2> controller2;
                     controller_.As(&controller2);
                     controller2->put_DefaultBackgroundColor(kBackground);
                     fitWebView();

                     ComPtr<ICoreWebView2_3> webview3;
                     webview_.As(&webview3);
                     webview3->SetVirtualHostNameToFolderMapping(kHostName, (exeDir() + L"\\ui").c_str(),
                                                                 COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY);
                     webview3->SetVirtualHostNameToFolderMapping(kStemsHostName, Karaoke::stemsDir().c_str(),
                                                                 COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);

                     ComPtr<ICoreWebView2_22> webview22;
                     webview_.As(&webview22);
                     for (const wchar_t* origin : kPitchWorkletOrigins)
                       webview22->AddWebResourceRequestedFilterWithRequestSourceKinds(
                         (std::wstring(origin) + kPitchWorkletPath).c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                         COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL);
                     webview_->add_WebResourceRequested(
                       Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                         [environment, worklet](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                           ComPtr<ICoreWebView2WebResourceRequest> request;
                           args->get_Request(&request);
                           LPWSTR uri = nullptr;
                           request->get_Uri(&uri);
                           const bool ours = std::wstring_view(uri).ends_with(kPitchWorkletPath);
                           CoTaskMemFree(uri);
                           if (!ours) return S_OK;
                           ComPtr<IStream> body;
                           body.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(worklet.data()), UINT(worklet.size())));
                           ComPtr<ICoreWebView2WebResourceResponse> response;
                           HRESULT made = environment->CreateWebResourceResponse(
                             body.Get(), 200, L"OK", L"Content-Type: text/javascript; charset=utf-8", &response);
                           if (FAILED(made)) return made;
                           return args->put_Response(response.Get());
                         })
                         .Get(),
                       nullptr);

                     webview_->add_WebMessageReceived(
                       Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                         [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                           LPWSTR source = nullptr;
                           args->get_Source(&source);
                           bool ours = std::wstring_view(source).starts_with(kPageOrigin);
                           CoTaskMemFree(source);
                           if (!ours) return S_OK;
                           LPWSTR json = nullptr;
                           args->get_WebMessageAsJson(&json);
                           try {
                             bridge_->handle(json);
                           } catch (const winrt::hresult_error& e) {
                             OutputDebugStringW((L"頁面訊息格式錯誤：" + std::wstring(e.message()) + L"\n").c_str());
                           }
                           CoTaskMemFree(json);
                           return S_OK;
                         })
                         .Get(),
                       nullptr);

                     return webview_->AddScriptToExecuteOnDocumentCreated(
                       script.c_str(),
                       Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                         [this](HRESULT added, LPCWSTR) -> HRESULT {
                           if (FAILED(added)) return fatal(Text::StepInjectScript, added);
                           return webview_->Navigate(kPageUrl);
                         })
                         .Get());
                   })
                   .Get());
      })
      .Get());
  if (FAILED(hr)) fatal(Text::StepStartWebView, hr);
}

void MainWindow::fitWebView() {
  if (!controller_) return;
  RECT bounds;
  GetClientRect(hwnd_, &bounds);
  controller_->put_Bounds(bounds);
}

LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->handle(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT MainWindow::handle(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      if (wparam != SIZE_MINIMIZED) fitWebView();
      return 0;
    case WM_WINDOWPOSCHANGED:
      syncShown();
      break;
    case WM_DPICHANGED: {
      auto* rect = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      UINT dpi = GetDpiForWindow(hwnd_);
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {MulDiv(kMinWidth, dpi, 96), MulDiv(kMinHeight, dpi, 96)};
      return 0;
    }
    case WM_COPYDATA: {
      auto* data = reinterpret_cast<COPYDATASTRUCT*>(lparam);
      if (data->dwData == ULONG_PTR(CopyKind::Activate)) {
        activate();
        return TRUE;
      }
      if (data->dwData == ULONG_PTR(CopyKind::Link)) {
        openLink(std::wstring(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t)));
        return TRUE;
      }
      return FALSE;
    }
    case WM_APP_HTTP_DONE:
      bridge_->finishHttp(lparam);
      return 0;
    case WM_APP_KARAOKE:
      karaoke_->finish(lparam);
      return 0;
    case WM_APP_RUN: {
      std::unique_ptr<std::function<void()>> task(reinterpret_cast<std::function<void()>*>(lparam));
      (*task)();
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}
