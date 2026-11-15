#include "bridge.h"

#include <memory>
#include <set>
#include <thread>

using namespace winrt::Windows::Data::Json;

namespace {

const std::set<std::wstring> kReachableHosts = {
  L"music.youtube.com",
  L"www.youtube.com",
  L"mobilecdn.kugou.com",
  L"krcs.kugou.com",
  L"lyrics.kugou.com",
  L"open.spotify.com",
  L"open.spotifycdn.com",
  L"api-partner.spotify.com",
};

struct HttpJob {
  double id;
  HttpResponse response;
};

std::string utf8(const std::wstring& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring wide(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
  return out;
}

}

Bridge::Bridge(HWND hwnd, Send send, Decorate decorate)
    : hwnd_(hwnd), send_(std::move(send)), decorate_(std::move(decorate)) {}

void Bridge::on(const std::wstring& method, Handler handler) { handlers_[method] = std::move(handler); }

void Bridge::onAsync(const std::wstring& method, AsyncHandler handler) {
  asyncHandlers_[method] = std::move(handler);
}

void Bridge::handle(const std::wstring& json) {
  JsonObject message = JsonObject::Parse(json);
  double id = message.GetNamedNumber(L"id");
  std::wstring method(message.GetNamedString(L"method"));
  JsonObject params = message.GetNamedObject(L"params");

  if (method == L"http") {
    startHttp(id, params);
    return;
  }
  try {
    if (auto async = asyncHandlers_.find(method); async != asyncHandlers_.end()) {
      async->second(params, [this, id](const Value& result) { reply(id, result); },
                    [this, id](const std::wstring& error) { fail(id, error); });
      return;
    }
    auto handler = handlers_.find(method);
    if (handler == handlers_.end()) {
      fail(id, L"沒有這個方法：" + method);
      return;
    }
    reply(id, handler->second(params));
  } catch (const winrt::hresult_error& e) {
    fail(id, std::wstring(e.message()));
  }
}

void Bridge::startHttp(double id, const JsonObject& params) {
  HttpRequest request;
  request.url = std::wstring(params.GetNamedString(L"url"));
  request.method = std::wstring(params.GetNamedString(L"method"));
  request.body = utf8(std::wstring(params.GetNamedString(L"body")));
  for (const auto& pair : params.GetNamedObject(L"headers"))
    request.headers.emplace_back(std::wstring(pair.Key()), std::wstring(pair.Value().GetString()));

  std::wstring host = hostOf(request.url);
  if (!kReachableHosts.contains(host)) {
    fail(id, L"不允許連到 " + host);
    return;
  }

  auto send = [hwnd = hwnd_, id](HttpRequest ready) {
    std::thread([hwnd, id, request = std::move(ready)] {
      auto job = std::make_unique<HttpJob>(HttpJob{id, httpFetch(request)});
      if (PostMessageW(hwnd, WM_APP_HTTP_DONE, 0, reinterpret_cast<LPARAM>(job.get()))) job.release();
    }).detach();
  };
  if (params.GetNamedBoolean(L"anonymous")) send(std::move(request));
  else decorate_(std::move(request), send);
}

void Bridge::finishHttp(LPARAM lparam) {
  std::unique_ptr<HttpJob> job(reinterpret_cast<HttpJob*>(lparam));
  if (!job->response.error.empty()) {
    fail(job->id, job->response.error);
    return;
  }
  JsonObject result;
  result.SetNamedValue(L"status", JsonValue::CreateNumberValue(job->response.status));
  result.SetNamedValue(L"body", JsonValue::CreateStringValue(wide(job->response.body)));
  reply(job->id, result);
}

void Bridge::emit(const std::wstring& event, const JsonObject& params) {
  JsonObject message;
  message.SetNamedValue(L"event", JsonValue::CreateStringValue(event));
  message.SetNamedValue(L"params", params);
  send_(std::wstring(message.Stringify()));
}

void Bridge::reply(double id, const Value& result) {
  JsonObject message;
  message.SetNamedValue(L"id", JsonValue::CreateNumberValue(id));
  message.SetNamedValue(L"result", result);
  send_(std::wstring(message.Stringify()));
}

void Bridge::fail(double id, const std::wstring& error) {
  JsonObject message;
  message.SetNamedValue(L"id", JsonValue::CreateNumberValue(id));
  message.SetNamedValue(L"error", JsonValue::CreateStringValue(error));
  send_(std::wstring(message.Stringify()));
}
