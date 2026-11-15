#include "text.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <optional>

#include "files.h"

namespace {

enum class Lang { ZhTw, ZhCn, En };

constexpr const wchar_t* kTexts[][3] = {
#define VOX_TEXT_ENTRY(id, zhTw, zhCn, en) {zhTw, zhCn, en},
  VOX_TEXTS(VOX_TEXT_ENTRY)
#undef VOX_TEXT_ENTRY
};

constexpr const wchar_t* kIds[] = {L"zh-TW", L"zh-CN", L"en"};

std::optional<Lang> langOf(const std::wstring& id) {
  for (int i = 0; i < int(std::size(kIds)); ++i)
    if (id == kIds[i]) return Lang(i);
  return std::nullopt;
}

Lang systemLang() {
  const LANGID id = GetUserDefaultUILanguage();
  if (PRIMARYLANGID(id) != LANG_CHINESE) return Lang::En;
  const WORD sub = SUBLANGID(id);
  return sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE ? Lang::ZhCn : Lang::ZhTw;
}

std::wstring settingPath() {
  PWSTR base = nullptr;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base);
  std::wstring path = std::wstring(base) + L"\\" APP_NAME L"\\language";
  CoTaskMemFree(base);
  return path;
}

struct State {
  std::atomic<bool> chosen;
  std::atomic<Lang> active;
};

State& state() {
  static State current = [] {
    auto saved = readUtf8File(settingPath());
    auto chosen = saved ? langOf(*saved) : std::nullopt;
    return State{chosen.has_value(), chosen.value_or(systemLang())};
  }();
  return current;
}

}

std::wstring languageId() { return kIds[int(state().active.load())]; }

std::wstring languageSetting() { return state().chosen ? languageId() : std::wstring(kSystemLanguage); }

bool chooseLanguage(const std::wstring& setting) {
  if (setting == kSystemLanguage) {
    DeleteFileW(settingPath().c_str());
    state().chosen = false;
    state().active = systemLang();
    return true;
  }
  auto lang = langOf(setting);
  if (!lang) return false;
  appDataDir();
  writeUtf8File(settingPath(), setting);
  state().chosen = true;
  state().active = *lang;
  return true;
}

const wchar_t* tr(Text id) { return kTexts[int(id)][int(state().active.load())]; }

std::wstring tr(Text id, std::initializer_list<std::wstring> args) {
  std::wstring text = tr(id);
  int index = 0;
  for (const std::wstring& arg : args) {
    const std::wstring mark = L"{" + std::to_wstring(index++) + L"}";
    for (size_t at = 0; (at = text.find(mark, at)) != std::wstring::npos; at += arg.size()) text.replace(at, mark.size(), arg);
  }
  return text;
}

std::wstring hexText(unsigned long value) {
  wchar_t text[16];
  swprintf_s(text, L"%08lX", value);
  return text;
}
