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
  X(TrayOpen, L"開啟 {0}", L"打开 {0}", L"Open {0}") \
  X(TrayLyrics, L"桌面歌詞", L"桌面歌词", L"Desktop lyrics") \
  X(TrayLockLyrics, L"鎖定桌面歌詞", L"锁定桌面歌词", L"Lock desktop lyrics") \
  X(TrayQuit, L"結束 {0}", L"退出 {0}", L"Quit {0}") \
  X(TrayHintTitle, L"{0} 還在背景執行", L"{0} 仍在后台运行", L"{0} is still running") \
  X(TrayHintBody, L"按系統匣的圖示可以開啟視窗，按右鍵可以結束。", L"点击托盘图标可以打开窗口，右键点击可以退出。", \
    L"Click the tray icon to open the window, or right-click it to quit.") \
  X(LoginTitle, L"登入 {0}", L"登录 {0}", L"Sign in to {0}") \
  X(LoginWindowFailed, L"登入視窗開不起來（0x{0}）", L"登录窗口打不开（0x{0}）", L"The sign-in window couldn't open (0x{0})") \
  X(StartFailed, L"{0} 開不起來：{1}（0x{2}）", L"{0} 无法启动：{1}（0x{2}）", L"{0} couldn't start: {1} (0x{2})") \
  X(StepReadPlayerScript, L"讀不到嵌入播放器的腳本", L"读不到嵌入播放器的脚本", L"the embedded player script is missing") \
  X(StepParseEffects, L"effectsGraph.js 格式不對", L"effectsGraph.js 格式不对", L"effectsGraph.js is malformed") \
  X(StepStartWebView, L"WebView2 啟動失敗", L"WebView2 启动失败", L"WebView2 failed to start") \
  X(StepCreateWebView, L"WebView2 視窗建立失敗", L"WebView2 窗口创建失败", L"the WebView2 window couldn't be created") \
  X(StepInjectScript, L"嵌入播放器的腳本注入失敗", L"嵌入播放器的脚本注入失败", L"the embedded player script couldn't be injected") \
  X(CantRead, L"讀不到 {0}，錯誤碼 {1}", L"无法读取 {0}，错误码 {1}", L"Couldn't read {0} (error {1})") \
  X(CantWrite, L"寫不了 {0}，錯誤碼 {1}", L"无法写入 {0}，错误码 {1}", L"Couldn't write {0} (error {1})") \
  X(CantCreate, L"建不了 {0}，錯誤碼 {1}", L"无法创建 {0}，错误码 {1}", L"Couldn't create {0} (error {1})") \
  X(CantDelete, L"刪不掉 {0}，錯誤碼 {1}", L"无法删除 {0}，错误码 {1}", L"Couldn't delete {0} (error {1})") \
  X(CantMove, L"搬不動 {0}，錯誤碼 {1}", L"无法移动 {0}，错误码 {1}", L"Couldn't move {0} (error {1})") \
  X(CantReplace, L"換不了 {0}，錯誤碼 {1}", L"无法替换 {0}，错误码 {1}", L"Couldn't replace {0} (error {1})") \
  X(CantCopy, L"複製不了 {0}，錯誤碼 {1}", L"无法复制 {0}，错误码 {1}", L"Couldn't copy {0} (error {1})") \
  X(CantStart, L"啟動不了 {0}，錯誤碼 {1}", L"无法启动 {0}，错误码 {1}", L"Couldn't start {0} (error {1})") \
  X(CantWriteRegistry, L"寫不了登錄檔 {0}，錯誤碼 {1}", L"无法写入注册表 {0}，错误码 {1}", L"Couldn't write registry key {0} (error {1})") \
  X(CantDeleteRegistry, L"刪不掉登錄檔 {0}，錯誤碼 {1}", L"无法删除注册表 {0}，错误码 {1}", L"Couldn't delete registry key {0} (error {1})") \
  X(CantCreateShortcut, L"建不了捷徑 {0}，錯誤碼 {1}", L"无法创建快捷方式 {0}，错误码 {1}", L"Couldn't create shortcut {0} (error {1})") \
  X(RunningNotFound, L"找不到執行中的 {0}，錯誤碼 {1}", L"找不到正在运行的 {0}，错误码 {1}", L"Couldn't find the running {0} (error {1})") \
  X(CantClose, L"{0} 關不掉，請先關閉 {0} 再試一次", L"{0} 无法关闭，请先手动关闭 {0} 后重试", L"{0} didn't close. Close {0} and try again.") \
  X(UnzipStart, L"無法啟動解壓縮，錯誤碼 {0}", L"无法启动解压，错误码 {0}", L"Couldn't start extraction (error {0})") \
  X(UnzipFailed, L"解壓縮失敗，結束碼 {0}", L"解压失败，退出码 {0}", L"Extraction failed (exit code {0})") \
  X(FileOpFailed, L"檔案操作失敗，錯誤碼 {0}", L"文件操作失败，错误码 {0}", L"A file operation failed (error {0})") \
  X(SetupNoVersion, L"安裝檔裡少了版本資訊", L"安装包里缺少版本信息", L"The installer is missing its version information") \
  X(SetupBadVersion, L"安裝檔的版本號不對：{0}", L"安装包的版本号不对：{0}", L"The installer has an invalid version: {0}") \
  X(NoPermission, L"沒有權限寫入 {0}，請選別的位置", L"没有权限写入 {0}，请选择其他位置", L"No permission to write to {0}. Choose another location.") \
  X(InstallLocation, L"安裝位置：\n{0}", L"安装位置：\n{0}", L"Install location:\n{0}") \
  X(PickFolderTitle, L"選擇要把 {0} 裝在哪個資料夾", L"选择把 {0} 安装到哪个文件夹", L"Choose the folder to install {0} in") \
  X(SetupTitle, L"安裝 {0}", L"安装 {0}", L"Install {0}") \
  X(ChangeLocation, L"變更位置", L"更改位置", L"Change location") \
  X(Install, L"安裝", L"安装", L"Install") \
  X(Installing, L"正在安裝 {0}", L"正在安装 {0}", L"Installing {0}") \
  X(InstallFailed, L"{0} 安裝失敗", L"{0} 安装失败", L"{0} couldn't be installed") \
  X(UninstallAsk, L"要解除安裝 {0} 嗎？", L"要卸载 {0} 吗？", L"Uninstall {0}?") \
  X(UninstallKeepNote, L"沒勾下面那格的話，收藏、歌單、播放紀錄和登入資料會留在這台電腦，重新安裝後還在。", \
    L"不勾选下面的选项，收藏、歌单、播放记录和登录信息会保留在这台电脑上，重新安装后还在。", \
    L"Unless you tick the box below, your favorites, playlists, play history and sign-ins stay on this computer and return when you reinstall.") \
  X(UninstallEverything, L"一併刪除收藏、歌單、播放紀錄、登入資料和伴唱快取", L"同时删除收藏、歌单、播放记录、登录信息和伴唱缓存", \
    L"Also delete favorites, playlists, play history, sign-ins and the karaoke cache") \
  X(Uninstall, L"解除安裝", L"卸载", L"Uninstall") \
  X(UninstallFailed, L"{0} 解除安裝失敗", L"{0} 卸载失败", L"{0} couldn't be uninstalled") \
  X(NotRemoved, L"{0} 沒有移除", L"{0} 没有卸载", L"{0} wasn't removed") \
  X(NotInstallFolder, L"{0} 不是 {1} 的安裝資料夾", L"{0} 不是 {1} 的安装文件夹", L"{0} isn't a {1} install folder") \
  X(PartlyRemoved, L"{0} 沒有完全移除", L"{0} 没有完全卸载", L"{0} wasn't fully removed") \
  X(Uninstalled, L"{0} 已解除安裝", L"{0} 已卸载", L"{0} was uninstalled") \
  X(UninstalledEverything, L"程式和收藏、歌單、播放紀錄、登入資料、伴唱快取都已經刪除。", L"程序和收藏、歌单、播放记录、登录信息、伴唱缓存都已删除。", \
    L"The app and your favorites, playlists, play history, sign-ins and karaoke cache were deleted.") \
  X(UninstalledKept, L"收藏、歌單、播放紀錄和登入資料還留在這台電腦，重新安裝後會回來。", L"收藏、歌单、播放记录和登录信息仍保留在这台电脑上，重新安装后会恢复。", \
    L"Your favorites, playlists, play history and sign-ins are still on this computer and return when you reinstall.") \
  X(CantOpen, L"{0} 開不起來", L"{0} 无法启动", L"{0} couldn't start") \
  X(InstallIncomplete, L"安裝不完整，請重新下載安裝程式安裝", L"安装不完整，请重新下载安装程序并安装", \
    L"The installation is incomplete. Download the installer and install again.")

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
