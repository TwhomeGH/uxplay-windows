/** @file settings_file_test.cpp
 * @brief 驗證正式原子儲存路徑；只使用工作區 fixture，不接觸使用者設定。
 */
#include "../src/settingsfile.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStringList>
#include <cassert>
#include <cstdio>

namespace {
QStringList messages;

void recordMessage(QtMsgType, const QMessageLogContext &, const QString &message) {
    messages.append(message);
}

QByteArray readFile(const QString &path) {
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory(QDir::currentPath() + "/build/settings-file-test-XXXXXX");
    assert(directory.isValid());
    directory.setAutoRemove(false);
    const auto previousHandler = qInstallMessageHandler(recordMessage);
    const QString path = directory.path() + "/nested/settings.json";
    const QByteArray initial = "{\"value\":1}\n";
    const QByteArray updated = "{\"value\":2}\n";

    assert(settingsfile::writeAtomically(path, initial));
    assert(readFile(path) == initial);
    messages.clear();
    // 正式函式完成比對後，必須已關閉自己的讀取 handle，才能正常替換。
    assert(settingsfile::writeAtomically(path, updated));
    assert(readFile(path) == updated);
    assert(messages.filter("Could not").isEmpty());
    assert(messages.filter("(atomic)").size() == 1);

    QFile reader(path);
    assert(reader.open(QIODevice::ReadOnly));
    reader.readAll();
    messages.clear();
    // 內容相同時不做替換，因此即使有外部讀取 handle 也能成功。
    assert(settingsfile::writeAtomically(path, updated));
    assert(messages.isEmpty());
    // 模擬其他程序／元件仍持有不分享刪除權的 Windows 讀取 handle。
    assert(!settingsfile::writeAtomically(path, initial));
    assert(readFile(path) == updated);
    assert(messages.filter("Could not commit").size() == 1);
    assert(messages.filter("direct fallback").isEmpty());
    reader.close();
    assert(settingsfile::writeAtomically(path, initial));
    assert(readFile(path) == initial);

    // 既有 Windows CRLF JSON 可更新為原始 LF bytes，後續比對不會被換行轉換干擾。
    const QString legacyPath = directory.path() + "/legacy.json";
    QFile legacy(legacyPath);
    assert(legacy.open(QIODevice::WriteOnly));
    assert(legacy.write("{\"value\":1}\r\n") == 13);
    legacy.close();
    assert(settingsfile::writeAtomically(legacyPath, initial));
    assert(readFile(legacyPath) == initial);
    assert(QJsonDocument::fromJson(readFile(legacyPath)).object().value("value").toInt() == 1);
    // 將目錄當檔名的 open 失敗也必須回傳 false，不影響其他成功設定。
    assert(!settingsfile::writeAtomically(directory.path(), updated));
    assert(readFile(path) == initial);
    qInstallMessageHandler(previousHandler);
    puts("Atomic settings tests passed: create/update/no-op/locked failure preservation/retry/CRLF");
}
