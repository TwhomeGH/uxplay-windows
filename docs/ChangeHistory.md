# ChangeHistory

## 2026.11.13 00:12 dev build 先停止舊 instance

**類型**: 修正 · **檔案**: `scripts/dev_build.sh、docs/DEVELOPERS-GUIDE.md`

### 問題 - dev_build 先連結 exe，舊 process 仍鎖檔，造成 Permission denied

### 根因 - 自動停止只加在 dev_run，build 階段沒有停舊 process

### 修改 - dev_build 在 configure/build 前停止同路徑 dev instance

實測 build + run 成功，PID 更新。

- 仍只比對 build-tree exe，不影響安裝版

**相關文件**: scripts/dev_build.sh

---

## 2026.11.12 23:58 修正 release 缺少 textoverlay plugin

**類型**: 修正 · **檔案**: `stuff/dll.txt、make_release_CI.sh、make_release_CI_arm64.sh`

### 問題 - nightly zip 有 pango DLL，但沒有 libgstpango.dll，安裝版 textoverlay 無法使用

### 根因 - stuff/dll.txt 沒有列 libgstpango.dll

### 修改 - dll.txt 加入 /ucrt64/lib/gstreamer-1.0/libgstpango.dll

現有 nightly 已下載驗證缺 plugin；重新 push 讓 CI 重建。

- x64／arm64 release smoke test 加入 textoverlay

**相關文件**: stuff/dll.txt

---

## 2026.11.12 23:37 精簡 HUD 文字

**類型**: 改善 · **檔案**: `src/mainwindow.cpp`

### 問題 - Audio Format 等欄位太長，HUD 佔滿畫面

### 根因 - HUD 直接使用完整 Media Info 字串

### 修改 - HUD 改為 Video／Output／Decoder／Renderer／Audio 精簡欄位

建置通過；待連線確認畫面寬度。

- Audio 只顯示 codec、ct、spf；完整格式留在托盤／diagnostics
- Output 與 requested 合併成一行

**相關文件**: src/mainwindow.cpp

---

## 2026.11.12 23:37 dev run 自動停止舊 instance

**類型**: 改善 · **檔案**: `scripts/dev_run.sh、scripts/dev_build.sh、docs/DEVELOPERS-GUIDE.md`

### 問題 - 每次重跑 dev build 都要手動停舊 uxplay-windows，否則檔案被鎖或連到舊版本

### 根因 - run-dev 與 dev_run 直接啟動，沒有先清理同路徑 process

### 修改 - dev_run.sh 啟動前停止同 dev exe 路徑的舊 instance

實測 PID 47540 → 55484，新 build 正常 ready。

- run-dev.cmd 產生器加入同樣的 path-aware stop
- 不影響 Program Files 的正式安裝版

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 23:34 修正 HUD 多行文字靠左排列

**類型**: 修正 · **檔案**: `libuxplay/renderers/video_renderer.c`

### 問題 - HUD 多行文字每一行仍置中，沒有靠左

### 根因 - textoverlay 的 line-alignment 預設 center，只設 halignment=left 不夠

### 修改 - 設定 line-alignment=left；右上／右下時設 right

建置通過；待連線確認多行文字左緣對齊。

**相關文件**: libuxplay/renderers/video_renderer.c

---

## 2026.11.12 23:29 修正 textoverlay enum 屬性設定

**類型**: 修正 · **檔案**: `libuxplay/renderers/video_renderer.c`

### 問題 - GLib 警告 halignment/valignment 值為負，HUD 位置無效

### 根因 - g_object_set varargs 把字串當成 enum 整數傳入

### 修改 - 改用 gst_util_set_object_arg 設定 halignment/valignment

建置通過；待連線確認四角位置且 console 無 GObject-CRITICAL。

- 快取 HUD 文字與位置，避免每 250ms 重設相同屬性
- 重建 pipeline 時清除快取，確保新 overlay 重新套用

**相關文件**: libuxplay/renderers/video_renderer.c

---

## 2026.11.12 22:56 HUD 四角位置與無 codec 重建

**類型**: 修正 · **檔案**: `libuxplay/lib/raop_rtp_mirror.c、libuxplay/renderers/video_renderer.c、libuxplay/renderers/video_renderer.h、libuxplay/uxplay.cpp、libuxplay/uxplay_api.h、src/mainwindow.h、src/mainwindow.cpp、src/settings.cpp、docs/DEVELOPERS-GUIDE.md`

### 問題 - HUD 文字位置只能左上／右上；reconnect 常常只出聲、沒有 video format

### 根因 - textoverlay 只設定左右；mirror thread 沒有 codec 起始 timeout

### 修改 - HUD 支援四角：top-left／top-right／bottom-left／bottom-right

建置通過；待 iPad 驗證四角位置與 audio-only reconnect 是否自動恢復。

- set_uxplay_hud_text 增加 vertical 參數，對應 valignment
- 6 秒內沒有 video codec 時重建 mirror 連線

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 22:38 HUD 改用 GStreamer textoverlay

**類型**: 修正 · **檔案**: `libuxplay/renderers/video_renderer.c、libuxplay/renderers/video_renderer.h、libuxplay/uxplay_api.h、libuxplay/uxplay.cpp、src/mainwindow.cpp、src/mainwindow.h、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md`

### 問題 - 獨立 HUD 閃爍、位置亂飄；SetParent 成 child 後 Qt geometry 與 Windows 衝突

### 根因 - Qt top-level widget 當成 foreign Win32 child，D3D 疊層不穩

### 修改 - mirror pipeline 加入 textoverlay name=hud_overlay

建置通過；待 iPad 連線確認文字出現在影片左上／右上。

- engine API set_uxplay_hud_text；GUI 直接更新 overlay 文字
- 移除獨立 MediaInfoHud 視窗與 child 定位
- tray 左上／右上改對應 halignment

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 22:23 HUD 改為影片視窗子視窗

**類型**: 修正 · **檔案**: `src/mediainfohud.h、src/mediainfohud.cpp、src/mainwindow.cpp`

### 問題 - HUD 與影片視窗是兩個獨立 top-level window，拖動時靠輪詢追

### 根因 - HUD 建立為 Qt::Tool 獨立視窗，只用 SetWindowPos 定位

### 修改 - 以 SetParent 將 HUD 掛成影片視窗的 Win32 child

待 iPad 連線實測 d3d11/d3d12 sink 是否會蓋掉 child；若蓋掉改用 textoverlay。

- 拖動時由 Windows 自動跟隨；縮放仍由定位更新
- 停止／斷線／停用 HUD 時 detach，避免隨父視窗銷毀
- SetParent 失敗時回退獨立視窗定位

**相關文件**: src/mediainfohud.cpp

---

## 2026.11.12 20:22 加入 AirPlay 凍結 watchdog 與重建

**類型**: 修正 · **檔案**: `libuxplay/lib/raop_rtp_mirror.c、src/mainwindow.cpp、src/mainwindow.h、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - 投屏畫面定格但聲音仍在，沒有自動重建機制

### 根因 - mirror thread 持續執行，但 client 停止送 video packet；沒有 timeout

### 修改 - engine 在 codec 已設定後 8 秒無 video packet 時重建 mirror 連線

建置通過；待實機重現定格驗證 8 秒重建 log。

- video_stream_suspended 期間不觸發，避免誤判暫停
- tray 新增 Rebuild AirPlay stream，手動重啟 engine
- 文件記錄 watchdog 與手動重建

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 20:13 顯示實際與請求解析度

**類型**: 功能 · **檔案**: `src/settings.h、src/settings.cpp、src/mainwindow.h、src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md`

### 問題 - 無法確定目前實際套用的串流寬高，Native 也被 preset 覆寫成 1280x720

### 根因 - 只有設定值，沒有 requested/actual 對照；Native 與 preset 共用 width/height

### 修改 - Media Info HUD 與主視窗顯示 Output／Requested 寬高

主視窗實測 Native 2360x1640，Output waiting (requested 2360x1640)。

- Native 使用獨立 native_width／native_height，preset 不再覆寫
- Custom 編輯後切回 Native 可更新 Native 尺寸

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 20:05 修正 HUD 跟隨影片視窗

**類型**: 修正 · **檔案**: `src/mainwindow.cpp、src/mediainfohud.cpp`

### 問題 - HUD 位置不會即時跟隨影片視窗，拖動後才突然移動

### 根因 - 列舉到隱藏的 GStreamer renderer 視窗；1 秒輪詢＋Qt move 對透明 tool window 有延遲

### 修改 - 只選可見且尺寸合理的 renderer 視窗，優先 AirPlay Video Stream

待 iPad 重連驗證：HUD 是否持續跟著拖動與縮放。

- HUD 改用原生 SetWindowPos 跟隨，輪詢降到 250ms

**相關文件**: src/mainwindow.cpp

---

## 2026.11.12 05:02 新增 Media Info HUD 疊層

**類型**: 功能 · **檔案**: `src/mediainfohud.h、src/mediainfohud.cpp、src/mainwindow.h、src/mainwindow.cpp、src/settings.h、src/settings.cpp、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - Media Info 只能在托盤查看，觀察時要一直找選單

### 根因 - 沒有影片視窗上的即時資訊層

### 修改 - 新增 MediaInfoHud：半透明、always-on-top、點擊穿透

主視窗 checkbox 已驗證；疊層需 iPad 連線後實測位置與穿透。

- 主視窗 checkbox 與 tray 開關，tray 可選左上／右上
- 顯示 Client／Status／Video／Decoder／Renderer／Audio
- settings.json 新增 hud.enabled／hud.corner，預設關閉

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 04:59 記住 AirPlay 影片視窗幾何

**類型**: 功能 · **檔案**: `src/settings.h、src/settings.cpp、src/mainwindow.h、src/mainwindow.cpp`

### 問題 - 每次重連 AirPlay 視窗都回到預設大小，使用者要重新拖動縮放

### 根因 - GStreamer 建立原生 HWND，GUI 只改標題，沒有幾何記憶

### 修改 - settings.json 新增 video_window（x／y／width／height／remember）

待 iPad 重連驗證：視窗比例、拖動後 settings 寫入、重連還原。

- 第一次依串流比例 fit；之後套用上次幾何
- 1 秒輪詢視窗位置大小，變更即儲存；離開螢幕時重新 fit

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 04:45 新增 720p 解析度與 no-display 實驗結果

**類型**: 功能 · **檔案**: `src/settings.cpp、src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - -s 只有 Native／1080p／1440p／4K，缺少 720p 低負載測試；no -s 行為未知

### 根因 - 解析度選單沒有 720p；省略 displays 的 client 行為尚未驗證

### 修改 - 新增 720p（1280x720）選項與 engine args

720p 可補低負載測試；no-display 實驗已還原，未進主線。

- 修正 updateResolutionControls 的 1080p／1440p 尺寸對應
- 記錄 omit displays 會讓 iPad 連上後立即斷線

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 04:28 補 h265 無 -s 的明確 log

**類型**: 修正 · **檔案**: `libuxplay/uxplay.cpp`

### 問題 - 直接跑 h265 而沒有 -s 時，只會默默套用 1080p 預設

### 根因 - 預設解析度與 HEVC capability 的關係沒有在 log 說明

### 修改 - 無 -s 且 h265 時記錄 1920x1080 與 H.264 預期

GUI 的 Native 仍送 -s 2360x1640@60，不會走這條路徑。

- 提示 HEVC 需要 -s height>1080

**相關文件**: libuxplay/uxplay.cpp

---

## 2026.11.12 04:22 修正 h265 不再自動變成 4K

**類型**: 修正 · **檔案**: `libuxplay/uxplay.cpp、libuxplay/README.txt、docs/DEVELOPERS-GUIDE.md`

### 問題 - 沒有 -s 時 h265 會把預設解析度改成 4K，造成 Auto 意外的 3108x2160

### 根因 - 把 codec capability 和 resolution default 綁在一起

### 修改 - h265 只宣告 HEVC；無 -s 時預設固定 1920x1080

GUI 啟動 args 為 -s 2360x1640@60；實測 H.265 2360x1640；自動 4K fallback 已移除。

- HEVC 必須明確請求 height >1080；GUI Native 預設 2360x1640
- 更新 -h 說明與 libuxplay README

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 04:15 移除 Auto，Native 成為預設解析度

**類型**: 修正 · **檔案**: `src/settings.h、src/settings.cpp、src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md`

### 問題 - Auto 實質是不送 -s，HEVC 下會變 4K，和「自動選原生」的預期相反

### 根因 - 解析度選單用 Auto 命名一個 client default 行為

### 修改 - 移除 Auto 選項；Native (device) 成為預設 2360x1640

啟動 args 為 -s 2360x1640@60；settings mode=native；HEVC 實際 2360x1640。

- 舊 auto 設定遷移為 native
- 文件改為 Native 預設與 Custom 調整方式

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 04:11 新增 Native 原生解析度模式

**類型**: 功能 · **檔案**: `src/settings.cpp、src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - Auto 在 HEVC 下請求 4K，實際變成 3108x2160，不是裝置原生尺寸

### 根因 - AirPlay 沒有查詢原生解析度的欄位；Auto 只代表不送 -s

### 修改 - 新增 Native (device) 模式，送出設定的 width x height

實測 iPad13,18：-s 2360x1640@60 得到 h265 2360x1640；UI 顯示 Native (device)。

- Auto 標示為 client default；Native 停用寬高欄位
- 文件說明 Native 是設定請求，不是自動查詢

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:56 釐清 HEVC 只在超過 1080p 時啟用

**類型**: 修正 · **檔案**: `src/mainwindow.cpp、src/mainwindow.h、docs/DEVELOPERS-GUIDE.md`

### 問題 - 預設 HEVC 但 1080p 實際顯示 H.264，UI 容易誤解

### 根因 - AirPlay client 依請求高度選擇 codec；height<=1080 時送 H.264，server 無法強制

### 修改 - Video Codec 選項標示 needs >1080p，並加動態提示

實測 Media Info：H.264、Stream Size 1554x1080，log 有 `-h265`；
UxPlay README 1149-1153 與 issue #233 證實需 height>1080。
Audio Codec 現在為 AAC-ELD。

- 選 HEVC 但解析度不足時，tray 提示將使用 H.264
- 文件補充 Apple client 行為與 4K／1440p 條件

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:52 新增 HEVC／H.264 視訊編碼偏好

**類型**: 功能 · **檔案**: `src/settings.cpp、src/settings.h、src/mainwindow.cpp、src/mainwindow.h、src/diagnostics.cpp、docs/DEVELOPERS-GUIDE.md`

### 問題 - HEVC 只能透過 extra_args 的 -h265，沒有 UI，也無法表達 H.264 偏好

### 根因 - video codec 沒有結構化設定

### 修改 - settings.json 新增 video.codec=hevc／h264，預設 hevc

實測 h264：args 無 -h265；hevc：args 有 -h265；畫面顯示 HEVC / H.265 (default)。

- 主視窗 Video Codec 下拉選單；變更提示重啟
- engineArguments 管理 -h265；extra_args 不再保留
- diagnostics 的 settings.redacted 顯示 video.codec

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:46 修正 audio codec 標示

**類型**: 修正 · **檔案**: `libuxplay/uxplay.cpp`

### 問題 - Audio Codec 把 ALAC 與 AAC-ELD 對調，且缺少 AAC-LC／PCM

### 根因 - audio_get_format 的 ct 標籤映射錯誤

### 修改 - ct=2 → ALAC、ct=8 → AAC-ELD、ct=4 → AAC-LC、ct=1 → PCM

以 audio_renderer／raop_rtp 的定義為準；下次 ct=8 應顯示 AAC-ELD，而非 ALAC。

- unknown ct 顯示實際 ct 值，不再只寫 Unknown

**相關文件**: libuxplay/uxplay.cpp

---

## 2026.11.12 03:42 限制診斷包保留數量

**類型**: 修正 · **檔案**: `src/diagnostics.cpp、docs/DEVELOPERS-GUIDE.md`

### 問題 - 每次 Save Diagnostics 都累積 folder／zip，磁碟可能無限增長

### 根因 - 沒有診斷包的 retention policy

### 修改 - 每次匯出後只保留最近 10 組 folder／zip

實測 15 組 → 10 組；log 顯示 Pruned old diagnostics entries: 6。

- bundle timestamp 加入毫秒，避免同秒覆蓋
- 記憶體 3000 events／1800 metrics、日誌 2MB × 3 的上限維持不變

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:15 新增日誌選單與診斷資料夾入口

**類型**: 功能 · **檔案**: `src/logging.cpp、src/logging.h、src/mainwindow.cpp、src/mainwindow.h、docs/DEVELOPERS-GUIDE.md`

### 問題 - Open Log 固定用系統關聯開啟，且沒有診斷資料夾入口

### 根因 - 只有單一 QDesktopServices 開啟，沒有 Open With 與資料夾動作

### 修改 - Open Log 改為日誌選單與資料夾入口

實測 UI：Open Log 有下拉箭頭；USB not started；重啟無 USB 通知。

- Open Log 選單：Open Log／Open Log With...／Open Log Folder／
  Open Diagnostics Folder
- tray 新增 Logs & Diagnostics 子選單
- Open Log 使用 InstantPopup，點擊即展開選單
- Open Log With 使用 rundll32 OpenAs_RunDLL；診斷資料夾不存在時建立

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:08 USB 不再隨啟動自動 preflight

**類型**: 修正 · **檔案**: `src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md`

### 問題 - obs.mode=window 每次重啟自動偵測 USB，失敗時反覆發通知

### 根因 - 把輸出模式偏好當成 capture 啟動指令

### 修改 - 移除啟動時自動 startUsbObs；只在 Connect／選 Clean window／--usb-obs 啟動

重啟後 log 無 usb-device／Clean Output；usb_procs=0；obs.mode=window 保留為偏好。

- tray toggle 依實際 running 判斷，不再把偏好當成執行中
- UI 顯示 USB: not started (press Connect)；文件更新

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 03:05 加入診斷包與近期診斷記錄

**類型**: 功能 · **檔案**: `src/diagnostics.cpp、src/diagnostics.h、src/mainwindow.cpp、src/mainwindow.h、src/settings.cpp、src/settings.h、src/main.cpp、CMakeLists.txt、.github/ISSUE_TEMPLATE/bug_report.yml、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - 異常發生時只能靠長 soak 或使用者描述，沒有可交付的近期證據

### 根因 - 缺少結構化事件／指標 ring buffer 與診斷 bundle

### 修改 - DiagnosticsRecorder 保留最近 30 分鐘事件與 1Hz 指標

實測 zip 890KB；manifest app_version `edbd077-dirty`、GStreamer 1.28.3；
redaction scan 無 agp05／UDID／MAC／display name；YAML parse ok。

- Save Diagnostics 產生 zip：manifest／events／metrics／logs／settings／system
- redact 裝置名稱、UDID、MAC、IPv4 與使用者路徑
- tray／主視窗／--save-diagnostics 入口與 Keep diagnostics 開關
- 新增 GitHub bug report issue template

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 01:48 新增 AirPlay soak 監測腳本

**類型**: 工具 · **檔案**: `scripts/airplay_soak.ps1、docs/DEVELOPERS-GUIDE.md`

### 問題 - AirPlay 穩定度沒有可重複的長時間監測

### 根因 - 只靠肉眼與 log，沒有 RSS／CPU 趨勢

### 修改 - 新增 PowerShell soak 腳本，取樣 RSS／CPU／threads／handles 與 log error

1 分鐘自測：6 samples、RSS 392.3→392.5MB、CPU 全核平均 0.8%、0 errors；60 分鐘監測已啟動。

- 輸出 CSV 與 report 到 build/diagnostics
- 文件補執行方式與判讀方式

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 01:44 定位 AirPlay 主線與 USB 可選

**類型**: 文件 · **檔案**: `TODO.md`

### 問題 - USB 探索容易發散，需要固定產品優先順序

### 根因 - USB 供電與驅動限制多；AirPlay 才是主要使用路徑

### 修改 - TODO 加註 AirPlay 預設／主線、USB optional／experimental

USB 保持 obs.mode=off 預設，不阻塞 AirPlay。

- 新增 Phase 2D AirPlay 穩定度：soak、reconnect、網路、sleep、音訊、健康狀態

**相關文件**: TODO.md

---

## 2026.11.12 01:18 補充 USB 模式採購與接線建議

**類型**: 文件 · **檔案**: `docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md、TODO.md`

### 問題 - USB 模式需要同時供電，但沒有採購與線材指引

### 根因 - 一般 USB 分線器易被誤解；PC 只有 USB-A 時 C-C 線不能直接接

### 修改 - 新增採購清單：downstream 供電 hub、30W PD、A-C／C-C 線

驗證以 app 狀態列 charging 且瓦數大於 10W 為準。

- 說明被動 Y 線、無電源 hub、只往 upstream 供電 dock 的風險
- 補桌機只有 USB-A 的接法與驗證方式
- 釐清供電不足是 USB-A 埠 500mA 上限，不是線材只有傳輸功能
- 說明 AI Charging／BC 1.2 是 5V 增流，不是 PD

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 01:09 顯示 iPad 電池與 USB 供電狀態

**類型**: 功能 · **檔案**: `src/usbdeviceconnector.cpp、src/usbdeviceconnector.h、src/mainwindow.cpp、docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md、TODO.md`

### 問題 - USB 模式會邊用邊掉電，但 app 沒有揭露供電不足

### 根因 - PC USB 埠只給 5V／0.5A／3W，app 沒有讀電池與 adapter 資訊

### 修改 - preflight 讀 com.apple.mobile.battery 與 diagnostics battery single

實測 `AdapterDetails` 5000mV／500mA／3W、`Amperage` -1976mA、
`BatteryIsCharging=false`；停止 live 只降低負載，不恢復充電；
preflight 顯示
`USB ready: iPad · 50% · not charging · 3W adapter`。

- 狀態列顯示電量、是否充電、adapter 瓦數；未充電顯示橘色
- tray 低電／未充電警告，明示 USB 佔用唯一埠
- 明示 USB 佔用唯一埠：停止串流只降低負載，不會恢復充電
- 文件補供電拓撲與 PD dock 注意事項

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 00:53 加入 USB 連線與配對入口

**類型**: 功能 · **檔案**: `src/usbdeviceconnector.cpp、src/usbdeviceconnector.h、src/usbpython.cpp、src/usbpython.h、src/mainwindow.cpp、src/mainwindow.h、src/usbobscontroller.cpp、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md、TODO.md`

### 問題 - OBS 模式有了，但沒有偵測／配對／掛 DDI 的 USB 入口

### 根因 - USB 模式直接啟動 sidecar，失敗原因只留在 log

### 修改 - 新增 UsbDeviceConnector preflight：usbmux → lockdown pair → amfi → mounter

實測 00:51:09 開始檢查，00:51:17 USB ready，00:51:22 串流開始；狀態列顯示 USB ready: iPad。

- 主視窗 USB 狀態列與 Connect／Reconnect 按鈕
- window 模式先 preflight，ready 才啟動 Clean Output
- 共用 UsbPython 解析 UXPLAY_USB_PYTHON

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 00:43 規範 OBS 輸出模式設定

**類型**: 功能 · **檔案**: `src/settings.cpp、src/settings.h、src/mainwindow.cpp、src/mainwindow.h、docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md、TODO.md`

### 問題 - OBS 輸出只有 --usb-obs 隱藏開關，使用者無法在設定中選擇

### 根因 - 沒有持久化的模式 schema，start/stop 與設定狀態不一致

### 修改 - settings.json 新增 obs.mode（off／window；camera 保留）

實測 `--usb-obs` 寫入 `obs.mode=window` 並自動啟動；關閉 clean
window 後回到 `off`、USB 子程序停止；正常啟動預設不啟動 USB。

- 主視窗 OBS output group 與下拉選單；預設 off
- 選 window 啟動、off 停止；關閉 clean window 回 off
- --usb-obs 選擇 window 並持久化

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.12 00:32 Clean Output 支援拖曳與縮放

**類型**: 修正 · **檔案**: `src/cleanoutputwindow.cpp、src/cleanoutputwindow.h、usbcapture/spike/README.md、TODO.md`

### 問題 - Clean Output 無邊框但無法移動或縮放

### 根因 - frameless 視窗沒有標題列與系統 resize 邊框

### 修改 - 左鍵拖曳呼叫 startSystemMove

實測視窗 (338,108) → (458,168)；大小 1243x864 → 1363x914。

- 8 px 邊緣呼叫 startSystemResize
- 右鍵選單補 Move／Resize fallback

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.12 00:24 實作 USB Clean Output 視窗與 --usb-obs

**類型**: 功能 · **檔案**: `src/cleanoutputwindow.cpp、src/cleanoutputwindow.h、src/usbobscontroller.cpp、src/usbobscontroller.h、src/mainwindow.cpp、src/mainwindow.h、src/main.cpp、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md、usbcapture/spike/README.md、TODO.md`

### 問題 - USB 畫面只能在 browser viewer 看，不能當 OBS 來源

### 根因 - browser viewer 有 chrome 與 device frame，且沒有原生輸出視窗

### 修改 - 新增 frameless Clean Output 視窗與固定標題

實測視窗標題正確、60 fps、無外框；OBS Window Capture 可直接選。音訊尚未解碼。

- UsbObsController 啟動 sidecar／bridge／GStreamer
- rotation 用 /codec、crop 用 h265 caps
- --usb-obs、tray、主視窗入口與 IPC
- 修正 GStreamer bus 跨執行緒 QTimer

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.12 00:10 實測 USB CoreDevice 到 GStreamer 的 bridge 與 rotation

**類型**: 研究 · **檔案**: `usbcapture/spike/README.md、TODO.md`

### 問題 - browser viewer 不能作為 OBS 輸出，rotation 也未套用

### 根因 - raw HEVC 是 1648x2384 portrait，display 1640x2360，rotation -90

### 修改 - bridge 實測 240+ access units，live d3d11videosink 播放成功

iPad 重插後 usbmux 恢復；PnP present 但 idevice_id 空時，重插可 reset mux。Clean Output 尚未實作。

- videoflip counterclockwise 讓 UI 正立
- videocrop right=8 bottom=12 還原 1640x2360
- 更新 spike README 與 TODO

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.12 00:02 加入 USB CoreDevice 串流到 GStreamer 的 bridge 原型

**類型**: 研究 · **檔案**: `usbcapture/spike/tools/stream_to_tcp.py、usbcapture/spike/README.md、TODO.md`

### 問題 - Clean Output 需要原生 video，而不是 browser viewer

### 根因 - browser viewer 有 device frame、Windows 無音訊，rotation 又依賴事件時序

### 修改 - 新增 /stream.bin → Annex-B TCP bridge

bridge 尚未實測，iPad 目前未連接；GStreamer 外掛檢查通過。

- 從 /codec hvcC 取 VPS/SPS/PPS
- GStreamer tcpclientsrc → h265parse → avdec_h265 管道
- rotation 以 display.rotation 為準，clean output 不畫外框

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.11 23:46 完成 USB 擷取 spike：QuickTime 不可行、CoreDevice 可用

**類型**: 研究 · **檔案**: `usbcapture/spike/README.md、usbcapture/spike/tools、TODO.md`

### 問題 - Windows 上沒有可用的 USB 擷取路徑

### 根因 - Apple UMDF filter 阻擋 user mode；iOS 27 QT subclass 改為 0xFD

### 修改 - 記錄四組 USB config 與 QT interface ff/fd

5 秒影片 1646 packets／1.8 MB，HEVC Main 1648x2384，ffmpeg 與
GStreamer 均可解碼；5 秒音訊 504 packets，PT=101 AAC-ELD。
QuickTime 路徑需 libusb0 upper filter 與簽章 driver；CoreDevice 需
Developer Mode + DDI（iOS 27+）。

- 驗證 Apple filter 拒絕 WinUSB 開啟（ERROR_NOT_SUPPORTED）
- 以 CoreDevice display 擷取 5 秒 HEVC 與 AAC-ELD
- 加入 pyusb／WinUSB／RTP 工具

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.11 23:33 加入 quicktime_video_hack Windows patch 與擷取結果

**類型**: 維護 · **檔案**: `usbcapture/spike/README.md、usbcapture/spike/capture.ps1、usbcapture/spike/patches/quicktime_video_hack-windows.patch`

### 問題 - quicktime_video_hack 在 Windows 無法建置，也無法直接做 live QuickTime 擷取

### 根因 - gstadapter 依賴 Linux-only GStreamer；Windows 無 Unix usbmuxd socket

### 修改 - 移除 gstadapter 與 GStreamer 命令，保留 AVFileWriter 的 record 路徑

patched build 成功；實際 `record` 回報 `libusb: i/o error` 與
`dial unix /var/run/usbmuxd`。iPad 已配對成功
（`00008101-000531C2229B001E` / iOS 27.3）；`pymobiledevice3` 只有
DVT screenshot，沒有 live stream。結論：Windows 需另寫 backend，
或改用 Linux/macOS 做協議 spike。

- 以 CGO + libusb 成功建置 patched tool
- spike capture 改用 record，並補 UCRT64 PATH

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.11 23:14 修正 USB 擷取依賴並加入 spike 腳本

**類型**: 維護 · **檔案**: `usbcapture/CMakeLists.txt、usbcapture/spike/README.md、usbcapture/spike/check_env.ps1、usbcapture/spike/capture.ps1、usbcapture/spike/.gitignore`

### 問題 - USB spike 的 libusbmuxd pkg-config 名稱錯誤，且 Windows quicktime_video_hack 無法建置

### 根因 - MSYS2 模組是 libusbmuxd-2.0；quicktime_video_hack 依賴 Linux-only gst

### 修改 - CMake 改用 libusbmuxd-2.0；ENABLE_USB_CAPTURE=ON 可完整建置 usbcapture

`libimobiledevice` 1.3.0／`libusbmuxd` 2.1.1 已安裝；Apple Mobile Device
Service running；iPad 尚未被 `idevice_id` 看到（需解鎖／信任）；
`quicktime_video_hack` 在 Windows 因 gst build constraints 失敗，
README 建議 `pymobiledevice3` 或 Linux/macOS。

- 新增 spike 的環境檢查、擷取腳本與 Windows 限制說明

**相關文件**: usbcapture/spike/README.md

---

## 2026.11.11 23:07 建立 usbcapture 骨架與 USB 擷取開關

**類型**: 維護 · **檔案**: `usbcapture/CMakeLists.txt、usbcapture/README.md、usbcapture/include/usbcapture/capture.h、usbcapture/include/usbcapture/device.h、usbcapture/include/usbcapture/session.h、usbcapture/src/capture.cpp、usbcapture/src/device.cpp、usbcapture/src/session.cpp、usbcapture/src/usbmuxd_client.cpp、usbcapture/src/quicktime_session.cpp、usbcapture/src/pipeline.cpp、usbcapture/tests/README.md、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md、TODO.md`

### 問題 - USB 模式沒有獨立邊界，容易和 AirPlay engine 混在一起

### 根因 - AirPlay 走 IP/mDNS，USB 走 QuickTime/usbmuxd，是不同協定

### 修改 - 新增 top-level usbcapture library、公開 API、README 與測試計畫

`isBackendAvailable()` 回 false、`createSession()` 回 nullptr；預設 dev
build 成功；`usbcapture` 原始碼在 UCRT64 以 `-fsyntax-only` 通過；
`libimobiledevice`／`libusbmuxd` 尚未安裝，`ENABLE_USB_CAPTURE=ON`
會停在 pkg-config。

- CMake 加 ENABLE_USB_CAPTURE（預設 OFF），AirPlay-only build 不受影響
- TODO 新增 Phase 5 USB spike 項目

**相關文件**: usbcapture/README.md

---

## 2026.11.11 21:42 修正 arm64 nightly 的 pacman 下載逾時

**類型**: 修正 · **檔案**: `.github/workflows/build-uxplay-windows-arm64.yml、.github/workflows/build_uxplay-windows-v2.yml、docs/DEVELOPERS-GUIDE.md`

### 問題 - arm64 nightly 在 install build dependencies 失敗，後續 release 步驟全被跳過

### 根因 - pacman 從 ftp2.osuosl.org 下載 clang-aarch64 套件時低於 1 B/s，10 秒後逾時

### 修改 - 固定 mirror.msys2.org，pacman 加 --disable-download-timeout 並重試 3 次

run 43030360402 在 13:36:43 出現 `failed retrieving ... Operation too slow`；
x64 run 43030360409 成功並已顯示台北時間。推送後重跑 nightly 驗證 arm64。

- x64 套用同樣的 mirror／重試設定

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 21:34 Nightly release 改用台北時間與 AM/PM

**類型**: 改進 · **檔案**: `.github/workflows/build_uxplay-windows-v2.yml、.github/workflows/build-uxplay-windows-arm64.yml、docs/DEVELOPERS-GUIDE.md`

### 問題 - Nightly release 的 Build time 是 UTC 24 小時制

### 根因 - workflow 用 Get-Date().ToUniversalTime() 並固定 UTC

### 修改 - 改用 Taipei Standard Time、12 小時制 AM/PM，並標註 UTC+8/Asia/Taipei

本機驗證 UTC 13:33:22 → 2026-11-11 09:33:22 PM；推送後 nightly x64/arm64 會重建 release。

- release 標題改為 `Nightly Build <sha> - <time>`

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 18:45 修正 VS Code F5 的 0xc0000135

**類型**: 修正 · **檔案**: `.vscode/launch.json、scripts/dev_build.sh、docs/DEVELOPERS-GUIDE.md`

### 問題 - VS Code F5 以 gdb 啟動 dev exe 時，程式以 0xc0000135 退出

### 根因 - launch 的 PATH 未帶進 inferior；dev build 未自帶 DLL 與 plugins

### 修改 - dev_build.sh 把 UCRT64 DLL、qt.conf、GStreamer plugins 放進 build/dev

先前 F5 的 debug console 顯示 gdb 啟動後 app 以 0xc0000135
退出；修正後 `build/dev` 有 349 個 DLL、`qt.conf` 與
`lib/gstreamer-1.0`。F5 已能啟動 app，log 於 18:43:26 出現
`Starting UxPlay engine`；gdb 也已在 `src/main.cpp:34` 成功停下。
VS Code 的停止/重啟會讓最後的 process 檢查為 0。

- launch.json 改用固定 UCRT64 路徑，並用 setupCommands 設定 inferior 環境

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 18:31 新增 VS Code F5 偵錯設定

**類型**: 維護 · **檔案**: `.vscode/launch.json、.vscode/tasks.json、.vscode/settings.json、.vscode/c_cpp_properties.json、docs/DEVELOPERS-GUIDE.md、scripts/dev_build.sh`

### 問題 - VS Code 沒有 launch/tasks，F5 不能直接建置並用 gdb 跑 dev 產物

### 根因 - 原本只有 run-dev.cmd 與 dev_run.sh，UCRT64 gdb 也未安裝

### 修改 - 新增 build-dev task 與 gdb launch config，環境與 run-dev.cmd 一致

安裝 UCRT64 gdb 17.2。以 gdb batch 在 `src/main.cpp:34` 設中斷點，成功停在
`qMain` 內的 `isUxPlayProcess`；bt 為 `isUxPlayProcess` → `qMain` →
`qtEntryPoint`。task 的 `MSYSTEM=UCRT64` 命令完成 configure 與 57 個 target；
四個 `.vscode` JSON 均可解析。VS Code F5 由使用者實際按鍵確認。

- IntelliSense 改用 build/dev/compile_commands.json 與 UCRT64 g++
- dev_build.sh --clean 失敗時提示資料夾被占用與 BUILD_DIR 替代方案

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 18:10 修正強制結束後 stale lock 阻擋啟動

**類型**: 修正 · **檔案**: `src/main.cpp`

### 問題 - 強制結束 uxplay-windows 後再啟動，可能卡在 already running

### 根因 - QLockFile 的 PID 可能已被其他行程重用，或行程已死但 lock 仍存在

### 修改 - 取得 lock 的 PID，確認是否為 uxplay-windows；失效時移除 lock 並重試

以 Stop-Process 強制結束 build/dev，lock 仍存在；下一次啟動成功取得 lock，PID 由 14428 變為 56440。

- lock PID 等於目前行程時也視為 stale

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 18:10 Phase 2B：設定收斂為 settings.json

**類型**: 功能 · **檔案**: `src/settings.h、src/settings.cpp、src/mainwindow.cpp、src/mainwindow.h、CMakeLists.txt、docs/DEVELOPERS-GUIDE.md`

### 問題 - 設定分散在 QSettings 與 arguments.txt，啟動時要逐項 remove/append 參數

### 根因 - UI 與引擎各自維護同一組選項

arguments.txt 的 managed args 與 UI 值會衝突，applyXxxArgs 只能事後修補。

### 修改 - 新增 AppSettings：settings.json 為唯一引擎／UI 設定來源

以 build/dev 實測：遷移後 `settings.json` 正確、`arguments.txt.migrated`
存在、QSettings 引擎 key 清除；預設引擎參數為
`-h265 -fps 60 -n uxplay-test -nh -s 1920x1080@60 -ble ... -d`；
改成 1440p／d3d12／fullscreen 後重啟，參數變為
`-fs -vs d3d12videosink -s 2560x1440@60`。
Stop/Start 後 JSON `server_enabled` 正確切換。

- 舊 QSettings／arguments.txt 一次性遷移；arguments.txt 改名為 .migrated
- 引擎參數由模型產生；移除 applyDisplayNameArg／applyRendererAndFullscreenArgs／applyResolutionArgs
- server_enabled 與 Start/Stop 熱啟停寫回 settings.json

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 08:26 Nightly release 加入 SHA、run 連結與最近 10 筆 commit

**類型**: 維護 · **檔案**: `.github/workflows/build_uxplay-windows-v2.yml、.github/workflows/build-uxplay-windows-arm64.yml、docs/DEVELOPERS-GUIDE.md`

### 問題 - Release 標題時間空白，tag 停在舊 commit

### 根因 - PowerShell step 用了 bash 的 date 與輸出語法

Workflow 在 PowerShell step 用 bash 的 `date` 與
`$GITHUB_OUTPUT`；固定 tag 沒有更新到當前 commit。

### 修改 - 改用 PowerShell 產生 UTC 時間與完整／短 SHA

tag 仍為 nightly-build-x64／arm64，assets 名稱不變；release 的建立
日期會是當晚。正式版本、Core／Exp 分類仍留到 Phase 3。

- release 名稱與內文加入 commit、run 連結、架構、最近 10 筆 commit
- 每晚刪除重建 nightly release，tag 指向當前 SHA
- fetch-depth 改為 20 取得 commit log

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 08:21 MSI 建立固定防火牆規則並清理舊 build 規則

**類型**: 修正 · **檔案**: `product.wxs、scripts/build_msi.ps1、scripts/clean_firewall_rules.ps1、.github/workflows/build_uxplay-windows-v2.yml、.github/workflows/build-uxplay-windows-arm64.yml、docs/DEVELOPERS-GUIDE.md、README.md`

### 問題 - 每次換 build 目錄就多一組 Windows 防火牆規則

### 根因 - Windows 防火牆以執行檔完整路徑辨識程式

build/dev、build/fix、build/phase2a、Temp 解壓都是不同程式，因此各自建立規則；程式本身沒有建立規則。

### 修改 - MSI 透過 WiX Firewall extension 建立一組 uxplay-windows group 規則

本機 MSI 已含 Wix5FirewallException table 與 install／uninstall custom
actions；CI 同步加入 WixToolset.Firewall.wixext。舊規則是 Windows
依程式路徑自動建立，不是 App 寫入。

- 新增 clean_firewall_rules.ps1，保留 MSI group、移除舊 build／Temp 規則
- 文件改為固定使用 build/dev，BUILD_DIR 只做一次性隔離

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 08:21 Phase 2A：引擎支援熱啟停與崩潰 backoff

**類型**: 功能 · **檔案**: `libuxplay/uxplay.cpp、libuxplay/uxplay_api.h、src/airplayworker.cpp、src/airplayworker.h、src/mainwindow.cpp、src/mainwindow.h、src/main.cpp`

### 問題 - Start/Stop 只能重啟整個 App，引擎停止不乾淨

stop_uxplay 走 QueuedConnection 永遠不會被 worker 執行，cleanup 又直接 exit(0)。

### 根因 - AirPlayWorker 沒有 Qt event loop，Queued 訊號不會派送

worker 的 run() 是自訂阻塞迴圈，GUI 端只能用 Direct call 或引擎自己的 shutdown 狀態。

### 修改 - 引擎改為單次 start_uxplay，stop 由引擎 thread 自行拆除

以 build/phase2a 連續 Stop/Start 5 次：每次都重新
Initialized server socket(s)、register_dnssd、UxPlay engine ready，
App 全程存活，無 terminate。ready 回呼在引擎真正就緒後才發出，
UI 不再提前顯示 started。

- 新增 ready callback、reset_engine_options／reset_uxplay_state
- cleanup 移除 exit，錯誤改回傳負數 exit code
- GUI 直接呼叫 stopAirplay 並等待，崩潰 backoff 1／2／5／10 秒
- 新增 --start／--stop IPC，托盤 Start/Stop 改為熱切換

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 06:57 修正安裝包缺少 FFmpeg DLL 導致引擎啟動即退出

**類型**: 修正 · **檔案**: `make_release_CI.sh、make_release_CI_arm64.sh、product.wxs`

### 問題 - MSI 安裝後啟動，引擎在 gstreamer_init() 失敗並退出

### 根因 - FFmpeg 版本漂移，dll.txt 仍列舊版

CI 的 GStreamer 使用 avcodec-63／avformat-63／avutil-61，但
stuff/dll.txt 仍列 62／60／11；舊檔不存在，新檔又不在清單中，
libgstlibav.dll 被 GStreamer blacklist。

### 修改 - release 腳本改用 objdump 依賴閉包並在打包後驗證 plugin

以 objdump 解析 app、Qt／GStreamer DLL 與 plugin 的匯入表，
遞迴複製所有存在於 MSYS2 prefix 的 DLL；打包後逐一檢查非系統
匯入是否齊全，並用 gst-inspect 驗證 appsrc、avdec_h264、
playbin、autovideosink、h264parse 可載入。本機以 UCRT64 的
libgstlibav.dll 模擬，closure 收集 114 個 DLL，blacklist 為 0，
avdec_h264 可載入。

- 讓 arm64 腳本依 MSYSTEM_PREFIX 改寫 dll.txt 的 /ucrt64 路徑
- 缺少的 dll.txt 項目改為警告，不再靜默跳過
- MSI 允許同版本升級，nightly 同版本可直接覆蓋

**相關文件**: docs/DEVELOPERS-GUIDE.md

---

## 2026.11.11 06:03 修正 AirPlay 快速重連後畫面定格

**類型**: 修正 · **檔案**: `libuxplay/lib/raop_rtp_mirror.c`

### 問題 - iPad 斷線後快速重連，畫面停在上一幀

### 根因 - mirror thread 自行結束時 joined 仍為 0，start 因此直接 return

### 修改 - stop() 即使 running=false 也 join，start() 回收上一個 thread

- 新增 stopping 狀態，並在 concurrent stop 時等待
- start 被跳過時記錄 running/joined/stopping

### 驗證 - 快速重連

- `BUILD_DIR=build/fix bash scripts/dev_build.sh` 建置通過。
- 已用 iPad 停止／重啟鏡像實測：第二次連線恢復正常，畫面不再定格。

**相關文件**: [開發者指南](DEVELOPERS-GUIDE.md)

---

## 2026.11.11 05:56 修正 dev exe 直接啟動缺少 libgcc_s_seh-1.dll

**類型**: 修正 · **檔案**: `scripts/dev_build.sh、docs/DEVELOPERS-GUIDE.md`

### 問題 - 直接雙擊 build/dev/uxplay-windows.exe 會找不到 libgcc_s_seh-1.dll

### 根因 - dev exe 動態連結 MinGW runtime，啟動環境沒有 MSYS2 PATH

Explorer／debugger 不繼承 MSYS2 PATH；Qt6Core 與 GStreamer 也依賴同一個 DLL。

### 修改 - dev_build.sh 產生 build/dev/run-dev.cmd，設定 PATH／QT_PLUGIN_PATH／GST_PLUGIN_PATH

- 指南補上直接啟動、debugger 環境與疑難排解

**相關文件**: [開發者指南](DEVELOPERS-GUIDE.md)

---

## 2026.11.11 05:52 新增本地建置／除錯腳本與開發者指南

**類型**: 文件 · **檔案**: `scripts/dev_build.sh、scripts/dev_run.sh、docs/DEVELOPERS-GUIDE.md、.markdownlint.json、src/main.cpp、src/mainwindow.cpp、TODO.md`

### 問題 - 本地 debug 要手動設 PATH／Qt plugin／GStreamer／Bonjour SDK，開發指南只提到 make_release.sh

### 根因 - 沒有 dev build 入口；App 會強制覆寫 GST_PLUGIN_PATH，也沒有把 -d 傳給引擎的簡便方式

### 修改 - 新增 dev_build/dev_run 腳本與 --engine-debug

- App 只在 bundled plugin 存在時覆寫 GST_PLUGIN_PATH，dev build 改用 MSYS2 plugin
- 改寫 DEVELOPERS-GUIDE 為本地開發單一入口，並補上狀態位置、重置與疑難排解

**相關文件**:

- [建置文件](BUILDING.md)
- [待辦清單](../TODO.md)

---

## 2026.11.11 05:41 Phase 1：連線事件、引擎日誌與單一實例

**類型**: 功能 · **檔案**: `src/mainwindow.cpp、src/airplayworker.cpp、src/main.cpp、src/logging.cpp、src/logging.h、libuxplay/uxplay.cpp、libuxplay/uxplay_api.h、CMakeLists.txt、TODO.md`

### 問題 - UI 只看得到 running/streaming 的推測狀態；引擎錯誤與連線事件不可見，多開會搶埠，Bonjour 停止時只能失敗

### 根因 - 引擎的 conn_init／conn_destroy／report_client_request 與日誌沒有接到 GUI；沒有單一實例；只檢查服務存在

### 修改 - 接上連線事件與引擎日誌，新增 rolling log 與 Open Log

- tray Start/Stop（以 App 重啟套用）、狀態圖示與 tooltip、單一實例叫回既有視窗
- Bonjour 改檢查執行狀態並嘗試啟動；主視窗顯示最後錯誤

### 驗證 - Phase 1

- `ninja -C build/ucrt64` 建置通過。
- 實機啟動：Bonjour running、engine 啟動、`register_dnssd` 成功，日誌寫入 `%LOCALAPPDATA%\leapbtw\uxplay-windows\logs\uxplay-windows.log`。
- 第二實例會退出並叫回既有視窗；`--restart` 實例可在舊行程結束後接手。
- `server_enabled=false` 時不啟動引擎，主視窗顯示「AirPlay stopped」。

**相關文件**: [待辦清單](../TODO.md)

---

## 2026.11.11 05:11 接入 changelogkit 並建立 TODO 路線圖

**類型**: 工具 · **檔案**: `.changelog.toml、docs/ChangeHistory.md、TODO.md`

### 問題 - 改動紀錄與待辦散落在 commit 與對話中，缺少可瀏覽的變更歷史與集中待辦

### 根因 - 專案先前沒有 changelogkit 設定，也沒有根目錄 TODO.md

### 修改 - 接入 changelogkit 並建立 AirPlay 改進路線圖

- 新增 `.changelog.toml`：history 指向 `docs/ChangeHistory.md`、todo 指向 `TODO.md`。
- 新增 `docs/ChangeHistory.md`（由 changelogkit 維護）與根目錄 `TODO.md` 分期清單。

**相關文件**:

- [開發者指南](DEVELOPERS-GUIDE.md)
- [待辦清單](../TODO.md)

---
