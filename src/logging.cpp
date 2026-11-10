#include "logging.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QUrl>

#include <cstdio>

namespace applog {
namespace {

constexpr qint64 kMaxBytes = 2 * 1024 * 1024;
constexpr int kMaxRotatedFiles = 3;

QMutex g_mutex;
QFile g_file;
QString g_path;
bool g_installed = false;

QString levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("LOG");
}

QString rotatedPath(int index) {
    const QFileInfo info(g_path);
    return info.absolutePath() + "/" + info.completeBaseName() + "."
           + QString::number(index) + "." + info.suffix();
}

void openLocked() {
    if (g_path.isEmpty()) {
        return;
    }

    QDir().mkpath(QFileInfo(g_path).absolutePath());
    g_file.setFileName(g_path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QByteArray pathBytes = g_path.toLocal8Bit();
        const QByteArray errorBytes = g_file.errorString().toLocal8Bit();
        std::fprintf(stderr, "applog: cannot open %s: %s\n",
                     pathBytes.constData(), errorBytes.constData());
    }
}

void rotateLocked() {
    if (!g_file.isOpen()) {
        return;
    }

    g_file.close();

    QFile::remove(rotatedPath(kMaxRotatedFiles));
    for (int i = kMaxRotatedFiles - 1; i >= 1; --i) {
        const QString from = rotatedPath(i);
        if (QFile::exists(from)) {
            QFile::remove(rotatedPath(i + 1));
            QFile::rename(from, rotatedPath(i + 1));
        }
    }
    if (QFile::exists(g_path)) {
        QFile::remove(rotatedPath(1));
        QFile::rename(g_path, rotatedPath(1));
    }

    openLocked();
}

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  levelName(type),
                                  msg);

    // Mirror to the attached console first, so a broken log file never hides
    // diagnostics. Use stderr rather than the default handler to avoid
    // double-printing when a console is attached.
    const QByteArray consoleLine = line.toLocal8Bit();
    std::fprintf(stderr, "%s\n", consoleLine.constData());
    std::fflush(stderr);

    QMutexLocker locker(&g_mutex);
    if (!g_file.isOpen()) {
        return;
    }
    if (g_file.size() >= kMaxBytes) {
        rotateLocked();
    }
    if (!g_file.isOpen()) {
        return;
    }
    g_file.write(line.toUtf8());
    g_file.write("\n");
    g_file.flush();
}

} // namespace

void install() {
    QMutexLocker locker(&g_mutex);
    if (g_installed) {
        return;
    }

    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    g_path = dir + QStringLiteral("/uxplay-windows.log");

    if (QFileInfo(g_path).size() >= kMaxBytes) {
        openLocked();
        rotateLocked();
    } else {
        openLocked();
    }

    qInstallMessageHandler(messageHandler);
    g_installed = true;
}

void shutdown() {
    QMutexLocker locker(&g_mutex);
    if (!g_installed) {
        return;
    }

    qInstallMessageHandler(nullptr);
    g_file.flush();
    g_file.close();
    g_installed = false;
}

QString filePath() {
    QMutexLocker locker(&g_mutex);
    return g_path;
}

void openLogFile() {
    QString path;
    {
        QMutexLocker locker(&g_mutex);
        path = g_path;
    }

    if (path.isEmpty()) {
        return;
    }

    if (QFile::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

} // namespace applog
