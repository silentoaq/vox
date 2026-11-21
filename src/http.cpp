#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include <functional>
#include <vector>

namespace {

constexpr DWORD kReadChunk = 1 << 20;

constexpr wchar_t kUserAgent[] =
  L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36";

class Handle {
public:
  explicit Handle(HINTERNET h) : h_(h) {}
  ~Handle() {
    if (h_) WinHttpCloseHandle(h_);
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HINTERNET get() const { return h_; }
  explicit operator bool() const { return h_ != nullptr; }

private:
  HINTERNET h_;
};

HINTERNET session() {
  static HINTERNET s = [] {
    HINTERNET h = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof decompress);
    return h;
  }();
  return s;
}

HttpResponse failure(const wchar_t* step) {
  return {0, {}, std::wstring(step) + L"失敗，錯誤碼 " + std::to_wstring(GetLastError())};
}

struct UrlParts {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = 0;
  bool secure = false;
};

bool crack(const std::wstring& url, UrlParts& out) {
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
  out.host.assign(parts.lpszHostName, parts.dwHostNameLength);
  out.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength);
  out.port = parts.nPort;
  out.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

HttpResponse httpExchange(const HttpRequest& request, const std::function<bool(const char*, DWORD)>& sink) {
  UrlParts parts;
  if (!crack(request.url, parts)) return failure(L"解析網址");

  Handle connection(WinHttpConnect(session(), parts.host.c_str(), parts.port, 0));
  if (!connection) return failure(L"連線");

  Handle req(WinHttpOpenRequest(connection.get(), request.method.c_str(), parts.path.c_str(), nullptr,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                parts.secure ? WINHTTP_FLAG_SECURE : 0));
  if (!req) return failure(L"建立請求");

  for (const auto& [name, value] : request.headers) {
    std::wstring line = name + L": " + value;
    if (!WinHttpAddRequestHeaders(req.get(), line.c_str(), DWORD(-1L), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
      return failure(L"加入標頭");
  }

  if (!WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          request.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)request.body.data(),
                          DWORD(request.body.size()), DWORD(request.body.size()), 0))
    return failure(L"送出請求");
  if (!WinHttpReceiveResponse(req.get(), nullptr)) return failure(L"接收回應");

  HttpResponse response;
  DWORD status = 0;
  DWORD size = sizeof status;
  WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  response.status = status;

  std::vector<char> buffer(kReadChunk);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(req.get(), buffer.data(), DWORD(buffer.size()), &read)) return failure(L"讀取回應");
    if (read == 0) break;
    if (!sink(buffer.data(), read)) return failure(L"寫入回應");
  }
  return response;
}

}

std::wstring hostOf(const std::wstring& url) {
  UrlParts parts;
  return crack(url, parts) ? parts.host : std::wstring();
}

HttpResponse httpFetch(const HttpRequest& request) {
  std::string body;
  HttpResponse response = httpExchange(request, [&body](const char* data, DWORD size) {
    body.append(data, size);
    return true;
  });
  response.body = std::move(body);
  return response;
}

std::wstring httpDownload(const std::wstring& url, const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return L"寫不了 " + path + L"，錯誤碼 " + std::to_wstring(GetLastError());
  HttpResponse response = httpExchange({L"GET", url, {}, {}}, [file](const char* data, DWORD size) {
    DWORD written = 0;
    return WriteFile(file, data, size, &written, nullptr) && written == size;
  });
  CloseHandle(file);
  if (!response.error.empty()) return response.error;
  if (response.status != 200) return L"伺服器回 " + std::to_wstring(response.status);
  return {};
}
