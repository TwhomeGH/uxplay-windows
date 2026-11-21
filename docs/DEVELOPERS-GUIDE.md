# 開發者指南：本地建置與除錯

> 這份是本地開發的單一入口。Release 安裝包流程見
> [BUILDING.md](./BUILDING.md)；變更歷史用 `changelogkit` 維護。

## 文件導覽

| 想知道什麼 | 去哪裡 |
| --- | --- |
| 先跑起來、建置、除錯 | 本文件〈最快路徑〉與〈執行與除錯〉 |
| 程式碼模組與閱讀順序 | [API-GUIDE.md](./API-GUIDE.md) |
| 類別／函式／參數 | `_site/api/index.html`（由 Doxygen 產生） |
| 翻譯規範與 fallback | [translations/README.md](../translations/README.md) |
| 最近改了什麼 | [ChangeHistory.md](./ChangeHistory.md) |
| 待辦與優先順序 | [TODO.md](../TODO.md) |
| Release 產物 | [BUILDING.md](./BUILDING.md) |

## VS Code 工作

`.vscode/tasks.json` 提供：

| 任務 | 用途 |
| --- | --- |
| `build-dev` | `scripts/dev_build.sh` |
| `i18n: update catalog` | `lupdate` 更新 `.ts` |
| `i18n: check` | 嚴格檢查翻譯（需要 lupdate） |
| `i18n: check catalog only` | 沒有 lupdate 時只驗 catalog |
| `docs: api` | 建置 Doxygen API 並開啟 |
| `diagnostics: viewer` | 開啟本機診斷檢視器 |

按 `Ctrl+Shift+B` 可直接跑 `build-dev`；其他任務從
`Terminal → Run Task...` 選取。F5 使用 `.vscode/launch.json` 的 gdb 設定。

## 最快路徑

```bash
# 1. 從開始功能表開「MSYS2 UCRT64」終端
cd /f/uxplay-windows

# 2. 建置（預設 RelWithDebInfo，輸出 build/dev/uxplay-windows.exe）
bash scripts/dev_build.sh

# 3. 啟動，並把 -d 傳給 UxPlay 引擎
bash scripts/dev_run.sh --engine-debug
```

App 是托盤程式：關閉視窗只會隱藏，要退出請用托盤選單的 **Quit**。

> 直接雙擊 `build/dev/uxplay-windows.exe` 也可用：`dev_build.sh` 會把
> UCRT64 DLL、`qwindows.dll`（`platforms/`）與 `qt.conf` 放進 build
> 目錄。若仍出現 Qt platform plugin 錯誤，改用 `dev_run.sh` 或
> `build/dev/run-dev.cmd`。

## 前置需求

- **MSYS2 UCRT64**（不要用 MINGW64；Qt6／GStreamer 開發檔在 UCRT64）。
- 安裝依賴：

  ```bash
  pacman -S --needed --noconfirm - < ucrt_x64_dependencies.txt
  ```

- **Bonjour SDK** 放在 repo 根目錄的 `Bonjour SDK/`（已被 `.gitignore`
  忽略）。內容需有 `Include/dns_sd.h` 與 `Lib/x64/dnssd.lib`。
- **Bonjour Service** 必須存在並執行。App 現在會檢查狀態並嘗試啟動；
  失敗時會提示用 `services.msc` 手動啟動。
- （選配）Doxygen 產生 API 參考：

  ```bash
  pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-doxygen
  ```

- （選配）BLE beacon 需要 Python 套件；本地 debug 沒 build
  `uxplay-bluetooth-beacon.exe` 也能跑，App 只會在 log 記一筆
  `uxplay-bluetooth-beacon.exe not found`，mDNS 發現不受影響。

> 這台機器實際的 MSYS2 prefix 可能是 `H:\FIX_VIAP\msys64\ucrt64`；
> 腳本用 `$MSYSTEM_PREFIX`，不要手動硬寫 `/ucrt64` 或 `C:\msys64`。

`scripts/dev_run.sh` 與 `build/dev/run-dev.cmd` 會先停止**同一個 dev
exe 路徑**的舊 instance，再啟動新 build，避免舊 process 鎖住檔案或連到
舊版本。這只比對 dev build 路徑，不會誤殺已安裝的正式版。

## API 文件

Doxygen 入口在 [API-GUIDE.md](./API-GUIDE.md)，設定在 `Doxyfile`。

```powershell
pwsh -NoProfile -File scripts/build_docs.ps1 -Open
```

輸出 `_site/api/index.html`，`_site/` 不進版控。VS Code 任務
`docs: api` 等同這個指令；沒有本機 Doxygen 時腳本會嘗試 Docker。

Doxygen 只讀 `///` 與 `/** */`；新增公開 API 時請補上用途、參數與回傳值。

### CI 與 API 文件

- `.github/workflows/docs.yml`：變更 `docs/**`、`Doxyfile` 或
  `scripts/build_docs.ps1` 時產生 API，並上傳 `api-reference` artifact。
- `.github/workflows/checks.yml`：PR／手動執行翻譯檢查，不安裝建置環境、
  不觸發 nightly。
- x64／arm64 build workflow：push `main` 時執行 `check_i18n.py`，並建置 nightly。
- nightly 已拆成 `build` 與 `publish` 兩個 job：建置產物先上傳 artifact，
  publish 才下載並更新 release；取消 build 不會動到既有 release。
- 文件 PR 不會觸發 nightly；只有 push `main` 才會更新既有 nightly release。

### Fork 的 Actions

這個工作區的 remote 是 `TwhomeGH/uxplay-windows`，GitHub 標記為 fork。
Fork 預設可能顯示 workflows 被停用（`disabled_fork`），需要到 Actions
分頁按 **Enable workflow**；這是 fork 保護，不是 workflow 寫錯。

本專案的 CI 沒有 `schedule` trigger，只有 `push`／`pull_request`／
`workflow_dispatch`，因此 GitHub 的「公開 repo 60 天無活動停用 scheduled
workflow」規則不適用。若之後 Actions 被停用，先看 workflow state：

```text
active
 disabled_fork
 disabled_inactivity
 disabled_manually
```

有開發工作要跑 CI 時再重新啟用即可；若不想消耗 nightly，先推分支開 PR，
不要直接 push `main`。

## 建置

```bash
bash scripts/dev_build.sh            # RelWithDebInfo
bash scripts/dev_build.sh --debug    # 全 Debug，符號完整但較慢
bash scripts/dev_build.sh --release  # Release
bash scripts/dev_build.sh --clean    # 先刪 build/dev 再 configure
```

`--clean` 若出現 `Device or resource busy`，代表有程式把 `build/dev`
當成目前目錄（執行中的 dev app，或開在該資料夾的終端）。關掉它，
或改用另一個 build dir：

```bash
BUILD_DIR=/f/uxplay-windows/build/clean bash scripts/dev_build.sh --clean
```

腳本會：

1. 檢查 `MSYSTEM=UCRT64` 與 UCRT64 toolchain。
2. 預設 `BONJOUR_SDK_HOME=<repo>/Bonjour SDK`。
3. 以 `build/dev` 為 build dir，帶入 `CMAKE_PREFIX_PATH`、OpenSSL、
   `NO_MARCH_NATIVE=ON` 與 `CMAKE_EXPORT_COMPILE_COMMANDS=ON`。
4. 建置後把 `stuff/newicon.ico` 複製成 `build/dev/resources/icon.ico`。

要改 build dir 時用環境變數：

```bash
BUILD_DIR=/f/uxplay-windows/build/mydev bash scripts/dev_build.sh
```

## 執行與除錯

```bash
bash scripts/dev_run.sh                  # 前景執行，終端可看 printf 輸出
bash scripts/dev_run.sh --engine-debug   # 額外傳 -d 給引擎，保留生命週期與錯誤資訊
bash scripts/dev_run.sh --engine-trace   # 連 packet／HTTP debug 也輸出（量很大）
bash scripts/dev_run.sh --detach         # 背景啟動後返回
```

`--engine-debug` 會過濾每包 RTP、`/feedback` keepalive 與 HTTP
連線細節；需要逐包追查時再加 `--engine-trace`。

從 Explorer／PowerShell 直接啟動時，用建置時產生的 wrapper：

```powershell
.\build\dev\run-dev.cmd
.\build\dev\run-dev.cmd --engine-debug
```

`dev_build.sh` 會把 `$MSYSTEM_PREFIX/bin`（含
`libgcc_s_seh-1.dll`、Qt6、GStreamer）寫進 `run-dev.cmd` 的 `PATH`。

### Pipeline stall 偵測（目前不自動重建）

stats tick 同時讀取「送進 pipeline 的輸入幀」與「實際 presented 的幀」：

- input 持續增加，但 presented frame age 超過 20 秒、且 streaming 超過
  20 秒、連續 3 次取樣：記錄 `[stats] pipeline_stall`、寫 diagnostics
  event 並顯示 tray 提示；**不自動重建 engine**，避免把根因
  （影片視窗／sink）掩蓋成反覆斷線。
- input 停止（例如 iPad 暫停或鎖定）不算 stall，避免誤判。
- 需要時由使用者按 `Rebuild AirPlay stream`。
- H.265 + D3D12 若反覆只送 1 frame，先把 renderer 改成 D3D11 比較。

### AirPlay soak 監測

在 iPad 已連上 AirPlay 時跑：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\airplay_soak.ps1 -Minutes 60 -IntervalSeconds 60
```

每 60 秒記錄 `uxplay-windows` 的 CPU、RSS、private bytes、threads、
handles 與 log 的 ERROR／WARN。輸出：

```text
build\diagnostics\airplay-soak-<timestamp>.csv
build\diagnostics\airplay-soak-<timestamp>.txt
```

CSV 可畫 RSS 趨勢；report 有首尾 RSS、CPU 平均／峰值、初始連線 context
與 log 事件。監測期間不要關掉 AirPlay 或讓 iPad 睡眠，否則會記錄到
斷線事件。

### 診斷包（Diagnostics）

診斷包的用途是**重現一次事件**（連線卡住、掉 FPS、engine error），
不是保存完整歷史。預設只匯出「上次 AirPlay 連線到現在」；若還沒
連過，預設最近 1 小時。

匯出流程：

- 主視窗 **Save Diagnostics** 或 tray 的 **Save Diagnostics** 會開區間
  對話框：
  - Since last AirPlay connection（預設）
  - Last 1 hour
  - Last 5 hours
  - Entire app session
  - Custom range
- 預設輸出**資料夾**，方便直接看 `summary.txt`；要分享給他人時才勾
  `Create .zip for sharing`。
- 既有實例：`uxplay-windows.exe --save-diagnostics` 使用預設區間與資料夾
  輸出。

輸出：

```text
%LOCALAPPDATA%\leapbtw\uxplay-windows\diagnostics\
  uxplay-diagnostics-<timestamp>\   ← 預設，直接可讀
  uxplay-diagnostics-<timestamp>.zip  ← 只有勾選時
```

日誌與診斷入口在 tray 的 `Logs & Diagnostics`，或主視窗 `Open Log`
按鈕（點擊會展開選單）：

- **Open Log**：用系統預設程式開啟。
- **Open Log With...**：Windows「開啟方式」，可選 VS Code 或其他工具。
- **Open Log Folder**：開啟日誌資料夾。
- **Open Diagnostics Folder**：開啟診斷包資料夾（不存在時會建立）。

### Diagnostics Window（即時）

除了匯出 bundle，主視窗的 **Diagnostics Window**、tray 的
**Diagnostics Window**，或 `uxplay-windows.exe --diagnostics` 可開啟
獨立、可縮放的即時視窗；關閉它不會停止 AirPlay。內容：

- **Overview**：連線狀態、client／device ID、連線數、session uptime、
  app 版本、OBS／USB 狀態、last error、settings／log／diagnostics 路徑、
  engine 參數。
- **Video**：codec、Output／Requested、decoder、renderer、FPS、frame
  interval、last frame age、presented／input frames。
- **Audio**：codec、format、reported latency，以及 audio RTP 封包
  統計：received／delivered、resend requests、empty resend responses、
  bytes。`empty resend` 持續增加代表 client 已被要求重送它不再保留的
  音訊，可能聽見短暫斷音。
- **Network**：client、device ID、連線數；RTP 細節仍以 bundle 為準。
- **Events**：最近 80 筆 diagnostics 事件。
- 底部 **Copy summary**／**Copy all** 可複製文字；**Save diagnostics**
  沿用目前的時間區間匯出流程。

音訊計數是 engine 的 audio RTP 執行緒看到的封包，不代表 audio sink
真的播放成功；要判斷是否真的掉音，需同時看 `empty resend` 是否持續
增加與 client／網路狀況。

### Runtime Debug（開發者原始狀態）

`uxplay-windows.exe --runtime-debug`、tray 的
**Logs & Diagnostics → Runtime Debug** 會開啟開發者用的唯讀視窗；
關閉它不會影響 streaming。內容不經過一般 log level 過濾，包含：

- 目前已套用的 engine arguments
- settings 中的 renderer／codec／virtual camera／adapter
- 實際 stream 尺寸與 requested 尺寸
- HUD 實際 decoder／renderer
- presented／input／decoded／overlay counters
- FPS、frame interval、frame age
- DXGI adapter 清單與 capability（h264／h265、software）
- 最近 200 筆 structured diagnostics events

可用 **Copy all** 或 **Save...** 匯出；這個視窗是排查 pipeline、
D3D11 adapter、decoder 與 virtual camera 狀態用，不是一般使用者的
diagnostics bundle。

內容包括 `summary.txt`（先看這個）、`timeline.txt`、`manifest.json`、
`events.jsonl`、`metrics.jsonl`、logs、`settings.redacted.json`、
`system.txt` 與 `README.txt`。`summary.txt` 會列出 build、client、
連線次數、engine error、低 FPS 事件與最近 40 筆事件。

保留量：

- 記憶體 ring buffer：20000 events、21600 metrics（1Hz 約 6 小時）。
- 日誌：8MB × 5 個 rotated 檔＋目前檔（約 48MB）。
- 影片統計每 5 秒寫一行 `[stats] fps=... interval_ms=...
  frame_age_ms=...`；連續 5 秒低於 30fps 會寫 `[stats] low_fps`。
- 診斷包：只保留最近 **5 組** folder／zip；每次匯出後自動刪除較舊的。
  不是每次啟動或連線都自動建包，只有按下 Save Diagnostics 才產生。

匯出時會 redact 裝置名稱、UDID、MAC、IPv4 與使用者路徑；client model
等技術欄位會保留。**不會自動上傳**；分享前請自行檢查。回報請使用
`.github/ISSUE_TEMPLATE/bug_report.yml` 並附上 zip。

### 診斷檢視器

本機唯讀檢視器，靈感來自 ReplyKit 的 `diagnostic_viewer.py`，但改成
讀我們的 bundle／日誌格式：

```powershell
# 自動開最新的 bundle（沒有 bundle 時可指定 log）
python scripts/diagnostic_viewer.py

# 指定 bundle 或目前 log
python scripts/diagnostic_viewer.py "$env:LOCALAPPDATA\leapbtw\uxplay-windows\diagnostics\uxplay-diagnostics-....zip"
python scripts/diagnostic_viewer.py "$env:LOCALAPPDATA\leapbtw\uxplay-windows\logs\uxplay-windows.log"
```

檢視器只綁 `127.0.0.1`，不依賴外部套件、不上傳。內容：

- Overview cards：events／errors／warnings／low_fps／connects／disconnects。
- 分類篩選、嚴重度、來源（event／log／timeline）、全文搜尋。
- 事件列可展開 `key=value` 欄位。
- 系統指標：CPU、working set 的時序圖。
- `summary.txt` 是 bundle 的快速入口；`timeline.txt` 是完整時間軸。

### 視訊編碼偏好（HEVC / H.264）

主視窗 `Video Codec` 預設 `H.264 / AVC`，可切換 `HEVC / H.265
(experimental; may freeze)`。設定存在 `settings.json`：

```json
"video": { "codec": "h264" }
```

- `hevc`：引擎帶 `-h265`，宣告 HEVC 能力；client 仍可能選 H.264。
- `h264`：不帶 `-h265`，不宣告 HEVC，client 使用 H.264。
- **HEVC 只會在請求高度大於 1080p 時被 client 選用**；`-s
  1920x1080@60` 會走 H.264。Native 預設請求 2360x1640，已可觸發
  HEVC；若改成 1080p 則會走 H.264。4K 負載最高，建議有線網路。
- 這是 Apple screen-mirroring client 的 codec 選擇行為：server 只能
  宣告 bit 42 capability，不能強制 1080p 使用 HEVC。
- `-h265` 只宣告能力，不是解析度選項；沒有 `-s` 時 libuxplay 預設
  1920x1080。要 HEVC 必須明確請求 height >1080（Native 預設即符合）。
- 若要在接近 1080p 使用 HEVC，實務上只能提高請求高度（例如 1440p）
  後在輸出端縮放；這會增加網路與解碼負載，不是真正的 1080p HEVC。
- 這是能力宣告與偏好，不是強制 actual codec；實際值看 Media Info
  或 diagnostics 的 `Video Codec`。
- 變更目前需要重啟 app；Phase 2C 會改為只重啟 engine worker。
- 既有設定若沒有 `video.codec`：`extra_args` 有 `-h265` 視為
  `hevc`，沒有則保留 `h264`。新安裝預設 `h264`。
- **`textoverlay` 是 `frames=1` 的根因**：H.264／H.265、HUD 開或關都會
  卡在 textoverlay；probe 顯示 `overlay_in` 持續增加但 `overlay_out=1`。
  已從 AirPlay mirror pipeline 移除，HUD 暫時停用；詳見 Media Info HUD。
- pipeline watchdog 採保守門檻：連續 20 秒沒有 presented frame、且已
  streaming 20 秒、連續 3 次取樣才判定 stall；**只記錄，不自動重建或
  自動切換 codec**。

#### 解析度：Native 預設

- `Native (device)` 是預設，使用 `native_width`／`native_height`；這台
  iPad13,18 預設為 `2360x1640`，`-h265` 仍會生效。
- 解析度 preset（720p／1080p／1440p／4K）不會覆寫 Native 尺寸；
  要改 Native，先選 `Custom` 填入寬高，再切回 `Native`。
- `Output:` 顯示實際串流寬高，`Requested:` 顯示送出的 `-s`；
  主視窗外掛與 Media Info HUD 都有這兩行。
- `720p`（1280x720）可降低負載；height <=1080，client 通常送 H.264。
- `Output:` 是實際串流寬高；`Requested:` 是 server 送出的 `-s`，
  兩者可能不同，因為 client 可自行調整。
- `-s` 是請求上限與效能提示，不是硬命令；client 可送更低解析度，
  寬度也會依畫面比例調整。
- Native **不是自動查詢**：AirPlay 沒有回報原生解析度的欄位；這些值是
  已設定的請求。其他裝置請先選 `Custom` 填入寬高，再選 `Native`。
- 舊設定的 `auto` 會遷移成 `native`；不再提供「不指定、由 client
  決定」的選項。即使直接跑引擎，`-h265` 也不再改變預設解析度；
  沒有 `-s` 時固定 1080p。
- 實測省略 `displays` 節點會讓 iPad 連上後立刻斷線（沒有 video
  format）；client 需要 display 資訊，因此不能把「不送 display」
  當成 native。
- Native 模式會停用寬高欄位，避免誤改；refresh 仍以自訂值為準。

### VS Code F5

`.vscode` 已附可直接用的設定：

| 檔案 | 用途 |
| --- | --- |
| `tasks.json` | `build-dev`（預設 build task） |
| `launch.json` | `Debug uxplay-windows (dev)`，用 gdb 啟動 |
| `settings.json` | `msys2.root`／`msys2.prefix` |
| `c_cpp_properties.json` | 用 `build/dev/compile_commands.json` |

IntelliSense 會另外定義 `_WIN32` 與 `WIN32`；libuxplay 的 `compat.h`／
`sockets.h` 兩者都接受。若仍看到 `sys/socket.h` 找不到，先在
`build-dev` 後執行 **C/C++: Reset IntelliSense Database** 或
**Developer: Reload Window**，再確認 `build/dev/compile_commands.json`
存在。

前置：

1. 安裝 C/C++ extension（`ms-vscode.cpptools`）。
2. UCRT64 gdb：
   `pacman -S --needed mingw-w64-ucrt-x86_64-gdb`

按 F5 會先跑 `build-dev`，再用 gdb 啟動
`build/dev/uxplay-windows.exe --engine-debug`；`PATH`、
`QT_PLUGIN_PATH`、`GST_PLUGIN_PATH` 與 `run-dev.cmd` 相同。
中斷點可直接設在 `src/main.cpp`、`src/mainwindow.cpp`。

`dev_build.sh` 會把 UCRT64 的 DLL、`qt.conf` 與 GStreamer plugins
放進 `build/dev`，所以 F5 不需要先開 MSYS2 shell。`launch.json` 目前
使用這台機器的固定 UCRT64 路徑；換機器時同時改 `.vscode/settings.json`
與 `.vscode/launch.json`。

注意：

- F5 前先結束已在執行的 dev app；否則連結失敗，或新 instance
  因單一實例鎖直接退出。
- 預設 build 是 `RelWithDebInfo`（`-O2 -g`），已可設中斷點；
  要完整 Debug 用 `bash scripts/dev_build.sh --debug`，但 F5 的
  `build-dev` 會再 configure 回 RelWithDebInfo。
- `.vscode/settings.json` 的 `msys2.root`／`msys2.prefix` 是這台機器
  的路徑；MSYS2 換位置時改這裡。

若用其他 editor／gdb 直接啟動 exe，請補上相同的環境變數
（`PATH`、`QT_PLUGIN_PATH`、`GST_PLUGIN_PATH`），否則 debugger
不會繼承 MSYS2 shell 的 PATH。

`dev_run.sh` 會設定：

- `PATH`：加入 `$MSYSTEM_PREFIX/bin`（Qt6／GStreamer／OpenSSL DLL）。
- `QT_PLUGIN_PATH`：`$MSYSTEM_PREFIX/share/qt6/plugins`。
- `GST_PLUGIN_PATH`：只有 build 目錄沒有 `lib/gstreamer-1.0` 時，
  才指向 `$MSYSTEM_PREFIX/lib/gstreamer-1.0`。

App 的 log 在：

```text
%LOCALAPPDATA%\leapbtw\uxplay-windows\logs\uxplay-windows.log
```

- 每 2 MB 輪替，保留 `uxplay-windows.1.log` 到 `.3.log`。
- 引擎的 `LOGI`／`LOGE` 會以 `[engine]` 前綴寫入同一份 log。
- 托盤選單與主視窗都有 **Open Log**。
- 主視窗的紅色錯誤列會顯示最後一筆引擎錯誤。

常用檢查：

```bash
# 看引擎是否完成 mDNS 註冊
grep -E "register_dnssd|Initialized server socket|using system MAC" \
  "$LOCALAPPDATA/leapbtw/uxplay-windows/logs/uxplay-windows.log"
```

### Media Info HUD（暫時停用）

HUD 目前暫時停用，主視窗 checkbox、tray 開關與位置選單都是 disabled。
原因不是 H.264／HEVC，也不是 HUD 文字內容，而是 **GStreamer
`textoverlay` 在 AirPlay mirror pipeline 中只輸出第一張 frame**：

```text
decoded_frames=704
overlay_in=704
overlay_out=1
frames=1
```

移除 `textoverlay` 後同一條 pipeline 可維持 30–60fps，且 `frames`
持續增加。相同 GStreamer 1.28.3 的 `videotestsrc + textoverlay` 最小
測試可正常跑完，因此不是整體 GStreamer 損壞，而是 textoverlay 與這個
AirPlay／appsrc／caps 組合的特定問題。

後續會改用不阻塞的 HUD 實作（`cairooverlay` 或獨立 native overlay），
在替代方案驗證前不要重新加入 `textoverlay`；HUD 欄位與 margin 設定仍保留，
供新實作使用。

- FPS 來源仍是 video sink pad probe 的實際 presented frame。
- `hud.enabled`、`hud.corner`、`hud.margin_x`、`hud.margin_y`、
  `hud.fields` 仍保存在 `settings.json`。

### 影片視窗置前

- 新影片視窗第一次出現時只做一次 `HWND_TOPMOST → HWND_NOTOPMOST`，
  將它拉到其他一般視窗之上，預設不持續置頂。
- 需要一直保持在最上層時，tray 勾選 `Video always on top`；
  設定存在 `video_window.always_on_top`，預設關閉。

### 本地化

- 來源語言是英文；`translations/uxplay-windows_zh_TW.ts` 是繁體中文。
- `settings.json` 的 `language` 可設 `system`（預設）、`en`、`zh_TW`。
  也可用 tray 的 `Language` 子選單或 `--lang=zh_TW` 覆寫；變更後重啟 App。
- 缺 `.qm`、翻譯為空或找不到 locale 時，Qt 會顯示英文原文；
  不會顯示空字串或 key。
- 新增 UI 字串時用 `tr("...")`，再執行：

```powershell
# 有 lupdate 在 PATH 時
lupdate src -ts translations/uxplay-windows_zh_TW.ts -no-obsolete

# 沒有 PATH 時指定實際路徑
$lupdate = "$env:MSYSTEM_PREFIX\bin\lupdate.exe"
& $lupdate src -ts translations/uxplay-windows_zh_TW.ts -no-obsolete

# 填入翻譯後
python scripts/fill_translations.py

# 驗證
python scripts/check_i18n.py --lupdate $lupdate --color=always
```

VS Code 可直接跑 `i18n: update catalog` 與 `i18n: check`；後者會使用
`${config:msys2.prefix}\bin\lupdate.exe`。

`scripts/check_i18n.py` 會檢查缺少的 source key、空白／unfinished
翻譯、多餘 catalog key、placeholder 不一致，以及未使用 `tr()` 的明顯 UI
字串；輸出以 `[ERROR]`／`[WARN]`／`[OK]` 顏色區分。完整規範見
`translations/README.md`。

沒有 `lupdate` 時，本機預設只驗證 catalog 並顯示黃色警告；若要避免跳過
source-key 比對，使用 `--require-lupdate`。正式 CI 使用：

```powershell
python scripts/check_i18n.py --require-lupdate
```

### 凍結與重建

AirPlay 影片可能出現「聲音仍在、畫面定格」而不斷線的狀態。

- Engine watchdog：mirror 連線已收到 video codec，但連續 8 秒沒有
  video packet 時，記錄 log 並重建 mirror 連線（`conn_reset`）。
- 若 6 秒內完全沒有收到 video codec，也會重建；這對應「只出聲音、
  沒有 video format」的 reconnect 卡住。
- `video_stream_suspended`（client 暫停或鎖屏）期間不觸發。
- 手動：tray `Rebuild AirPlay stream` 會停止並重新啟動 engine，
  不需要重啟整個 App。
- 自動重建失敗時，仍可用 `Restart` 或重新連線。

## 狀態位置與重置

| 狀態 | 位置 | 重置方式 |
| --- | --- | --- |
| 引擎／UI 設定 | `%APPDATA%\leapbtw\uxplay-windows\settings.json` | 關閉 App 後刪除，下次啟動用預設值 |
| 舊 `arguments.txt` | `%APPDATA%\leapbtw\uxplay-windows\arguments.txt.migrated` | 一次性遷移備份，可刪除 |
| 開機自啟 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` | 刪除 `uxplay-windows` 值 |
| MSI 桌面捷徑 | `HKCU\Software\leapbtw\uxplay-windows` 的 `DesktopShortcut` | 由 MSI 管理 |
| 日誌 | `%LOCALAPPDATA%\leapbtw\uxplay-windows\logs` | 關閉 App 後刪除目錄 |
| 單一實例 | Windows named mutex `Local\uxplay-windows-single-instance` | 由作業系統在行程結束時釋放；必要時關閉所有 `uxplay-windows.exe` |
| BLE 狀態檔 | `%APPDATA%\leapbtw\uxplay-windows\uxplay_status.ble` | 引擎停止時自動刪除；必要時手動刪除 |

`settings.json` 主要欄位：`display_name`、`ble_enabled`、`force_fullscreen`、
`renderer`、`resolution`、`no_hold`、`server_enabled`、`extra_args`。
`extra_args` 放未納入 UI 的引擎參數（例如 `-fps 60`）；`-h265`
由 `video.codec` 管理，不會留在 `extra_args`。

### 引擎設定何時套用

- 影響引擎參數的設定（AirPlay 名稱、解析度、renderer、codec、BLE、全螢幕）
  會先寫入 `settings.json`，主視窗顯示黄色的
  「引擎設定已變更」列與「套用並重新啟動引擎」按鈕。
- 按下套用只會停止並重建 AirPlay engine worker，不會重啟整個 App。
- HUD、診斷、OBS 模式、影片視窗置頂等執行期選項會立即套用，不進入
  pending 狀態。
- AirPlay 已停止時，設定仍會儲存；下次啟動 engine 時直接使用新參數。
- 直接編輯 `settings.json` 的 `extra_args` 不會被 UI 自動偵測，仍需重新
  啟動 App 或重新啟動 engine。

第一次啟動 Phase 2B 會把舊 QSettings 與 `arguments.txt` 匯入
`settings.json`；`arguments.txt` 會改名為 `arguments.txt.migrated`，
舊 QSettings 的引擎 key 會移除。

## Windows 防火牆規則

Windows 防火牆以**執行檔完整路徑**辨識程式；每個 build 目錄
（`build/dev`、`build/fix`、`build/phase2a`、Temp 解壓）都是一個
不同的程式，因此會各自產生一組規則。

- 日常開發固定使用 `build/dev`；`BUILD_DIR` 只用於一次性隔離測試。
- MSI 安裝時會建立一組 `uxplay-windows` 規則（Grouping 同為
  `uxplay-windows`），指向
  `C:\Program Files\uxplay-windows\uxplay-windows.exe`。
- 清除舊 build／Temp 規則（需要管理員）：

  ```powershell
  .\scripts\clean_firewall_rules.ps1
  ```

  腳本保留 MSI 的 `uxplay-windows` group，移除其他 uxplay 規則。
- 若 Windows 仍跳出提示，允許一次即可；同一路徑不會再重複建立。

## 疑難排解

| 症狀 | 可能原因 | 處理 |
| --- | --- | --- |
| iPhone 看不到 PC | Bonjour 沒跑、防火牆擋 UDP 5353、不同網段 | 看 log 是否有 `register_dnssd`；啟動 Bonjour Service；確認 MSI 的 `uxplay-windows` 防火牆規則存在 |
| `register_dnssd` 失敗 | 名稱或 MAC 衝突、port 被佔用 | 關閉其他實例；必要時在 `arguments.txt` 加 `-m`／`-p` |
| 引擎一啟動就退出 | 參數錯誤、GStreamer／Bonjour 初始化失敗 | 看 log 的 `[engine]` 與 `*** ERROR` 行；先用 `--engine-debug` |
| GStreamer plugin 找不到 | `GST_PLUGIN_PATH` 沒指到 UCRT64 plugin | 用 `dev_run.sh`；確認 `$MSYSTEM_PREFIX/lib/gstreamer-1.0` 存在 |
| 找不到 `libgcc_s_seh-1.dll`／`Qt6Core.dll` 或 Qt platform plugin | 直接跑了非 self-contained 的 exe | 用 `dev_run.sh`／`run-dev.cmd`；`dev_build.sh` 會複製 Qt platform plugin 到 `build/dev/platforms` |
| 沒有聲音 | Windows 輸出裝置不對 | 在 `arguments.txt` 用 `-as` 指定 sink；或改系統預設裝置 |
| 第二次啟動沒叫回視窗／卡在 already running | 舊行程仍持有 named mutex，或 IPC server 尚未啟動 | 確認托盤圖示仍在；必要時關閉所有 `uxplay-windows.exe` 後重開 |
| BLE 找不到裝置 | `uxplay-bluetooth-beacon.exe` 不存在 | 本地 debug 可忽略；要 BLE 請 build beacon |
| `--restart` 後沒起來 | 舊行程還沒退出 | 等 5 秒；看新行程 log 與 lock |

## Release 打包（與 dev build 不同）

- `make_release.sh`：互動式打包。會啟動 App、要求你連一台裝置開始串流，
  再抓取執行中的 UCRT64 DLL、跑 `windeployqt`、複製 icon。
- `make_release_CI.sh`：GitHub Actions 使用；會額外複製
  `dnssd.dll` 與 `mDNSResponder.exe`。
- Dev build 不複製 DLL／GStreamer plugin，請用 `dev_run.sh` 啟動。
- `make_release.sh` 目前的本機路徑仍偏 MINGW64，且未複製
  `mDNSResponder.exe`／`dnssd.dll`；這列在 `TODO.md` 的 Phase 4。

### 本地建 MSI

1. 先 stage `release/`：在 UCRT64 執行 `make_release_CI.sh`（需要
   PyInstaller／winrt）或 `make_release.sh`。
2. 安裝 WiX v7：`dotnet tool install --global wix`。
3. 執行：

   ```powershell
   .\scripts\build_msi.ps1 -Arch x64
   ```

- 產物：`uxplaywindows-installer-x64.msi`；ARM64 用 `-Arch arm64`。
- `product.wxs` 會把 `release/**` 打包；`LICENSE.rtf` 會自動從
  `docs/` 複製。
- `release/` 的內容必須對應目標架構（x64 或 arm64）。

### Nightly release

- 每次 build 使用唯一 tag：`nightly-x64-<UTC timestamp>-r<run>`、
  `nightly-arm64-...`；不再覆蓋 `nightly-build-x64`／`-arm64`。
- 每個架構保留最近 **10 個** release；建立新 release 成功後才刪除較舊的。
- 名稱：`Nightly Build <short-sha> - <台北時間 AM/PM> (UTC+8, Asia/Taipei)`。
- 內文：commit 連結、workflow run、架構、最近 10 筆 commit。
- arm64 的 `pacman` 依賴安裝固定走 `mirror.msys2.org`，失敗重試 3 次；
  避免 `ftp2.osuosl.org` 慢速造成 nightly 失敗。
- Assets 名稱固定；正式版本、Core／Exp 分類留到 Phase 3。

## 虛擬攝影機（Windows 11 spike）

Windows 11 的 Media Foundation virtual camera 走
`MFCreateVirtualCamera`，不需要 kernel driver；官方 Microsoft
`Windows-Camera` sample 已在本機建置與驗證。細節、建置命令、HKLM
註冊與 named-pipe frame protocol 見
[`virtualcam/README.md`](../virtualcam/README.md)。

目前狀態：

- `SWVCamMediaSource (Windows 虛擬攝影機)` 可由 DirectShow 列舉
- Python 測試 producer 以 2360x1640 NV12、60fps 經
  `\\.\pipe\uxplay-vcam-frame-v1` 送入
- ffmpeg 擷取成功，兩張不同時間的 frame SHA-256 不同
- AirPlay renderer 已加入 `-vcam` / `appsink` 分支；pipeline 可建立，
  仍待 iPad 端到端驗證

虛擬攝影機只輸出 video，audio 與 HUD 不在其中。

主視窗 OBS 區的 `AirPlay virtual camera (experimental)` 勾選會儲存到
`settings.json` 的 `virtualcam.enabled`，按下「套用並重新啟動引擎」
後生效。

## USB Clean Output（實驗性）

USB 螢幕走 CoreDevice display stream，不走 QuickTime USB。`--usb-obs`
會啟動 Python sidecar + GStreamer，並開一個無邊框視窗
`uxplay-windows USB Clean Output`；OBS 用 Window Capture 選這個標題。

```powershell
build\dev\run-dev.cmd --usb-obs
```

需求：

- iPad 以 USB 連線、解鎖、信任此電腦。
- Developer Mode 開啟。
- DDI 已掛載：`py -3.11 -m pymobiledevice3 mounter auto-mount`。
- Python 3 + `pymobiledevice3`；`UXPLAY_USB_PYTHON` 可覆寫直譯器。

模式設定在 `settings.json`：

```json
"obs": { "mode": "off" }
```

- `off`：預設，不啟動 USB 擷取。
- `window`：偏好使用 USB Clean Output；**不會在 app 啟動時自動開始**，
  要按 `Connect USB device`、選 Clean window 或 `--usb-obs` 才啟動。
- `camera`：保留給 virtual camera，尚未實作。

主視窗的 `OBS output (USB capture)` 下拉選單就是這個設定。
`--usb-obs` 等於選 `window`（會持久化）並明確啟動；關閉 Clean Output
視窗會回到 `off` 並停止 USB 子程序。

重啟 app 不會因為 `obs.mode=window` 自動 preflight，也不會發 USB
失敗通知；這是為了避免每次啟動都變成通知噪音。

選擇 `window` 或按 `Connect USB device` 會先跑 preflight：

1. `usbmux list` 偵測裝置。
2. `lockdown info` 檢查配對；未配對時跑 `lockdown pair`，需要在 iPad
   上按 **Trust This Computer**。
3. `amfi developer-mode-status` 檢查 Developer Mode；關閉時顯示指示。
4. `mounter auto-mount` 掛載 DDI（第一次可能需要下載）。
5. 全部通過才啟動 sidecar 與 Clean Output。

狀態列會顯示目前步驟與錯誤原因；失敗不會啟動 sidecar。
`Connect USB device`／`Reconnect USB device` 可重跑 preflight。

### USB 供電

USB 模式只負責資料，不負責供電。PC 的 USB 埠不會因為 app 而提高
電流；USB 2.0 預設 500mA、USB 3.x 900mA，USB-C 在沒有 PD 協商時
通常只有 5V/1.5A 或 3A。

本機實測（iPad13,18，iOS 27.3，接在主機 USB 埠）：

```text
AdapterDetails: 5000mV / 500mA / 3W ("usb brick")
Amperage: -1976 mA（放電）
BatteryIsCharging: false
BatteryCurrentCapacity: 51%
```

也就是邊串流邊掉電。**就算停止 Clean Output，只要 USB 資料線還插在
iPad 唯一的 USB-C 埠上，它仍然只能從這個埠取得 3W；停止串流只降低
負載，不會恢復充電。** 要充電只有兩條路：拔掉 USB 線改用充電器
（AirPlay），或使用可同時供電與資料的 PD dock／USB-C 供電分接器。
preflight 會讀
`com.apple.mobile.battery` 與 `diagnostics battery single`，狀態列顯示
`USB ready: iPad · 50% · not charging · 3W adapter`；未充電時顯示橘色
並在 tray 警告，但不會阻止啟動。

長期使用要外接供電，接法：

```text
iPad USB-C ── USB-C hub/dock ── PC USB-A／USB-C（資料）
                    │
                    └── USB-C PD 充電器（30W 以上）
```

名詞：

- **upstream**：接電腦那側；**downstream**：接 iPad 那側。
- **PD passthrough**：dock 把充電器的 USB PD 電力轉給下游裝置。
- **CDP**（Charging Downstream Port）：可同時提供資料與充電的 USB 埠。

#### 採購與接線

**不是買一般的 USB 分線器。** 被動一分二 Y 線沒有 PD 協商，可能逆灌；
需要的是「帶 PD 輸入、能對 downstream 供電」的主動式 hub／dock，或
標示 data + charging 的 USB-C 供電分接器（power injector）。

**也不是「線只有資料功能」。** USB 資料線通常也帶電；能列舉、能串流
表示資料與基本 VBUS 正常。`3W` 是 USB-A 2.0 埠的 500mA 上限：
換支援 3A 的 A-C 線仍是 500mA；換 USB 3 埠＋線最多 900mA（4.5W），
仍遠低於 iPad 使用中的約 10W。只有 PD 供電源（dock／充電器）會改變。
charge-only 線沒有資料對，會直接列舉不到；你現在能串流，所以不是
這個問題。

主板的 AI Charging／BC 1.2 是 **5V 增流**（1.5A／2.4A，7.5／12W），
不是 PD；可以減緩掉電，但通常不足以在滿載時充電。USB-A 沒有 PD，
這是硬體上限。若主板有 ON/OFF Charge 或指定充電埠，可以測試，
adapter 瓦數應會上升，但仍不會到 20W。

桌機只有 USB-A 時：

- 資料線用 **USB-C（iPad）→ USB-A（PC）**；USB 2.0 即可，USB 3 更好。
- USB-A 埠本身沒有 PD，最多 5V/0.5A（USB 2.0）或 0.9A（USB 3.x），
  不可能靠這條線充電。
- **USB-C→USB-C 線不能直接插進 USB-A 埠**；C-C 是用在充電器↔dock、
  或 dock↔iPad。PC 端要 A-C（或 PC 有 USB-C 埠才用 C-C）。

採購清單：

| 角色 | 規格 | 注意 |
| --- | --- | --- |
| 供電 hub/dock | downstream USB-C 支援資料 + 充電，PD passthrough | 只往 upstream 筆電送 PD 的 laptop dock 不行 |
| 充電器 | USB-C PD 30W 以上 | iPad 會自行協商，不會硬吃 |
| PC 資料線 | USB-C→USB-A 或 C→C，依 PC 埠 | 必須是資料線，不是 charge-only |
| 充電線 | USB-C→USB-C PD 60W 以上 | 充電器到 dock |
| iPad 線 | USB-C→USB-C 資料 + PD | 若 dock 到 iPad 是 C 口 |

不要買：

- 被動 Y 型「一分二」分線器：可能逆灌、沒有 PD 協商。
- 無電源 hub：下游通常只有 5V/0.9A。
- 只標「充電」的 USB-C 埠：可能沒有資料。
- 只把 PD 往 upstream 筆電送的 laptop dock：iPad 還是不會充。

驗證：

- app 狀態列應從 `3W adapter` 變成 `charging` 且瓦數大於 10W。
- 若仍是 `3W`／`not charging`，dock 沒把 PD 送到 iPad。
- iPad 80% 優化充電時可能暫停充電；以 adapter 瓦數與 app 狀態為準。

替代方案：長期使用走 AirPlay，USB-C 埠留給充電器；USB 模式留給短
時間、低延遲的場合。

目前 video-only；AAC-ELD 在 Windows 還沒解碼。QuickTime USB 在
user mode 不可行，需 libusb0 upper filter 與簽章 driver，未實作。
`ENABLE_USB_CAPTURE=ON` 仍只編譯 QuickTime skeleton，與這條路無關。
協定與實測數字見 `usbcapture/spike/README.md`。

## 目前限制

### 日誌與診斷驗證

- dev 固定使用 `build/dev/uxplay-windows.exe`，主要由 VSCode 啟動偵錯。
  建置前先停止該偵錯程序；不要另取名字繞過執行檔占用。
- 日誌由背景執行緒每 250 ms 或 64 KiB 批次輸出；佇列上限 4096 則／4 MiB。
  滿載時略過一般訊息並寫摘要，Runtime Debug 的 `logging` 行顯示排隊、
  略過與批次數。正常關閉會排空，強制終止仍可能失去尾端紀錄。
- `--engine-debug` 保留一般診斷，封包與補包雜訊每五秒摘要；只有需要完整
  封包文字時使用 `--engine-trace`，此模式仍受佇列上限限制。
- `audio ingress`／`audio delivery` 的時間診斷在 log 中，尚未接到
  Runtime Debug。它們區分抵達時的映射時間戳落後量及本機佇列等待，
  不代表喇叭實際延遲。
- 執行 `python Testing/run_tests.py` 驗證音訊緩衝與排空、執行緒回收、
  GPU 快照與背景日誌。測試範圍和實機限制見 `Testing/README.md`。

- 設定變更（顯示名稱、解析度、renderer、codec、BLE、全螢幕）需要按
  「套用並重新啟動引擎」；只重啟 worker，不重啟 App。
- Core／Exp release channel、SemVer tag 尚未定義；nightly 使用唯一 tag，
  每個架構保留最近 10 個 release。
- 客戶端連線事件已接到 UI；實機驗證請看 `[engine]`／
  `Client connected` 與 tray 的 `Client` 欄位。
