#pragma once
#include <string>
#include <utility>
#include <vector>

struct HttpRequest {
  std::wstring method;
  std::wstring url;
  std::vector<std::pair<std::wstring, std::wstring>> headers;
  std::string body;
};

struct HttpResponse {
  unsigned status = 0;
  std::string body;
  std::wstring error;
};

std::wstring hostOf(const std::wstring& url);
HttpResponse httpFetch(const HttpRequest& request);
std::wstring httpDownload(const std::wstring& url, const std::wstring& path);
