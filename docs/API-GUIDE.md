# uxplay-windows API 導讀 {#mainpage}

**用途**：這是 Doxygen 的預設首頁，提供程式碼地圖與閱讀順序；詳細的
類別、參數與回傳值仍由原始碼註解產生。

本入口只產生主 App 的 Qt／設定／診斷 API。引擎、RTP 與 renderer 移至
<a href="../libuxplay/index.html">libuxplay API 參考</a>。
建置時先產生引擎標籤檔，因此本頁的 start_uxplay()、stop_uxplay() 可連到
對應的引擎 API；各自的變更歷史亦獨立維護。

## 怎麼看這份 API

| 想做什麼 | 從哪裡開始 |
| --- | --- |
| 看程式啟動與托盤生命週期 | `src/main.cpp` → `MainWindow` |
| 看 AirPlay engine 如何啟動／停止 | `src/airplayworker.cpp` → `libuxplay/uxplay_api.h` |
| 看主視窗與設定 | `src/mainwindow.cpp`、`src/settings.cpp` |
| 看診斷包與 ring buffer | `src/diagnostics.cpp`、`src/diagnostics.h` |
| 看影格統計與 HUD | `src/mainwindow.cpp`、`libuxplay/renderers/video_renderer.c` |
| 看 USB 擷取（選用） | `src/usbdeviceconnector.cpp`、`src/usbobscontroller.cpp`、`usbcapture/` |
| 看 i18n 規則 | `translations/README.md` |
| 看建置與除錯 | `docs/DEVELOPERS-GUIDE.md` |

## 模組地圖

| 目錄 | 角色 | 常見入口 |
| --- | --- | --- |
| `src/` | Qt GUI、托盤、設定、診斷、USB 整合 | `MainWindow`、`AirPlayWorker`、`AppSettings`、`DiagnosticsRecorder` |
| `libuxplay/` | AirPlay 接收器與 GStreamer renderer | `uxplay_api.h`、`start_uxplay()`、`stop_uxplay()` |
| `libuxplay/renderers/` | 影片／音訊 pipeline | `video_renderer.c`、`audio_renderer.c` |
| `usbcapture/` | 選用 USB CoreDevice spike 與 bridge | `usbcapture/README.md` |
| `scripts/` | 建置、執行、診斷、i18n、文件工具 | `dev_build.sh`、`check_i18n.py`、`diagnostic_viewer.py` |
| `translations/` | Qt catalog 與 i18n 規範 | `uxplay-windows_zh_TW.ts`、`README.md` |
| `docs/` | 開發者入口與變更歷史 | `DEVELOPERS-GUIDE.md`、`ChangeHistory.md` |

## 啟動與串流生命週期

```text
main()
  └─ MainWindow
       ├─ setupTray() / setupUI()
       ├─ AppSettings::load()          settings.json
       ├─ AirPlayWorker::run()
       │    └─ start_uxplay()          libuxplay/uxplay.cpp
       │         ├─ dnssd/mDNS 註冊
       │         ├─ raop server
       │         └─ GStreamer renderers
       ├─ mediaInfoChanged / client events
       ├─ DiagnosticsRecorder          ring buffer + export
       └─ stop_uxplay()                engine thread 自行 teardown
```

- `start_uxplay()` 每次啟動都會重設可重入狀態；`stop_uxplay()` 只發出
  shutdown 訊號，由 engine thread 清理。
- 影片是 GStreamer 原生視窗；GUI 只負責找到 HWND、套用幾何與 HUD。
- 診斷資料由 `DiagnosticsRecorder` 保存，匯出才是檔案。

## 註解策略

Doxygen 只讀 `///` 與 `/** */`，一般 `//` 不會出現在 API 文件。

- 公開標頭與類別：使用 `///` 或 `/** */`。
- 參數／回傳值：優先寫 `@param`、`@return`；行為限制寫在 `@note`。
- 內部快速註解可以保留 `//`，但不要期待它出現在 API 頁面。
- 新增公開 API 時，順手補上用途與生命週期說明。

## 產生 API 文件

```powershell
pwsh -NoProfile -File scripts/build_docs.ps1
pwsh -NoProfile -File scripts/build_docs.ps1 -Open
```

輸出：

```text
build/doxygen/app/index.html
build/doxygen/libuxplay/index.html
```

`build/doxygen/` 不進版控，且與 changelogkit 匯出的 `_site/` 分離。若本機沒有 Doxygen，腳本會嘗試用 Docker 的
`ubuntu:24.04` 執行。VS Code 也可直接用任務 `docs: api`。

changelogkit 的「API 文件」下拉同時提供這兩套站點。主專案設定以
`embed = true` 支援靜態匯出；來源在 `build/doxygen`，匯出副本才位於
`_site/api-docs/app` 與 `_site/api-docs/libuxplay`，避免來源被匯出清理移除。

## 相關文件

- `docs/DEVELOPERS-GUIDE.md`：建置、執行、除錯與 i18n
- `translations/README.md`：翻譯規範
- `docs/ChangeHistory.md`：變更歷史
