# ChangeHistory

## 2026.11.11 06:57 修正安裝包缺少 FFmpeg DLL 導致引擎啟動即退出

**類型**: 修正 · **檔案**: `make_release_CI.sh、make_release_CI_arm64.sh`

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
