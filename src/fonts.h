#pragma once
#include <string>

struct FontRequest {
  std::wstring id;
  std::wstring weight;
  std::wstring url;
  std::wstring gfont;
  std::wstring zipPath;
};

struct FontFile {
  std::wstring path;
  std::wstring error;
};

FontFile fetchFontFile(const FontRequest& request);
