#include "link.h"

#include <regex>

std::optional<std::wstring> videoIdOfLink(const std::wstring& link) {
  static const std::wregex pattern(std::wstring(L"^") + APP_ID + L"://play/([A-Za-z0-9_-]{11})$");
  std::wsmatch match;
  if (!std::regex_match(link, match, pattern)) return std::nullopt;
  return match[1].str();
}
