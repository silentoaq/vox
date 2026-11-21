#include "install.h"

#include <shlobj.h>

#include <regex>

#include "files.h"

namespace {

std::optional<std::wstring> readString(const wchar_t* key, const wchar_t* name) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) return std::nullopt;
  std::wstring value(size / sizeof(wchar_t), L'\0');
  if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) return std::nullopt;
  value.resize(wcslen(value.c_str()));
  return value;
}

bool writeString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
  return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
}

}

bool isVersionText(const std::wstring& text) {
  static const std::wregex pattern(L"^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$");
  return std::regex_match(text, pattern);
}

std::optional<std::wstring> installedDir() {
  auto location = readString(kUninstallKey, L"InstallLocation");
  if (!location) return std::nullopt;
  const std::wstring dir = exeDir();
  if (CompareStringOrdinal(location->c_str(), int(location->size()), dir.c_str(), int(dir.size()), TRUE) != CSTR_EQUAL)
    return std::nullopt;
  return dir;
}

void repairLinkRegistration(const std::wstring& dir) {
  const std::wstring exe = L"\"" + dir + L"\\" APP_NAME L".exe\"";
  const std::wstring command = exe + L" \"%1\"";
  const std::wstring key = kLinkKey;
  if (readString((key + L"\\shell\\open\\command").c_str(), nullptr) == command) return;
  const bool written = writeString(key, nullptr, L"URL:" APP_NAME) && writeString(key, L"URL Protocol", L"") &&
                       writeString(key + L"\\DefaultIcon", nullptr, exe + L",0") &&
                       writeString(key + L"\\shell\\open\\command", nullptr, command);
  if (!written) OutputDebugStringW(L"vox:// 的登錄寫不進去\n");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
