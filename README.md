# Vox

Windows 上的桌面 KTV 播放器。歌來自 YouTube Music 和 Spotify，歌詞逐字跟著唱，還能去人聲、升降調。

Vox 是用 C++ 寫的小程式，畫面交給 Windows 內建的 WebView2，所以安裝檔不用帶一整份瀏覽器。

## 功能

- **點歌**：搜尋 YouTube 或 Spotify，排進待播清單，可以插播、置頂；全部播完會自動接相似的歌。YouTube 會同時找 YouTube Music 的曲庫和一般影片；打「歌名/歌手」可以指定歌手或上傳者，符合的排在最前面。
- **逐字歌詞**：唱到哪個字就亮到哪個字。字體、顏色、邊框、行數、橫書直書都能調。
- **伴唱**：在自己的電腦上把人聲分離出來，人聲音量可以從 0 調到 100。目前只支援 YouTube 的歌。開著時約多用 1 顆 CPU 核心、1.5 GB 記憶體。
- **升降調與音效**：逐首記住調了幾個半音；內建等化器和殘響。
- **桌面歌詞**：一條浮在所有視窗上面的歌詞，可以鎖定，滑鼠會穿過去。
- **收藏、歌單、播放歷史**：存在這台電腦上；登入後也能用 YouTube、Spotify 帳號裡的歌單。
- **給其他程式用**：`vox://` 連結可以叫它點歌、播放、暫停；正在播什麼會報給 Windows 的媒體控制，逐字歌詞存成檔案。
- **語言**：繁體中文、简体中文、English，預設跟著系統。歌詞的繁簡、歌名和歌單的顯示語言、預設字型也跟著換。

## 安裝

下載 [VoxSetup.exe](https://vox.lichitdev.com/latest/VoxSetup.exe) 執行。

- 需要 Windows 10 或 11（64 位元）和 WebView2 Runtime。Windows 11 內建，Windows 10 大多也已經有，沒有的話到[微軟的下載頁](https://developer.microsoft.com/microsoft-edge/webview2/)裝。
- 不需要系統管理員權限。預設裝在 `%LOCALAPPDATA%\Programs\Vox`，安裝時可以改位置。
- 之後會自動更新：開 Vox 時在背景下載新版的安裝程式，下次開 Vox 會先裝好新版再打開。
- 解除安裝在 Windows 的「應用程式」裡。最後會問要不要一併刪除收藏、歌單、播放紀錄和登入資料，選「否」就保留。

## 登入是選用的

不登入也能點歌、唱歌。右上角的帳號按鈕可以登入 YouTube 或 Spotify：

- **YouTube**：可以用帳號裡的歌單；有 Premium 就不會播廣告。
- **Spotify**：沒登入只能試聽 30 秒，登入後可以聽完整首歌、用帳號裡的歌單。

登入是在 Vox 開的視窗裡直接連到 Google 或 Spotify，登入資料只存在這台電腦。Vox 不會把登入資料或聽歌紀錄傳給作者，它連到 `vox.lichitdev.com` 只為了下載更新。

## 給其他程式用

叫 Vox 做事用 `vox://` 連結；要知道正在播什麼，讀 Windows 的媒體控制和 Vox 存的歌詞檔。

### 連結

```
vox://open
vox://queue?query=晴天/周杰倫
vox://queue?query=晴天/周杰倫&source=spotify&insert=true
vox://queue?source=ytm&id=QoXDQa9L12A
vox://command?action=pause
vox://dequeue?query=晴天/周杰倫
vox://dequeue?source=ytm&id=QoXDQa9L12A
```

- `open`：打開 Vox，已經開著就把視窗叫到前面。
- `queue`：把歌加進待播清單，沒在播就直接播。`query` 給歌名，可以寫成「歌名/歌手」，加入搜尋結果的第一首；加上 `source` 就只在那個平台搜，可以是 `ytm` 或 `spotify`，不給就用 Vox 搜尋框目前選的平台。知道是哪一首的話，也可以給 `source` 加 `id`。加 `insert=true` 會排到下一首。
- `command`：`action` 可以是 `play`、`pause`、`next`。
- `dequeue`：從待播清單移除一首。給 `query` 會移除用同一段 `query` 點的那首，也可以給 `source` 加 `id`。有好幾首符合就移除最前面那首。

中文可以直接寫，也可以做網址編碼。除了 `open`，Vox 沒開的話會在背景開起來，不會搶走目前視窗的焦點。連結照收到的順序一個一個做。做不到時 Vox 會跳提示，但要的狀態本來就是那樣的不跳，例如沒在播時暫停、要移除的歌已經不在清單裡。

安裝時會在 `HKEY_CURRENT_USER\Software\Classes\vox` 登記，從 `HKEY_CLASSES_ROOT\vox\shell\open\command` 也讀得到。

### 正在播什麼

Vox 開著時會在 Windows 的媒體控制登記一個工作階段，`SourceAppUserModelId` 是 `Vox.exe`。用 `GlobalSystemMediaTransportControlsSessionManager` 讀：

- 歌名、歌手、專輯、封面。
- 播放狀態：`Playing`、`Paused`；換歌、拖進度條、緩衝時是 `Changing`；沒有歌時是 `Stopped`。
- 時間軸：`Position` 是 `LastUpdatedTime` 那一刻播到的位置，`EndTime` 是歌曲長度。播放中不會一直更新，現在播到哪要自己算：`Position + (現在 − LastUpdatedTime)`。換歌、暫停、拖進度條時會立刻更新。

鍵盤的媒體鍵和 Windows 的媒體控制面板也能播放、暫停、下一首。

### 歌詞檔

正在播的歌的逐字歌詞存在 `%LOCALAPPDATA%\Vox\lyrics.json`，換歌、歌詞載好、在 Vox 裡調歌詞快慢時會整個重寫：

```json
{ "title": "晴天", "artist": "周杰倫", "source": "ytm", "id": "…", "state": "ready",
  "lines": [ { "start": 21350, "text": "故事的小黃花",
               "chars": [ { "start": 21350, "end": 21700, "text": "故" }, … ] } ] }
```

- `title`、`artist` 跟媒體控制裡的一樣，用來確認是同一首。換歌時會先寫好這個檔，媒體控制才換成新歌。
- `requestedAs`：這首是用 `vox://queue?query=` 點進來的話，是那段 `query`，一字不改；不是的話沒有這個欄位。
- `cover`：封面的 https 網址，跟 Vox 畫面上的同一張；沒有封面就沒有這個欄位。
- `state` 是 `ready`、`loading`、`error`、`none` 其中一個，不是 `ready` 時 `lines` 是空的。
- 時間都是毫秒，已經對好媒體控制的播放位置，直接拿位置來比就好。唱到哪一句：找 `start` 不超過播放位置的最後一行；唱到哪個字：看每個字的 `start` 和 `end`。

## 從原始碼建置

需要 Visual Studio（含「使用 C++ 的桌面開發」）、CMake 3.25 以上。

```
powershell -ExecutionPolicy Bypass -File scripts\deps.ps1
cmake -S . -B build
cmake --build build --config Release --target vox vox-separate
build\Release\Vox.exe
```

`scripts\deps.ps1` 會把 WebView2 SDK、ONNX Runtime、Opus、NSIS 等相依元件和伴唱模型下載到 `deps\`。開發時加 `-DVOX_DEVTOOLS=ON` 可以開 DevTools。

程式分四塊：

- `src\`：視窗、WebView2、帳號、桌面歌詞、媒體控制、更新。
- `separate\`：人聲分離，獨立的執行檔。
- `ui\`：畫面，沒有建置步驟的 HTML、CSS、JavaScript。升降調在瀏覽器的聲音執行緒上跑，建置時會把 SoundTouch 和 `ui\js\pitchWorklet.js` 合成 `vox-pitch-worklet.js`。
- `scripts\installer.nsi`：NSIS 安裝程式，也負責解除安裝。

### 翻譯

畫面上的字在 `ui\locales\*.json`，桌面歌詞這類原生視窗的字在 `src\text.h`，安裝程式的字在 `scripts\installer.nsi`。各地用當地慣用的說法，不是逐字轉換。改完跑一次檢查，三種語言的句子和參數要對得上：

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

這會建置、用 NSIS 打包成 `VoxSetup.exe`、把安裝程式的雜湊寫進 `update.json` 並簽章，再用 `wrangler` 傳到 Cloudflare R2，最後打標籤、建 GitHub Release。不加 `-Bucket` 就只產生上傳資料夾，裡面的 `latest\` 和 `files\` 照原樣放到任何靜態主機都可以。

版本號是發版當天的日期，像 `2026.11.16`，同一天再發一次就是 `2026.11.16.1`，腳本照 git 標籤自己算。更新網址改 `CMakeLists.txt` 的 `VOX_UPDATE_URL`。

### 下載頁

`vox.lichitdev.com` 的首頁在 `site\`，是一支 Cloudflare Worker：首頁和圖是靜態檔案，`/latest/VoxSetup.exe` 從 R2 讀出來回傳，不讓 CDN 快取、下載的檔名固定。改完在 `site\` 跑：

```
npx wrangler deploy
```

## 資料放在哪裡

| 內容 | 位置 |
|---|---|
| 收藏、歌單、播放紀錄、設定 | `%LOCALAPPDATA%\Vox` |
| 登入資料 | `%LOCALAPPDATA%\Vox\WebView2` |
| 分離好的伴奏 | `%LOCALAPPDATA%\Vox\stems` |
| 下載好、還沒裝的新版 | `%LOCALAPPDATA%\Vox\update` |

## 授權

Vox 以 [GPL-3.0](LICENSE) 釋出。

用到的第三方元件有 WebView2、ONNX Runtime、Opus、libopusenc、PocketFFT、SoundTouch、opencc-js，各自的授權條款全文在安裝資料夾的 `THIRD_PARTY_NOTICES.txt`。人聲分離用的是 Meta Research 的 [Demucs](https://github.com/facebookresearch/demucs) 模型，伴唱的音訊由 [yt-dlp](https://github.com/yt-dlp/yt-dlp) 下載。

Vox 是獨立的專案，跟 YouTube、Google、Spotify 沒有關係。
