#pragma once
#include <windows.h>

#include <optional>
#include <string>

inline constexpr wchar_t kWindowClass[] = APP_ID L".main";
inline constexpr wchar_t kInstanceMutex[] = L"Local\\" APP_ID L".instance";
inline constexpr wchar_t kAppUserModelId[] = APP_NAME;
inline constexpr wchar_t kManifestFile[] = L"manifest.json";
inline constexpr wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" APP_NAME;

inline constexpr wchar_t kCurrentMarker[] = L"current";
inline constexpr wchar_t kPendingMarker[] = L"pending";
inline constexpr wchar_t kTryingMarker[] = L"trying";
inline constexpr wchar_t kRejectedMarker[] = L"rejected";

std::wstring installRoot();
std::wstring removeInstall(const std::wstring& root);
std::wstring versionDir(const std::wstring& root, const std::wstring& version);
bool isVersionText(const std::wstring& text);
std::optional<std::wstring> readMarker(const std::wstring& root, const wchar_t* name);
void writeMarker(const std::wstring& root, const wchar_t* name, const std::wstring& version);
void removeMarker(const std::wstring& root, const wchar_t* name);
std::wstring removeTree(const std::wstring& path);
std::wstring removeVersionsExcept(const std::wstring& root, const std::wstring& keep);
std::wstring recordInstalledVersion(const std::wstring& version);
std::wstring installLauncher(const std::wstring& root, const std::wstring& version);
bool appRunning();
std::wstring closeRunningApp();
