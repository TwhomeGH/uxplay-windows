# 待辦清單 TODO

> [!TIP]
> AirPlay 是預設與主線；USB 是 optional／experimental，預設 `off`。
> AirPlay／UX 改進依「可觀測性 → 生命週期 → 穩定度 → 功能 → 體驗」
> 分期推進；完成後把 `[ ]` 改成 `[x]`，並用 `changelogkit add`
> 留下變更紀錄。

## 持續規則 — 改哪裡，補哪裡的代碼註釋

每次修改程式時，一併補齊該函式／類別與直接相關介面的必要註釋；
舊程式依下方清單逐步補齊，不以一次性大量加註釋取代功能驗證。

- 新增或修改的公開 API 使用中文 Doxygen 註釋（`///` 或 `/** */`），
  說明用途、參數、回傳值、單位、未知值及必要的錯誤／副作用。
- 涉及執行緒、回呼、資源或快取時，說明呼叫執行緒、資料擁有權、有效期限、
  停止／回收順序，以及初始化或快取的原因；不要只逐行翻譯程式。
- 複雜邏輯、時限、節流、相容處理與 workaround 補上「為什麼」及適用限制。
  行為變更時同步修正舊註釋；不確定的保證列為 TODO，不寫成已驗證事實。
- 保留上游授權與出處；不為統一語言而重寫所有既有註釋，不改生成檔或第三方程式。
- 每項完成標準：核對實作及直接呼叫處、補齊該範圍契約與重要原因；涉及 API
  註釋時重建 Doxygen 並確認參數與連結。零警告不等於註釋完整，亦不等於實機通過。

### 既有程式補齊順序

- [ ] 優先：`src/logging.h/.cpp` — 背景佇列、容量限制、略過策略、flush／shutdown 與正常退出限制。
- [ ] 優先：`src/gpuinfo.h/.cpp` — 一次性硬體探測、快照有效期間與 GPU 變更後的刷新方式。
- [ ] 優先：`src/airplayworker.h/.cpp`、`src/mainwindow.cpp` 的啟停路徑 — 回呼執行緒、宿主生命週期與解除註冊順序。
- [ ] 接續：`src/settings.h/.cpp`、`src/diagnostics.h/.cpp` — 設定套用時機、快照、時間單位、匯出與去識別化限制。
- [ ] 接續：HUD／Runtime Debug 與 virtual camera 宿主整合 — 更新成本、視窗擁有權、跨程序資料契約及失敗處理。

引擎協定、音訊與 renderer 的註釋清單由 `libuxplay/TODO.md` 維護。

## Phase 1 — 可觀測性與基本操作

- [x] 擴充引擎 callback：`client_connected(name, model, deviceid)`、`client_disconnected`、`open_connections`。
- [x] 主視窗與 tray 顯示目前連線裝置名稱／型號，連線與斷線時通知。
- [x] 引擎日誌寫入檔案（rolling log），tray 加「Open Log」，主視窗顯示最後錯誤。
- [x] tray 新增 Start/Stop AirPlay；圖示與 tooltip 區分 idle／streaming／error
  （Phase 2A 已改為引擎熱啟停）。
- [x] 單一實例保護（QLockFile 或 named mutex），第二次啟動叫回既有視窗。
- [x] 以 Windows named mutex 取代 QLockFile，避免 stale lock 讓第二個 process 啟動。
- [x] Bonjour 服務改檢查執行狀態並嘗試自動啟動；修正 `ACCESS_DENIED` 被誤判為存在。

## Phase 2 — 生命週期與設定模型

- [x] 設定收斂為單一結構化來源（JSON/INI），命令列參數由設定產生，只留「額外參數」欄位。
- [x] 設定變更改為重啟引擎 worker，UI 顯示 pending 變更與單一「套用」動作。
- [x] 修正優雅停止路徑：`stop_uxplay()` 實際被呼叫；移除 `cleanup()` 內的 `exit(0)`。
- [x] 引擎崩潰重啟加入 backoff，並顯示「引擎連續失敗」狀態。

## Phase 2D — AirPlay 穩定度（主線）

- [ ] 1h／4h soak：記錄 CPU、RSS、連線數與最後錯誤。
- [ ] 20 次 reconnect（iPad 斷線／重連）無 freeze。
- [ ] 網路切換（Wi-Fi／Ethernet）後自動重啟引擎並 re-register mDNS。
- [ ] sleep／wake 後自動恢復 engine、Bonjour 與音訊。
- [ ] 音訊裝置變更／取樣率切換的 fallback。
- [ ] 健康狀態（RSS／CPU／last error）寫入 log 或 tray tooltip。
- [x] 診斷包：ring buffer、Save diagnostics、redaction、issue template。
- [x] 診斷包 `summary.txt`／`timeline.txt`；日誌 8MB×5。
- [x] 診斷匯出時間區間（上次連線／1h／5h／session／自訂）與資料夾預設、zip 可選。
- [x] `Native (device)` 解析度預設（2360x1640 實測 H.265）。
- [x] 新增 720p 解析度，補效能／codec 測試缺口。
- [x] Media Info HUD：點擊穿透疊層、主視窗／tray 開關、左上／右上。
- [ ] HUD 兩種顯示模式的長時間串流與視窗定位驗收；預設獨立 HudWindow，可選舊原生 HudOverlay，不使用 textoverlay。
- [x] FPS／frame interval／last frame age telemetry（HUD）。
- [x] HUD 欄位勾選（client／status／video／output／decoder／renderer／fps／audio）。
- [x] 影片 Decoder／Renderer 資訊回報。
- [x] Diagnostics Window：Overview／Video／Audio／Network／Events、Copy／Save。
- [x] 本機診斷檢視器（bundle／log、篩選、搜尋、metrics 圖）。
- [x] 本地化：en source、zh_TW `.ts`（228 keys）、tray 語言切換、fallback 英文。
- [x] `check_i18n.py`：缺 key／空白翻譯／placeholder／未包 `tr()` 的 UI 字串掃描。
- [x] 翻譯檢查接入 x64／arm64 CI；`--require-lupdate`、顏色輸出、lupdate 缺失降級警告。
- [x] 開發導覽 `docs/API-GUIDE.md`、Doxygen `Doxyfile`、`scripts/build_docs.ps1`。
- [x] VS Code tasks：i18n 更新／檢查、API docs、診斷檢視器。
- [x] Docs CI：Doxygen API 產生與 artifact；x64／arm64 build 已含 i18n gate。
- [x] PR／手動 Checks workflow：Linux 安裝 qt6-l10n-tools 後執行嚴格 i18n 檢查。
- [x] nightly build／publish 拆分：build 只上傳 artifact，publish 才改 tag／release。
- [x] publish 改成先建立新 release、成功後才 prune 的流程。
- [ ] 累積第 11 個 nightly 時驗證 prune 實際刪除與 tag cleanup。
- [x] 用 frame age 做 pipeline stall 偵測（以 input frame 区分暂停）；目前只記錄，不自動重建。
- [ ] 延遲與 A-V offset：video arrival delay、audio latency。
- [ ] `-s` 測試矩陣：720p／1080p／1440p／Native／4K 的實際尺寸、codec、CPU／GPU。
- [x] 凍結 watchdog：8 秒無 video packet 自動重建 mirror 連線。
- [x] tray `Rebuild AirPlay stream` 手動重建。
- [x] D3D11／D3D12 A/B：D3D11 仍只 presented 1 frame，先排除 sink 為唯一原因。
- [ ] 測試 `-vd avdec_h265` 軟體解碼；若有效，評估 H.265 預設或 fallback。
- [ ] 音訊軌道穩定性：log 出現大量 `Received empty resent audio packet`／
  `raop_buffer_handle_resends` 重送迴圈；需確認是 Wi-Fi 封包遺失、buffer 太小
  或重送超時（Audio 頁已加封包／重送計數）。
- [x] 引擎 debug log 降噪：過濾每包 RTP、`/feedback` 與 HTTP 細節；`--engine-trace` 保留完整輸出。
- [x] 移除「HEVC 自動停用 HUD」保護；H.264／H.265 都可能 frames=1，HUD 不是主因。
- [x] pipeline watchdog 改為保守門檻（20 秒、3 次取樣），且不自動重建或切換 codec。
- [x] 管線層 frame heartbeat（區分 packet 有到但 pipeline 卡住）。
- [ ] 讀取設定後套用解析度不需重啟 App（目前仍需重啟）。
- [ ] Native 依裝置 profile 自動填入（目前使用已設定的寬高）。
- [x] 即時診斷面板（codec／resolution／fps／audio／latency）。
- [ ] Latency／A-V offset instrumentation（sink／pipeline／first NTP）。

## Phase 3 — AirPlay 功能

- [ ] PIN／密碼／allow-block 存取控制 UI；`display_pin` 改以 callback 在 GUI 顯示 PIN。
- [ ] 音訊輸出裝置選擇（`-as`／WASAPI）。
- [ ] 曝露錄影（`-mp4`）、HLS（`-hls`）、`-fps`、翻轉／旋轉、overscan、`-reset`、`-nc`／`-nohold`。
- [x] 影片視窗置頂開關／記憶位置。
- [ ] 影片視窗縮到 tray；視窗標題在地化。

## Phase 4 — UI／在地化／維護

- [ ] 主視窗以 QGroupBox 分組並加入 scroll，修正固定 420×420 的擁擠版面。
- [ ] i18n（至少 zh_TW）；`windeployqt` 納入 Qt 翻譯。
- [ ] 更新檢查器（README 的 TODO 項目）。
- [ ] `make_release.sh` 補複製 `mDNSResponder.exe`／`dnssd.dll`，與 CI 打包一致。
- [ ] Release 策略：SemVer／tag、Core／Exp 分類、nightly commit SHA 與版本顯示；
  nightly 已改為唯一 tag 並保留最近 10 個。
- [ ] 移除未使用的 `m_autostartAction`／`m_statusAction`。

## 已完成

- [x] 接入 changelogkit（`.changelog.toml`、`docs/ChangeHistory.md`）。
- [x] 建立根目錄 `TODO.md` 並整理 AirPlay 改進分期。
- [x] 本地建置／除錯腳本與開發者指南
  （`scripts/dev_build.sh`、`scripts/dev_run.sh`、`docs/DEVELOPERS-GUIDE.md`）。
- [x] Phase 2A：引擎熱啟停、ready callback、崩潰 backoff、`--start`／`--stop` IPC。
- [x] MSI 固定防火牆規則（WiX Firewall extension）與清理舊 build 規則腳本。

## Phase 5 - USB 擷取（探索）

> 可選模式；不阻塞 AirPlay 主線，預設 `obs.mode=off`。

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
- [x] Virtual camera（Media Foundation）可行性 spike 與實作：官方 sample
      已建置、註冊並由 ffmpeg 擷取；目前以 named pipe 接收測試 NV12
      畫面（`virtualcam/`）。
- [ ] AirPlay `appsink` → virtual camera 的 iPad 端到端驗證與 MSI 註冊。
- [ ] 多台 USB 裝置的 UDID 選擇（目前 preflight 取第一台）。
- [x] 採購與接線建議（PD dock／USB-A 桌機）。
- [ ] PD dock 實測：資料＋供電同時運作（需硬體）。
- [ ] `settings.json` 增加 `mode=airplay/usb`。
- [ ] Apple Mobile Device 依賴與安裝包策略。
- [ ] 測試矩陣：iPhone 型號、iOS 版本、DRM、長時間、USB 2/3。
