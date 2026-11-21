#include "settingsfile.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

bool settingsfile::writeAtomically(const QString &path, const QByteArray &data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Could not create settings directory for" << path;
        return false;
    }

    {
        // Windows 的讀取 handle 可能不允許替換；離開此作用域後才開始提交，
        // 避免自己的內容比對阻擋 QSaveFile。內容相同時也不需再次寫入。
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const QByteArray previous = existing.readAll();
            if (existing.error() == QFileDevice::NoError && previous == data) {
                return true;
            }
        }
    }

    QSaveFile file(path);
    // 權限、外部占用或檔案系統失敗時，不以 truncate 犧牲舊設定來繞過問題。
    file.setDirectWriteFallback(false);
    // 寫入原始 JSON bytes；Text 模式的 CRLF 轉換會讓下次內容比對永遠不同。
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Could not open atomic settings file" << path << ":" << file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        qWarning() << "Could not complete atomic settings write" << path << ":" << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qWarning() << "Could not commit settings file" << path << ":" << file.errorString();
        return false;
    }
    qDebug() << "Saved settings to" << path << "(atomic)";
    return true;
}
