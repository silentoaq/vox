#pragma once
#include <initializer_list>
#include <string>

#define VOX_TEXTS(X) \
  X(LyricsUnlock, L"解鎖", L"解锁", L"Unlock") \
  X(LyricsRestart, L"重唱", L"重唱", L"Restart") \
  X(LyricsPause, L"暫停", L"暂停", L"Pause") \
  X(LyricsPlay, L"播放", L"播放", L"Play") \
  X(LyricsSkip, L"切歌", L"切歌", L"Skip") \
  X(LyricsEarlier, L"提前", L"提前", L"Earlier") \
  X(LyricsLater, L"延後", L"延后", L"Later") \
  X(LyricsColor, L"顏色", L"颜色", L"Color") \
  X(LyricsDynamic, L"動態", L"动态", L"Dynamic") \
  X(LyricsWipe, L"刷色", L"刷色", L"Wipe") \
  X(LyricsLock, L"鎖定", L"锁定", L"Lock") \
  X(LoginTitle, L"登入 {0}", L"登录 {0}", L"Sign in to {0}") \
  X(LoginWindowFailed, L"登入視窗開不起來（0x{0}）", L"登录窗口打不开（0x{0}）", L"The sign-in window couldn't open (0x{0})") \
  X(StartFailed, L"{0} 開不起來：{1}（0x{2}）", L"{0} 无法启动：{1}（0x{2}）", L"{0} couldn't start: {1} (0x{2})") \
  X(StepReadPlayerScript, L"讀不到嵌入播放器的腳本", L"读不到嵌入播放器的脚本", L"the embedded player script is missing") \
  X(StepParseEffects, L"effectsGraph.js 格式不對", L"effectsGraph.js 格式不对", L"effectsGraph.js is malformed") \
  X(StepStartWebView, L"WebView2 啟動失敗", L"WebView2 启动失败", L"WebView2 failed to start") \
  X(StepCreateWebView, L"WebView2 視窗建立失敗", L"WebView2 窗口创建失败", L"the WebView2 window couldn't be created") \
  X(StepInjectScript, L"嵌入播放器的腳本注入失敗", L"嵌入播放器的脚本注入失败", L"the embedded player script couldn't be injected") \
  X(UnzipStart, L"無法啟動解壓縮，錯誤碼 {0}", L"无法启动解压，错误码 {0}", L"Couldn't start extraction (error {0})") \
  X(UnzipFailed, L"解壓縮失敗，結束碼 {0}", L"解压失败，退出码 {0}", L"Extraction failed (exit code {0})")

enum class Text {
#define VOX_TEXT_ID(id, zhTw, zhCn, en) id,
  VOX_TEXTS(VOX_TEXT_ID)
#undef VOX_TEXT_ID
};

inline constexpr wchar_t kSystemLanguage[] = L"system";

std::wstring languageId();
std::wstring languageSetting();
bool chooseLanguage(const std::wstring& setting);
const wchar_t* tr(Text id);
std::wstring tr(Text id, std::initializer_list<std::wstring> args);
std::wstring hexText(unsigned long value);
