# ChangeHistory

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
