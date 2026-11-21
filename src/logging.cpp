#include "logging.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace applog {
namespace {

constexpr qint64 kMaxBytes = 8 * 1024 * 1024;
constexpr int kMaxRotatedFiles = 5;

QMutex g_mutex;
QString g_path;
bool g_installed = false;
QtMessageHandler g_previousHandler = nullptr;

struct PendingLine {
    QByteArray bytes;
    quint64 sequence;
};
constexpr size_t kQueueBytes = 4 * 1024 * 1024;
constexpr size_t kQueueLines = 4096;
constexpr int kMaxLineBytes = 16 * 1024;
constexpr size_t kBatchBytes = 64 * 1024;
std::mutex g_queueMutex;
std::condition_variable g_wake;
std::condition_variable g_drained;
std::deque<PendingLine> g_pending;
std::thread g_writer;
size_t g_pendingBytes = 0;
quint64 g_accepted = 0;
quint64 g_completed = 0;
quint64 g_dropped = 0;
quint64 g_pendingDrops = 0;
quint64 g_batches = 0;
bool g_accepting = false;
bool g_stopping = false;
bool g_flushRequested = false;

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

void openLogFileForWriter(QFile &g_file) {
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

void rotateLogFileForWriter(QFile &g_file) {
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

    openLogFileForWriter(g_file);
}

// Only this thread owns the QFile and touches the console. Producers never wait
// for filesystem writes, console output or rotation while holding their locks.
void writerLoop() {
    QFile file;
    openLogFileForWriter(file);
    if (file.size() >= kMaxBytes) rotateLogFileForWriter(file);
    for (;;) {
        QByteArray batch;
        quint64 lastSequence;
        {
            std::unique_lock lock(g_queueMutex);
            g_wake.wait_for(lock, std::chrono::milliseconds(250), [] {
                return g_stopping || g_flushRequested || g_pendingBytes >= kBatchBytes;
            });
            if (g_stopping && g_pending.empty() && !g_pendingDrops) break;
            lastSequence = g_completed;
            while (!g_pending.empty() && batch.size() < static_cast<qsizetype>(kBatchBytes)) {
                PendingLine line = std::move(g_pending.front());
                g_pending.pop_front();
                g_pendingBytes -= line.bytes.size();
                batch.append(line.bytes);
                lastSequence = line.sequence;
            }
            if (g_pendingDrops) {
                batch.append("[applog] queue full: dropped ");
                batch.append(QByteArray::number(g_pendingDrops));
                batch.append(" messages\n");
                g_pendingDrops = 0;
            }
            g_flushRequested = !g_pending.empty();
        }
        if (!batch.isEmpty()) {
            std::fwrite(batch.constData(), 1, batch.size(), stderr);
            std::fflush(stderr);
            if (file.isOpen()) {
                if (file.size() >= kMaxBytes) rotateLogFileForWriter(file);
                if (file.write(batch) != batch.size() || !file.flush()) {
                    std::fputs("applog: file write/flush failed\n", stderr);
                }
            }
        }
        {
            std::lock_guard lock(g_queueMutex);
            g_completed = lastSequence;
            if (!batch.isEmpty()) ++g_batches;
        }
        g_drained.notify_all();
    }
    file.close();
}

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    // Limit characters before UTF-8 conversion so truncation keeps valid text.
    const QString text = msg.size() > kMaxLineBytes / 4
        ? msg.left(kMaxLineBytes / 4) + QStringLiteral(" [truncated]") : msg;
    QByteArray line = QStringLiteral("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             levelName(type), text).toUtf8();
    line.append('\n');
    const bool important = type == QtCriticalMsg || type == QtFatalMsg;
    {
        std::lock_guard lock(g_queueMutex);
        if (!g_accepting) return;
        auto full = [&] {
            return g_pending.size() >= kQueueLines || g_pendingBytes + line.size() > kQueueBytes;
        };
        // Reserve room for a new error by sacrificing older queued messages.
        // Normal traffic is dropped on overload rather than blocking media.
        if (important) {
            while (full() && !g_pending.empty()) {
                g_pendingBytes -= g_pending.front().bytes.size();
                g_pending.pop_front();
                ++g_dropped;
                ++g_pendingDrops;
            }
        }
        if (full()) {
            ++g_dropped;
            ++g_pendingDrops;
            return;
        }
        g_pendingBytes += line.size();
        g_pending.push_back({std::move(line), ++g_accepted});
        if (important) g_flushRequested = true;
    }
    g_wake.notify_one();
    // qFatal aborts immediately after the handler; allow a bounded drain first.
    if (type == QtFatalMsg) applog::flush();
}

} // namespace

void install(const QString &logPath) {
    QMutexLocker locker(&g_mutex);
    if (g_installed) return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs");
    g_path = logPath.isEmpty() ? dir + QStringLiteral("/uxplay-windows.log")
                               : QFileInfo(logPath).absoluteFilePath();
    {
        std::lock_guard lock(g_queueMutex);
        g_pending.clear();
        g_pendingBytes = 0;
        g_accepted = g_completed = g_dropped = g_pendingDrops = g_batches = 0;
        g_accepting = true;
        g_stopping = g_flushRequested = false;
    }
    g_writer = std::thread(writerLoop);
    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_installed = true;
}

bool flush(int timeoutMs) {
    std::unique_lock lock(g_queueMutex);
    const quint64 target = g_accepted;
    g_flushRequested = true;
    g_wake.notify_one();
    return g_drained.wait_for(lock, std::chrono::milliseconds(timeoutMs), [target] {
        return g_completed >= target;
    });
}

LogStats stats() {
    std::lock_guard lock(g_queueMutex);
    return {static_cast<quint64>(g_pending.size()), static_cast<quint64>(g_pendingBytes),
            g_dropped, g_batches};
}

void shutdown() {
    QMutexLocker locker(&g_mutex);
    if (!g_installed) return;
    qInstallMessageHandler(g_previousHandler);
    {
        std::lock_guard lock(g_queueMutex);
        g_accepting = false;
        g_stopping = true;
    }
    g_wake.notify_one();
    g_writer.join();
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

void openLogFileWith() {
    QString path;
    {
        QMutexLocker locker(&g_mutex);
        path = g_path;
    }

    if (path.isEmpty()) {
        return;
    }
    if (!QFile::exists(path)) {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        return;
    }

#ifdef _WIN32
    const bool started = QProcess::startDetached(
        QStringLiteral("rundll32.exe"),
        {QStringLiteral("shell32.dll,OpenAs_RunDLL"),
         QDir::toNativeSeparators(path)});
    if (!started) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

void openLogFolder() {
    QString path;
    {
        QMutexLocker locker(&g_mutex);
        path = g_path;
    }
    if (path.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void openDiagnosticsFolder() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/diagnostics");
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace applog
