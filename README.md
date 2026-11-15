# Vox

Windows 上的桌面 KTV 播放器。歌來自 YouTube Music 和 Spotify，歌詞逐字跟著唱，還能去人聲、升降調。

Vox 是用 C++ 寫的小程式，畫面交給 Windows 內建的 WebView2，所以安裝檔不用帶一整份瀏覽器。

## 功能

- **點歌**：搜尋 YouTube 或 Spotify，排進待播清單，可以插播、置頂；全部播完會自動接相似的歌。YouTube 會同時找 YouTube Music 的曲庫和一般影片；打「歌名/歌手」可以指定歌手或上傳者，符合的排在最前面。
- **逐字歌詞**：唱到哪個字就亮到哪個字。字體、顏色、邊框、行數、橫書直書都能調。
- **伴唱**：在自己的電腦上把人聲分離出來，人聲音量可以從 0 調到 100。目前只支援 YouTube 的歌。
- **升降調與音效**：逐首記住調了幾個半音；內建等化器和殘響。
- **桌面歌詞**：一條浮在所有視窗上面的歌詞，可以鎖定，滑鼠會穿過去。
- **背景播放**：關閉視窗會收進系統匣繼續播，系統匣圖示可以暫停、切歌、開關桌面歌詞和結束。設定裡可以改成關閉視窗就結束。
- **收藏、歌單、播放歷史**：存在這台電腦上；登入後也能用 YouTube、Spotify 帳號裡的歌單。
- **給其他程式用**：`vox://` 連結可以叫它播歌，本機介面可以讀到正在播什麼、唱到哪一句。
- **語言**：繁體中文、简体中文、English，預設跟著系統。歌詞的繁簡、歌名和歌單的顯示語言、預設字型也跟著換。

## 安裝

下載 [VoxSetup.exe](https://vox.lichitdev.com/latest/VoxSetup.exe) 執行。

- 需要 Windows 10 或 11（64 位元）和 WebView2 Runtime。Windows 11 內建，Windows 10 大多也已經有，沒有的話到[微軟的下載頁](https://developer.microsoft.com/microsoft-edge/webview2/)裝。
- 不需要系統管理員權限。預設裝在 `%LOCALAPPDATA%\Programs\Vox`，安裝時可以改位置。
- 之後會自動更新：開 Vox 時在背景下載新版，下次開就換成新版。新版開不起來會自動換回舊版。
- 解除安裝在 Windows 的「應用程式」裡。預設會留下收藏、歌單、播放紀錄和登入資料，勾選「一併刪除」才會清掉。

## 登入是選用的

不登入也能點歌、唱歌。右上角的帳號按鈕可以登入 YouTube 或 Spotify：

- **YouTube**：可以用帳號裡的歌單；有 Premium 就不會播廣告。
- **Spotify**：沒登入只能試聽 30 秒，登入後可以聽完整首歌、用帳號裡的歌單。

登入是在 Vox 開的視窗裡直接連到 Google 或 Spotify，登入資料只存在這台電腦。Vox 不會把登入資料或聽歌紀錄傳給作者，它連到 `vox.lichitdev.com` 只為了下載更新。

## 給其他程式用

### 連結

```
vox://play/<YouTube 影片 id>
```

Vox 沒開的話會開起來播這首，已經開著就直接切歌。

### 本機介面

Vox 開著時會在 `http://127.0.0.1:8690` 提供介面，只有這台電腦上的程式連得到，網頁發出的請求一律拒絕。回應都是 JSON，失敗時是 `{"error": "原因"}`。

| 方法 | 路徑 | 做什麼 |
|---|---|---|
| GET | `/api/v1/state` | 目前的歌、播放狀態、唱到的那句、待播清單 |
| GET | `/api/v1/lyrics` | 整首歌詞，含每個字的時間 |
| GET | `/api/v1/queue` | 待播清單 |
| GET | `/api/v1/events` | 即時推播（SSE），事件有 `track`、`playback`、`lyrics`、`line`、`queue` |
| POST | `/api/v1/command` | 控制播放 |
| POST | `/api/v1/queue` | 點歌 |

`/api/v1/events` 一連上會先送一次每種事件的現況，之後只在有變化時推。

控制播放：

```
curl -X POST http://127.0.0.1:8690/api/v1/command -d "{\"action\":\"pause\"}"
```

`action` 可以是 `play`、`pause`、`toggle`、`next`、`restart`、`seek`（加 `position`，毫秒）、`volume`（加 `value`，0 到 100）。

點歌，用歌名搜尋或直接給 id：

```
curl -X POST http://127.0.0.1:8690/api/v1/queue -d "{\"query\":\"周杰倫 晴天\"}"
curl -X POST http://127.0.0.1:8690/api/v1/queue -d "{\"source\":\"ytm\",\"id\":\"QoXDQa9L12A\",\"insert\":true}"
```

`source` 是 `ytm` 或 `spotify`；`insert` 設成 `true` 會排到下一首。`query` 也可以寫成「歌名/歌手」，會挑歌手或上傳者符合的那一首。

`playback` 的內容：

```json
{ "state": "playing", "position": 61234, "at": 1794676502377, "duration": 215000,
  "volume": 60, "keyShift": 0, "karaoke": false, "lyricOffset": 0 }
```

- `state` 是 `idle`、`loading`、`playing`、`paused` 其中一個。
- 播放中不會每秒推一次位置。現在播到第幾毫秒要自己算：`position + (現在的時間 − at)`，`at` 是 Unix 毫秒。
- 歌詞要對的時間是播放位置加上 `lyricOffset`。

## 從原始碼建置

需要 Visual Studio（含「使用 C++ 的桌面開發」）、CMake 3.25 以上。

```
powershell -ExecutionPolicy Bypass -File scripts\deps.ps1
cmake -S . -B build
cmake --build build --config Release --target vox vox-separate
build\Release\Vox.exe
```

`scripts\deps.ps1` 會把 WebView2 SDK、ONNX Runtime、Opus、cpp-httplib 等相依元件和伴唱模型下載到 `deps\`。開發時加 `-DVOX_DEVTOOLS=ON` 可以開 DevTools。

程式分三塊：

- `src\`：視窗、WebView2、帳號、桌面歌詞、本機介面、更新。`src\launcher\` 是啟動器，也負責安裝和解除安裝。
- `separate\`：人聲分離，獨立的執行檔。
- `ui\`：畫面，沒有建置步驟的 HTML、CSS、JavaScript。

### 翻譯

畫面上的字在 `ui\locales\*.json`，原生視窗（桌面歌詞、安裝程式）的字在 `src\text.h`。各地用當地慣用的說法，不是逐字轉換。改完跑一次檢查，三種語言的句子和參數要對得上：

```
python scripts\checkLocales.py
```

字型清單在 `ui\js\fonts.json`。簡體字齊全的字型要標 `"simplified": true`，简体中文介面只會列出這些。

### 伴唱模型

`scripts\deps.ps1` 下載的模型是 Demucs 的 `hdemucs_mmi` 轉成 ONNX 的版本，權重存成半精度，一次處理 431 × 1024 個取樣點，大約 10 秒。要自己轉一份：

```
pip install demucs onnx onnxruntime
python scripts\exportModel.py core.onnx 431
python scripts\halfWeights.py core.onnx deps\models\demucs-hdemucs_mmi-10s.onnx
```

兩支腳本各會印出轉換前後的最大誤差。發布的模型是用 demucs 4.1.0、torch 2.15.0、onnx 1.23.2 轉的。

### 發版

更新檔有簽章，Vox 只接受用對應私鑰簽過的版本。私鑰不放在這個 repo 裡。

```
powershell -File scripts\release.ps1 -Key <私鑰檔> -Bucket <R2 bucket>
```

這會建置、列出每個檔案的雜湊、簽章、組出 `VoxSetup.exe`，再用 `wrangler` 傳到 Cloudflare R2。不加 `-Bucket` 就只產生上傳資料夾，裡面的 `latest\` 和 `files\` 照原樣放到任何靜態主機都可以。

版本號是發版當天的日期，像 `2026.11.16`，同一天再發一次就是 `2026.11.16.1`，腳本自己算。更新網址改 `CMakeLists.txt` 的 `VOX_UPDATE_URL`。

## 資料放在哪裡

| 內容 | 位置 |
|---|---|
| 收藏、歌單、播放紀錄、設定 | `%LOCALAPPDATA%\Vox` |
| 登入資料 | `%LOCALAPPDATA%\Vox\WebView2` |
| 分離好的伴奏 | `%LOCALAPPDATA%\Vox\stems` |

## 授權

Vox 以 [GPL-3.0](LICENSE) 釋出。

用到的第三方元件有 WebView2、ONNX Runtime、Opus、libopusenc、PocketFFT、cpp-httplib、SoundTouch、opencc-js，各自的授權條款全文在安裝資料夾的 `THIRD_PARTY_NOTICES.txt`。人聲分離用的是 Meta Research 的 [Demucs](https://github.com/facebookresearch/demucs) 模型，伴唱的音訊由 [yt-dlp](https://github.com/yt-dlp/yt-dlp) 下載。

Vox 是獨立的專案，跟 YouTube、Google、Spotify 沒有關係。
