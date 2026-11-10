# ChangeHistory

## 2026.11.11 05:41 Phase 1：連線事件、引擎日誌與單一實例

**類型**: 功能 · **檔案**: `src/mainwindow.cpp、src/airplayworker.cpp、src/main.cpp、src/logging.cpp、src/logging.h、libuxplay/uxplay.cpp、libuxplay/uxplay_api.h、CMakeLists.txt、TODO.md`

### 問題 - UI 只看得到 running/streaming 的推測狀態；引擎錯誤與連線事件不可見，多開會搶埠，Bonjour 停止時只能失敗

### 根因 - 引擎的 conn_init／conn_destroy／report_client_request 與日誌沒有接到 GUI；沒有單一實例；只檢查服務存在

### 修改 - 接上連線事件與引擎日誌，新增 rolling log 與 Open Log

- tray Start/Stop（以 App 重啟套用）、狀態圖示與 tooltip、單一實例叫回既有視窗
- Bonjour 改檢查執行狀態並嘗試啟動；主視窗顯示最後錯誤

### 驗證

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
