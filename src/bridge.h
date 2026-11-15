#pragma once
#include <windows.h>

#include <functional>
#include <map>
#include <string>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include "http.h"

inline constexpr UINT WM_APP_HTTP_DONE = WM_APP + 1;

class Bridge {
public:
  using JsonObject = winrt::Windows::Data::Json::JsonObject;
  using Value = winrt::Windows::Data::Json::IJsonValue;
  using Send = std::function<void(const std::wstring& json)>;
  using Handler = std::function<Value(const JsonObject& params)>;
  using Respond = std::function<void(const Value& result)>;
  using Fail = std::function<void(const std::wstring& error)>;
  using AsyncHandler = std::function<void(const JsonObject& params, Respond respond, Fail fail)>;
  using Decorate = std::function<void(HttpRequest request, std::function<void(HttpRequest)> next)>;

  Bridge(HWND hwnd, Send send, Decorate decorate);

  void on(const std::wstring& method, Handler handler);
  void onAsync(const std::wstring& method, AsyncHandler handler);
  void handle(const std::wstring& json);
  void finishHttp(LPARAM job);
  void emit(const std::wstring& event, const JsonObject& params);

private:
  void reply(double id, const Value& result);
  void fail(double id, const std::wstring& error);
  void startHttp(double id, const JsonObject& params);

  HWND hwnd_;
  Send send_;
  Decorate decorate_;
  std::map<std::wstring, Handler> handlers_;
  std::map<std::wstring, AsyncHandler> asyncHandlers_;
};
