#include "text.h"

#include <atomic>
#include <iterator>
#include <optional>

#import "common.h"
#import "files.h"

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
  NSString* preferred = NSLocale.preferredLanguages.firstObject ?: @"en";
  if (![preferred hasPrefix:@"zh"]) return Lang::En;
  return [preferred hasPrefix:@"zh-Hans"] || [preferred hasSuffix:@"-CN"] || [preferred hasSuffix:@"-SG"] ? Lang::ZhCn : Lang::ZhTw;
}

NSString* settingPath() { return appDataPath(@"language"); }

struct State {
  std::atomic<bool> chosen;
  std::atomic<Lang> active;
};

State& state() {
  static State current = [] {
    NSString* saved = readTextFile(settingPath());
    auto chosen = saved ? langOf(wide(saved)) : std::nullopt;
    return State{chosen.has_value(), chosen.value_or(systemLang())};
  }();
  return current;
}

}

std::wstring languageId() { return kIds[int(state().active.load())]; }

std::wstring languageSetting() { return state().chosen ? languageId() : std::wstring(kSystemLanguage); }

bool chooseLanguage(const std::wstring& setting) {
  if (setting == kSystemLanguage) {
    [NSFileManager.defaultManager removeItemAtPath:settingPath() error:nil];
    state().chosen = false;
    state().active = systemLang();
    return true;
  }
  auto lang = langOf(setting);
  if (!lang) return false;
  writeTextFile(settingPath(), ns(setting));
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
  swprintf(text, 16, L"%08lX", value);
  return text;
}
