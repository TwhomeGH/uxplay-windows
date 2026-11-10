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

## 狀態位置與重置

| 狀態 | 位置 | 重置方式 |
| --- | --- | --- |
| 引擎參數 | `%APPDATA%\leapbtw\uxplay-windows\arguments.txt` | 關閉 App 後刪除，下次啟動重建預設值 |
| UI／引擎設定 | `HKCU\Software\leapbtw\uxplay-windows` | 關閉 App 後刪除整個 key |
| 日誌 | `%LOCALAPPDATA%\leapbtw\uxplay-windows\logs` | 關閉 App 後刪除目錄 |
| 單一實例 lock | `%TEMP%\uxplay-windows.lock` | 正常會自動處理；卡住時關閉 App 後刪除 |
| BLE 狀態檔 | `%APPDATA%\leapbtw\uxplay-windows\uxplay_status.ble` | 引擎停止時自動刪除；必要時手動刪除 |

QSettings 內常用的值：`display_name`、`renderer_mode`、`resolution_mode`、
`resolution_width`、`resolution_height`、`resolution_refresh`、
`ble_enabled`、`server_enabled`。

## 疑難排解

| 症狀 | 可能原因 | 處理 |
| --- | --- | --- |
| iPhone 看不到 PC | Bonjour 沒跑、防火牆擋 UDP 5353、不同網段 | 看 log 是否有 `register_dnssd`；啟動 Bonjour Service；允許 uxplay-windows 通過防火牆 |
| `register_dnssd` 失敗 | 名稱或 MAC 衝突、port 被佔用 | 關閉其他實例；必要時在 `arguments.txt` 加 `-m`／`-p` |
| 引擎一啟動就退出 | 參數錯誤、GStreamer／Bonjour 初始化失敗 | 看 log 的 `[engine]` 與 `*** ERROR` 行；先用 `--engine-debug` |
| GStreamer plugin 找不到 | `GST_PLUGIN_PATH` 沒指到 UCRT64 plugin | 用 `dev_run.sh`；確認 `$MSYSTEM_PREFIX/lib/gstreamer-1.0` 存在 |
| 沒有聲音 | Windows 輸出裝置不對 | 在 `arguments.txt` 用 `-as` 指定 sink；或改系統預設裝置 |
| 第二次啟動沒叫回視窗 | 第一次還沒建立 IPC，或舊 lock 卡住 | 看 log；必要時刪 `%TEMP%\uxplay-windows.lock` |
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

## 目前限制（Phase 1）

- Start/Stop 以「重啟 App」套用；Phase 2 會改成引擎熱重啟。
- 引擎的 `cleanup()` 仍會 `exit(0)`；Phase 2 會改為正常返回。
- 客戶端連線事件已接到 UI，但尚未用真實 iPhone 驗證；連上後看
  `[engine]`／`Client connected` 與 tray 的 `Client` 欄位。
