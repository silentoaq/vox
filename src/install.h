#pragma once
#include <windows.h>

#include <optional>
#include <string>

inline constexpr wchar_t kWindowClass[] = APP_ID L".main";
inline constexpr wchar_t kInstanceMutex[] = L"Local\\" APP_ID L".instance";
inline constexpr wchar_t kAppUserModelId[] = APP_NAME L".exe";
inline constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_NAME;
inline constexpr wchar_t kLinkKey[] = L"Software\\Classes\\" APP_ID;

bool isVersionText(const std::wstring& text);
std::optional<std::wstring> installedDir();
void repairLinkRegistration(const std::wstring& dir);
