#pragma once

#include <QString>
#include <QByteArray>

/** @file settingsfile.h
 * @brief 設定資料的原子寫入，與 AppData 路徑選擇及 JSON 序列化分離。
 */
namespace settingsfile {

/**
 * @brief 內容改變時以暫存檔原子替換目標，失敗時保留原有檔案。
 * @param path 目標設定檔路徑，缺少父目錄時會嘗試建立。
 * @param data 完整序列化內容，函式返回前保持有效即可。
 * @return 內容相同或原子提交成功時為 true；任何寫入階段失敗為 false。
 * @note 比對舊資料的 handle 會在建立 QSaveFile 前關閉；不啟用直接覆寫備援。
 * 本函式同步執行，呼叫端應序列化儲存要求；不提供跨程序的版本衝突處理。
 */
bool writeAtomically(const QString &path, const QByteArray &data);

} // namespace settingsfile
