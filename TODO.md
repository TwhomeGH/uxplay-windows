# 待辦清單 TODO

> [!TIP]
> AirPlay／UX 改進依「可觀測性 → 生命週期 → 功能 → 體驗」分期推進；
> 完成後把 `[ ]` 改成 `[x]`，並用 `changelogkit add` 留下變更紀錄。

## Phase 1 — 可觀測性與基本操作

- [ ] 擴充引擎 callback：`client_connected(name, model, deviceid)`、`client_disconnected`、`open_connections`。
- [ ] 主視窗與 tray 顯示目前連線裝置名稱／型號，連線與斷線時通知。
- [ ] 引擎日誌寫入檔案（rolling log），tray 加「Open Log」，主視窗顯示最後錯誤。
- [ ] tray 新增 Start/Stop AirPlay；圖示與 tooltip 區分 idle／streaming／error。
- [ ] 單一實例保護（QLockFile 或 named mutex），第二次啟動叫回既有視窗。
- [ ] Bonjour 服務改檢查執行狀態並嘗試自動啟動；修正 `ACCESS_DENIED` 被誤判為存在。

## Phase 2 — 生命週期與設定模型

- [ ] 設定收斂為單一結構化來源（JSON/INI），命令列參數由設定產生，只留「額外參數」欄位。
- [ ] 設定變更改為重啟引擎 worker，UI 顯示 pending 變更與單一「套用」動作。
- [ ] 修正優雅停止路徑：`stop_uxplay()` 實際被呼叫；移除 `cleanup()` 內的 `exit(0)`。
- [ ] 引擎崩潰重啟加入 backoff，並顯示「引擎連續失敗」狀態。

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
- [ ] 移除未使用的 `m_autostartAction`／`m_statusAction`。

## 已完成

- [x] 接入 changelogkit（`.changelog.toml`、`docs/ChangeHistory.md`）。
- [x] 建立根目錄 `TODO.md` 並整理 AirPlay 改進分期。
