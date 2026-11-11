# ChangeHistory

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
