# 待辦清單 TODO

> [!TIP]
> AirPlay／UX 改進依「可觀測性 → 生命週期 → 功能 → 體驗」分期推進；
> 完成後把 `[ ]` 改成 `[x]`，並用 `changelogkit add` 留下變更紀錄。

## Phase 1 — 可觀測性與基本操作

- [x] 擴充引擎 callback：`client_connected(name, model, deviceid)`、`client_disconnected`、`open_connections`。
- [x] 主視窗與 tray 顯示目前連線裝置名稱／型號，連線與斷線時通知。
- [x] 引擎日誌寫入檔案（rolling log），tray 加「Open Log」，主視窗顯示最後錯誤。
- [x] tray 新增 Start/Stop AirPlay；圖示與 tooltip 區分 idle／streaming／error
  （Phase 2A 已改為引擎熱啟停）。
- [x] 單一實例保護（QLockFile 或 named mutex），第二次啟動叫回既有視窗。
- [x] Bonjour 服務改檢查執行狀態並嘗試自動啟動；修正 `ACCESS_DENIED` 被誤判為存在。

## Phase 2 — 生命週期與設定模型

- [x] 設定收斂為單一結構化來源（JSON/INI），命令列參數由設定產生，只留「額外參數」欄位。
- [ ] 設定變更改為重啟引擎 worker，UI 顯示 pending 變更與單一「套用」動作。
- [x] 修正優雅停止路徑：`stop_uxplay()` 實際被呼叫；移除 `cleanup()` 內的 `exit(0)`。
- [x] 引擎崩潰重啟加入 backoff，並顯示「引擎連續失敗」狀態。

## Phase 3 — AirPlay 功能

- [ ] PIN／密碼／allow-block 存取控制 UI；`display_pin` 改以 callback 在 GUI 顯示 PIN。
- [ ] 音訊輸出裝置選擇（`-as`／WASAPI）。
- [ ] 曝露錄影（`-mp4`）、HLS（`-hls`）、`-fps`、翻轉／旋轉、overscan、`-reset`、`-nc`／`-nohold`。
- [ ] 影片視窗置頂／記憶位置／縮到 tray；視窗標題在地化。

## Phase 4 — UI／在地化／維護

- [ ] 主視窗以 QGroupBox 分組並加入 scroll，修正固定 420×420 的擁擠版面。
- [ ] i18n（至少 zh_TW）；`windeployqt` 納入 Qt 翻譯。
- [ ] 更新檢查器（README 的 TODO 項目）。
- [ ] `make_release.sh` 補複製 `mDNSResponder.exe`／`dnssd.dll`，與 CI 打包一致。
- [ ] Release 策略：SemVer／tag、Core／Exp 分類、nightly commit SHA 與版本顯示；
  先維持 nightly 固定 tag 替換。
- [ ] 移除未使用的 `m_autostartAction`／`m_statusAction`。

## 已完成

- [x] 接入 changelogkit（`.changelog.toml`、`docs/ChangeHistory.md`）。
- [x] 建立根目錄 `TODO.md` 並整理 AirPlay 改進分期。
- [x] 本地建置／除錯腳本與開發者指南
  （`scripts/dev_build.sh`、`scripts/dev_run.sh`、`docs/DEVELOPERS-GUIDE.md`）。
- [x] Phase 2A：引擎熱啟停、ready callback、崩潰 backoff、`--start`／`--stop` IPC。
- [x] MSI 固定防火牆規則（WiX Firewall extension）與清理舊 build 規則腳本。

## Phase 5 - USB 擷取（探索）

- [x] 建立 `usbcapture/` 骨架與 `ENABLE_USB_CAPTURE`（預設 OFF）。
- [x] USB 定位：獨立選用模式，預設 AirPlay 不受影響。
- [x] Windows spike：QuickTime USB 在 user mode 不可行，需 libusb0 upper
  filter；CoreDevice display stream 可用（iOS 27+，需 Developer Mode + DDI）。
- [x] iOS 27 QuickTime subclass 由 `0x2A` 改為 `0xFD`，已記錄於 spike README。
- [x] CoreDevice `/stream.bin` → Annex-B → GStreamer bridge 原型
  （`usbcapture/spike/tools/stream_to_tcp.py`）。
- [x] bridge 實測：240+ access units 進入 GStreamer，live
  `d3d11videosink` 正常播放。
- [x] rotation／padding：`videoflip counterclockwise` + `videocrop 8/24`。
- [x] Clean Output 視窗：frameless、無邊框、固定標題、OBS Window Capture。
- [x] Clean Output 可拖曳移動、8 px 邊緣縮放、右鍵 Move／Resize fallback。
- [x] `--usb-obs`／tray／主視窗入口；已有實例走 IPC。
- [x] `obs.mode` schema（off／window；camera 保留）與主視窗下拉選單。
- [x] USB 連線／配對／DDI preflight 按鈕與狀態列。
- [x] 電池／供電狀態顯示與低電警告（`BatteryIsCharging`、adapter W）。
- [x] usbmuxd／pymobiledevice3 配對與 trust 流程（preflight 內）。
- [x] rotation 用 `/codec` 的 `display.rotation`；crop 用 h265 caps 計算。
- [ ] diagnostic web：同一個 session 的 telemetry，不另開 stream。
- [ ] AAC-ELD 音訊解碼（Windows 需非 AudioToolbox 解碼器）。
- [ ] Python／sidecar 打包與子程序 Job Object 清理。
- [ ] USB backend 最終選擇：B（CoreDevice）為目前驗證路線；A 需簽章
  driver，暫不投入。
- [ ] （A 備案）libusb0 filter installer、`0xFD` 偵測、QuickTime demux。
- [x] `UsbObsController` Qt 控制器與 tray／主視窗入口。
- [ ] AirPlay 影片視窗共用 CleanOutputWindow（engine window-handle hook）。
- [ ] Virtual camera（Media Foundation）可行性 spike 與實作。
- [ ] 多台 USB 裝置的 UDID 選擇（目前 preflight 取第一台）。
- [ ] PD dock 實測：資料＋供電同時運作（需硬體）。
- [ ] `settings.json` 增加 `mode=airplay/usb`。
- [ ] Apple Mobile Device 依賴與安裝包策略。
- [ ] 測試矩陣：iPhone 型號、iOS 版本、DRM、長時間、USB 2/3。
