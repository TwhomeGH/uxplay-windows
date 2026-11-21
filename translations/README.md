# 翻譯規範（i18n）

本專案以英文為 source language，`uxplay-windows_zh_TW.ts` 是繁體中文 catalog。
Qt 的 `tr()` 以「英文原文 + context」作為 message key；英文原文同時是缺翻譯
時的 fallback。

## 檔案角色

| 檔案 | 角色 |
| --- | --- |
| `src/*.cpp` | UI 程式碼；英文原文以 `tr()` 包住 |
| `translations/uxplay-windows_zh_TW.ts` | 翻譯 catalog（由 `lupdate` 產生） |
| `translations/i18n.json` | 語言清單、fallback、技術字串 allowlist |
| `scripts/check_i18n.py` | CI／本機驗證 |
| `scripts/fill_translations.py` | 維護英文→zh_TW 對照並填入 `.ts`；原始碼控制，不是 build 產物 |

> VS Code 會依副檔名把 `.ts` 當成 TypeScript。本專案的 `.ts` 是 Qt Linguist
> XML，已由 `.vscode/settings.json` 將 `translations/*.ts` 關聯為 XML；若編輯器
> 仍顯示 `ts(1005)`、`ts(1161)` 等錯誤，請執行 **Developer: Reload Window**。

## 硬性規則

1. 所有使用者可見字串必須使用 `tr()`（非 QObject 類別使用
   `QCoreApplication::translate("context", "...")`）。
2. 不可把同一個英文句子當成兩個不同意思的 key；必要時拆成不同 context。
3. 不可拼接句子；使用 placeholder：

   ```cpp
   tr("Connected: %1").arg(name)   // 正確
   tr("Connected") + ": " + name   // 禁止
   ```

4. 英文原文是 key：修改文案等於修改 key。修改後必須重新 `lupdate`，
   確認舊翻譯沒有變成 `obsolete`，並補上新翻譯。
5. 翻譯必須保留與英文相同的 placeholder（`%1`、`%2`、`%n`、`%L1`）；
   checker 會比對數量與種類。
6. 有歧義的字串要加 translator comment：

   ```cpp
   //: Button that restarts the AirPlay engine, not the application.
   m_rebuildButton = new QPushButton(tr("Rebuild AirPlay stream"), this);
   ```

7. 不可把翻譯字串拿來做程式邏輯判斷；邏輯請使用 enum、常數或穩定的
   protocol key（例如 `"Video Decoder"`）。
8. 技術字串（`D3D11`、`d3d12`、`720p`、`uxplay-windows`）若刻意不翻譯，
   必須列在 `i18n.json` 的 `technical_literals`；否則 checker 會視為
   bare UI string。

## 工作流程

```powershell
# 1. 掃描 source，更新 .ts
lupdate src -ts translations/uxplay-windows_zh_TW.ts -no-obsolete

# 2. 填入既有翻譯對照（只改 <translation>）
python scripts/fill_translations.py

# 3. 驗證
python scripts/check_i18n.py
```

`check_i18n.py` 會檢查：

- 未翻譯、空白、`unfinished`
- source 有 key 但 catalog 沒有
- catalog 有 obsolete key
- 同一 context 重複 key
- placeholder 不一致
- 未使用 `tr()` 的明顯 UI 字串（QLabel、按鈕、選單、tray、message box）

輸出使用顏色區分：`[ERROR]` 紅、`[WARN]` 黃、`[OK]` 綠。
終端不支援顏色時用 `--color=never`；需要強制顏色時用 `--color=always`。

CI 使用：

```powershell
python scripts/check_i18n.py --require-lupdate
```

`--require-lupdate` 會在找不到 `lupdate` 時直接失敗，而不是跳過
source-key 比對。

若只是暫時要觀察問題，可用：

```powershell
python scripts/check_i18n.py --allow-missing --allow-bare
```

正式 CI 不得使用這兩個旗標。

## Fallback

- `.qm` 不存在或 locale 沒有 catalog → 全部英文。
- 單一 key 缺少／空白／`unfinished` → 該字串英文。
- Fallback 是安全網，不是完成狀態；CI 必須讓缺翻譯失敗。

## 何時要改成 stable ID

目前以英文原文作 key，適合單一翻譯者與少數 locale。若出現以下情況，
再評估 `qtTrId("main.rebuild_button")` 或外部
`translations/ui.<lang>.json`：

- 多個翻譯者／翻譯平台
- 多個語言
- 非工程人員頻繁修改文案
- 需要文案與程式碼完全解耦

改為 stable ID 是獨立遷移，不應與一般 UI 修改混在同一個變更。
