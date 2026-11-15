#include "fonts.h"

#include <windows.h>

#include <algorithm>
#include <regex>

#include "files.h"
#include "http.h"

namespace {

constexpr wchar_t kGoogleCss[] = L"https://fonts.googleapis.com/css2?family=";
constexpr wchar_t kWholeFileAgent[] = L"curl/8.0";
constexpr wchar_t kRegularWeight[] = L"400";

std::wstring wide(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
  return out;
}

FontFile failed(std::wstring error) { return {{}, std::move(error)}; }

FontFile googleFileUrl(const FontRequest& request) {
  std::wstring url = kGoogleCss + request.gfont;
  if (request.weight != kRegularWeight) url += L":wght@" + request.weight;
  HttpResponse css = httpFetch({L"GET", url, {{L"User-Agent", kWholeFileAgent}}, {}});
  if (!css.error.empty()) return failed(css.error);
  if (css.status != 200) return failed(L"Google Fonts 回 " + std::to_wstring(css.status) + L"：" + request.gfont);
  std::wsmatch match;
  std::wstring text = wide(css.body);
  if (!std::regex_search(text, match, std::wregex(LR"(url\((https://[^)]+\.(?:ttf|otf))\))")))
    return failed(L"Google Fonts 沒給完整字檔：" + request.gfont);
  return {match[1].str(), {}};
}

std::wstring extensionOf(const std::wstring& url) {
  std::wsmatch match;
  if (std::regex_search(url, match, std::wregex(LR"(\.(ttf|otf|ttc)(?:$|[?#]))", std::regex::icase)))
    return L"." + match[1].str();
  return L".ttf";
}

}

FontFile fetchFontFile(const FontRequest& request) {
  std::wstring url = request.url;
  if (!request.gfont.empty()) {
    FontFile resolved = googleFileUrl(request);
    if (!resolved.error.empty()) return resolved;
    url = resolved.path;
  }
  std::wstring dir = appDataDir() + L"\\fonts";
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring path = dir + L"\\" + request.id + L"-" + request.weight + extensionOf(request.zipPath.empty() ? url : request.zipPath);
  if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return {path, {}};

  HttpResponse file = httpFetch({L"GET", url, {}, {}});
  if (!file.error.empty()) return failed(file.error);
  if (file.status != 200 || file.body.empty())
    return failed(L"字檔下載失敗（HTTP " + std::to_wstring(file.status) + L"）：" + url);
  std::wstring partial = path + L".part";
  if (request.zipPath.empty()) {
    writeBytesFile(partial, file.body);
  } else {
    std::wstring work = dir + L"\\" + request.id + L"-" + request.weight + L".unzip";
    CreateDirectoryW(work.c_str(), nullptr);
    std::wstring archive = work + L"\\font.zip";
    writeBytesFile(archive, file.body);
    std::wstring error = unzip(archive, work, request.zipPath);
    std::wstring extracted = work + L"\\" + request.zipPath;
    std::replace(extracted.begin(), extracted.end(), L'/', L'\\');
    if (error.empty() && !MoveFileExW(extracted.c_str(), partial.c_str(), MOVEFILE_REPLACE_EXISTING))
      error = L"壓縮檔裡沒有 " + request.zipPath;
    DeleteFileW(archive.c_str());
    RemoveDirectoryW(work.c_str());
    if (!error.empty()) return failed(error);
  }
  if (!MoveFileExW(partial.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    return failed(L"字檔存不起來，錯誤碼 " + std::to_wstring(GetLastError()));
  return {path, {}};
}
