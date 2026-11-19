#include "link.h"

#include <windows.h>

#include <regex>

#include <winrt/Windows.Foundation.Collections.h>

using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;

namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::optional<std::wstring> decoded(const std::wstring& text) {
  const std::string raw = winrt::to_string(text);
  std::string bytes;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '+') {
      bytes += ' ';
    } else if (raw[i] == '%') {
      if (i + 2 >= raw.size()) return std::nullopt;
      const int high = hexDigit(raw[i + 1]);
      const int low = hexDigit(raw[i + 2]);
      if (high < 0 || low < 0) return std::nullopt;
      bytes += char(high * 16 + low);
      i += 2;
    } else {
      bytes += raw[i];
    }
  }
  if (bytes.empty()) return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), nullptr, 0);
  if (n == 0) return std::nullopt;
  std::wstring out(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), int(bytes.size()), out.data(), n);
  return out;
}

bool textParam(const std::wstring& kind, const std::wstring& name) {
  if (kind == L"command") return name == L"action";
  return name == L"query" || name == L"source" || name == L"id";
}

}

bool isOpenLink(const std::wstring& link) {
  static const std::wregex pattern(std::wstring(L"^") + APP_ID + L"://open/?$");
  return std::regex_match(link, pattern);
}

std::optional<LinkRequest> requestOfLink(const std::wstring& link) {
  static const std::wregex pattern(std::wstring(L"^") + APP_ID + L"://(queue|command|dequeue)/?\\?(.*)$");
  std::wsmatch match;
  if (!std::regex_match(link, match, pattern)) return std::nullopt;
  const std::wstring kind = match[1].str();
  const std::wstring query = match[2].str();
  JsonObject request;
  size_t start = 0;
  while (start < query.size()) {
    size_t end = query.find(L'&', start);
    if (end == std::wstring::npos) end = query.size();
    const std::wstring pair = query.substr(start, end - start);
    start = end + 1;
    const size_t equals = pair.find(L'=');
    if (equals == std::wstring::npos) return std::nullopt;
    const auto name = decoded(pair.substr(0, equals));
    const auto value = decoded(pair.substr(equals + 1));
    if (!name || !value || request.HasKey(*name)) return std::nullopt;
    if (textParam(kind, *name)) {
      request.SetNamedValue(*name, JsonValue::CreateStringValue(*value));
    } else if (kind == L"queue" && *name == L"insert" && (*value == L"true" || *value == L"false")) {
      request.SetNamedValue(*name, JsonValue::CreateBooleanValue(*value == L"true"));
    } else {
      return std::nullopt;
    }
  }
  return LinkRequest{kind, request};
}
