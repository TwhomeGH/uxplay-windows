# 開發者指南：本地建置與除錯

> 這份是本地開發的單一入口。Release 安裝包流程見
> [BUILDING.md](./BUILDING.md)；變更歷史用 `changelogkit` 維護。

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

> 直接雙擊 `build/dev/uxplay-windows.exe` 會找不到
> `libgcc_s_seh-1.dll`（Qt／GStreamer 也依賴它）。請用上面的
> `dev_run.sh`，或建置後產生的 `build/dev/run-dev.cmd`。

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
- （選配）BLE beacon 需要 Python 套件；本地 debug 沒 build
  `uxplay-bluetooth-beacon.exe` 也能跑，App 只會在 log 記一筆
  `uxplay-bluetooth-beacon.exe not found`，mDNS 發現不受影響。

> 這台機器實際的 MSYS2 prefix 可能是 `H:\FIX_VIAP\msys64\ucrt64`；
> 腳本用 `$MSYSTEM_PREFIX`，不要手動硬寫 `/ucrt64` 或 `C:\msys64`。

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
bash scripts/dev_run.sh --engine-debug   # 額外傳 -d 給引擎
bash scripts/dev_run.sh --detach         # 背景啟動後返回
```

從 Explorer／PowerShell 直接啟動時，用建置時產生的 wrapper：

```powershell
.\build\dev\run-dev.cmd
.\build\dev\run-dev.cmd --engine-debug
```

`dev_build.sh` 會把 `$MSYSTEM_PREFIX/bin`（含
`libgcc_s_seh-1.dll`、Qt6、GStreamer）寫進 `run-dev.cmd` 的 `PATH`。

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

預設會保留最近 30 分鐘的事件與 1Hz 指標（可在 tray／主視窗關閉）。
發生問題時：

- 主視窗 **Save Diagnostics** 或 tray 的 **Save Diagnostics**
- 既有實例：`uxplay-windows.exe --save-diagnostics`

輸出：

```text
%LOCALAPPDATA%\leapbtw\uxplay-windows\diagnostics\
  uxplay-diagnostics-<timestamp>.zip
  uxplay-diagnostics-<timestamp>\
```

日誌與診斷入口在 tray 的 `Logs & Diagnostics`，或主視窗 `Open Log`
按鈕（點擊會展開選單）：

- **Open Log**：用系統預設程式開啟。
- **Open Log With...**：Windows「開啟方式」，可選 VS Code 或其他工具。
- **Open Log Folder**：開啟日誌資料夾。
- **Open Diagnostics Folder**：開啟診斷包資料夾（不存在時會建立）。

內容包括 `manifest.json`、`events.jsonl`、`metrics.jsonl`、logs、
`settings.redacted.json`、`system.txt` 與 `README.txt`。

保留量：

- 記憶體 ring buffer：3000 events、1800 metrics（1Hz 約 30 分鐘）。
- 日誌：2MB × 3 個 rotated 檔＋目前檔。
- 診斷包：只保留最近 **10 組** folder／zip；每次匯出後自動刪除較舊的。
  要長期保存的 bundle 請先 copy 到其他位置。

匯出時會 redact 裝置名稱、UDID、MAC、IPv4 與使用者路徑；client model
等技術欄位會保留。**不會自動上傳**；分享前請自行檢查。回報請使用
`.github/ISSUE_TEMPLATE/bug_report.yml` 並附上 zip。

### 視訊編碼偏好（HEVC / H.264）

主視窗 `Video Codec` 預設 `HEVC / H.265 (needs >1080p)`，可切回
`H.264 / AVC`。設定存在 `settings.json`：

```json
"video": { "codec": "hevc" }
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
  `hevc`，沒有則保留 `h264`。新安裝預設 `hevc`。

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

### Media Info HUD

主視窗 `Media Info HUD` 勾選後，資訊會直接畫進 AirPlay 影片畫面：

- 內容：Client、Status、Video Codec／Stream Size、Decoder、
  Renderer、Audio Codec、Audio Format。
- 實作：GStreamer `textoverlay name=hud_overlay`，文字是 video
  frame 的一部分，因此只有一個視窗，不會有獨立 HUD 閃爍／飄移。
- 開關：主視窗 checkbox、tray `Media Info HUD`；關閉時送空字串。
- 位置：tray `HUD position` 可選 `Top left`／`Top right`，對應
  `halignment`。
- 文字會出現在 OBS 視窗擷取中，這是預期行為。
- 設定存在 `settings.json` 的 `hud.enabled`／`hud.corner`；預設關閉。

### 凍結與重建

AirPlay 影片可能出現「聲音仍在、畫面定格」而不斷線的狀態。

- Engine watchdog：mirror 連線已收到 video codec，但連續 8 秒沒有
  video packet 時，記錄 log 並重建 mirror 連線（`conn_reset`）。
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
| 單一實例 lock | `%TEMP%\uxplay-windows.lock` | 正常會自動處理；卡住時關閉 App 後刪除 |
| BLE 狀態檔 | `%APPDATA%\leapbtw\uxplay-windows\uxplay_status.ble` | 引擎停止時自動刪除；必要時手動刪除 |

`settings.json` 主要欄位：`display_name`、`ble_enabled`、`force_fullscreen`、
`renderer`、`resolution`、`no_hold`、`server_enabled`、`extra_args`。
`extra_args` 放未納入 UI 的引擎參數（例如 `-fps 60`）；`-h265`
由 `video.codec` 管理，不會留在 `extra_args`。

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
| 找不到 `libgcc_s_seh-1.dll`／`Qt6Core.dll` | 直接跑了 exe，PATH 沒設 | 用 `dev_run.sh` 或 `build/dev/run-dev.cmd`；debugger 請在 launch config 設 `PATH` |
| 沒有聲音 | Windows 輸出裝置不對 | 在 `arguments.txt` 用 `-as` 指定 sink；或改系統預設裝置 |
| 第二次啟動沒叫回視窗／卡在 already running | 舊 lock 的 PID 已失效或被重用 | 新版會自動清除 stale lock；仍卡住時刪 `%TEMP%\uxplay-windows.lock` |
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

- 固定 tag：`nightly-build-x64`、`nightly-build-arm64`；每晚刪除重建，
  tag 指向當前 `main` commit。
- 名稱：`Nightly Build <short-sha> - <台北時間 AM/PM> (UTC+8, Asia/Taipei)`。
- 內文：commit 連結、workflow run、架構、最近 10 筆 commit。
- arm64 的 `pacman` 依賴安裝固定走 `mirror.msys2.org`，失敗重試 3 次；
  避免 `ftp2.osuosl.org` 慢速造成 nightly 失敗。
- Assets 名稱固定；正式版本、Core／Exp 分類留到 Phase 3。

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

## 目前限制（Phase 2A 後）

- 設定變更（顯示名稱、解析度、renderer）仍會重啟 App；
  Phase 2B／2C 會改為套用設定並重啟引擎 worker。
- Core／Exp release channel、SemVer tag 尚未定義；nightly 仍用固定 tag。
- 客戶端連線事件已接到 UI；實機驗證請看 `[engine]`／
  `Client connected` 與 tray 的 `Client` 欄位。
