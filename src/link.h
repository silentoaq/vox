#pragma once
#include <optional>
#include <string>

#include <winrt/Windows.Data.Json.h>

struct LinkRequest {
  std::wstring kind;
  winrt::Windows::Data::Json::JsonObject request;
};

bool isOpenLink(const std::wstring& link);
std::optional<LinkRequest> requestOfLink(const std::wstring& link);
