/**
 * @file settings_commit_probe.cpp
 * @brief 在工作區副本比較開啟讀取 handle 對 Windows 原子替換的影響。
 * 不讀寫使用者真正的 settings.json；保留 fixture 供檢查。
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir folder(QDir::currentPath() + "/build/settings-probe-XXXXXX");
    folder.setAutoRemove(false);
    if (!folder.isValid()) return 2;
    const QString path = folder.path() + "/settings.json";
    QFile initial(path);
    if (!initial.open(QIODevice::WriteOnly) || initial.write("old\n") != 4) return 2;
    initial.close();

    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly)) return 2;
    existing.readAll();
    // 比較階段保留原程式的 direct fallback 選項，但不做破壞原檔的 truncate 備援。
    auto commit = [&](const char *phase) {
        QSaveFile file(path);
        file.setDirectWriteFallback(true);
        const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
        const bool wrote = opened && file.write("new\n") == 4;
        const bool committed = wrote && file.commit();
        std::printf("%s: opened=%d wrote=%d committed=%d error=%s\n", phase,
                    opened, wrote, committed, file.errorString().toUtf8().constData());
        return committed;
    };
    const bool held = commit("read_handle_open");
    existing.close();
    const bool closed = commit("read_handle_closed");
    std::printf("fixture=%s\n", folder.path().toUtf8().constData());
    return !held && closed ? 0 : 1;
}
