#pragma once
#include <optional>
#include <string>

std::wstring exeDir();
std::wstring appDataDir();
std::optional<std::wstring> readUtf8File(const std::wstring& path);
void writeUtf8File(const std::wstring& path, const std::wstring& text);
void writeBytesFile(const std::wstring& path, const std::string& bytes);
std::wstring unzip(const std::wstring& archive, const std::wstring& dir, const std::wstring& member = {});
