#include "diagnostics.h"

#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>

#include <gst/gst.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifndef UXPLAY_APP_VERSION
#define UXPLAY_APP_VERSION "dev"
#endif
#ifndef UXPLAY_GIT_SHA
#define UXPLAY_GIT_SHA "unknown"
#endif
#ifndef UXPLAY_BUILD_TYPE
#define UXPLAY_BUILD_TYPE "unknown"
#endif

namespace {

constexpr int kMaxEvents = 3000;
constexpr int kMaxMetrics = 1800;
constexpr int kMaxBundles = 10;

QString redactText(const QString &input, const QString &displayName,
                   const QString &hostName) {
    QString text = input;

    static const QRegularExpression userPath(
        QStringLiteral("C:[/\\\\]+Users[/\\\\]+[^/\\\\\\s]+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression udid(
        QStringLiteral("\\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{16}\\b"));
    static const QRegularExpression mac(
        QStringLiteral("\\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\\b"));
    static const QRegularExpression ipv4(
        QStringLiteral("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b"));
    static const QRegularExpression clientName(
        QStringLiteral("(Client connected:\\s*\")[^\"]*(\")"));
    static const QRegularExpression deviceId(
        QStringLiteral("(deviceid:\\s*\")[^\"]*(\")"));
    static const QRegularExpression nameArg(
        QStringLiteral("(-n\\s+)([^\\s\"]+)"));

    text.replace(userPath, QStringLiteral("C:/Users/<redacted>"));
    text.replace(udid, QStringLiteral("<udid>"));
    text.replace(mac, QStringLiteral("<mac>"));
    text.replace(ipv4, QStringLiteral("<ip>"));
    text.replace(clientName, QStringLiteral("\\1<redacted>\\2"));
    text.replace(deviceId, QStringLiteral("\\1<redacted>\\2"));
    text.replace(nameArg, QStringLiteral("\\1<redacted>"));

    if (!displayName.isEmpty()
        && displayName != QStringLiteral("uxplay-windows")) {
        text.replace(displayName, QStringLiteral("<redacted>"));
    }
    if (!hostName.isEmpty()) {
        text.replace(hostName, QStringLiteral("<redacted>"),
                     Qt::CaseInsensitive);
    }
    return text;
}

bool isSensitiveKey(const QString &key) {
    const QString lowered = key.toLower();
    return lowered == QStringLiteral("name")
           || lowered == QStringLiteral("deviceid")
           || lowered == QStringLiteral("udid")
           || lowered == QStringLiteral("ip")
           || lowered == QStringLiteral("mac")
           || lowered == QStringLiteral("ssid")
           || lowered == QStringLiteral("host")
           || lowered == QStringLiteral("hostname")
           || lowered == QStringLiteral("username")
           || lowered == QStringLiteral("display_name");
}

QJsonValue redactValue(const QJsonValue &value, const QString &displayName,
                       const QString &hostName) {
    if (value.isString()) {
        return redactText(value.toString(), displayName, hostName);
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const QJsonValue &item : value.toArray()) {
            result.append(redactValue(item, displayName, hostName));
        }
        return result;
    }
    if (value.isObject()) {
        QJsonObject result;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (isSensitiveKey(it.key())) {
                result[it.key()] = QStringLiteral("<redacted>");
            } else {
                result[it.key()] =
                    redactValue(it.value(), displayName, hostName);
            }
        }
        return result;
    }
    return value;
}

QJsonObject redactedSettings(const AppSettings &settings,
                             const QString &displayName,
                             const QString &hostName) {
    QJsonObject resolutionObject;
    resolutionObject[QStringLiteral("mode")] = settings.resolution.mode;
    resolutionObject[QStringLiteral("width")] = settings.resolution.width;
    resolutionObject[QStringLiteral("height")] = settings.resolution.height;
    resolutionObject[QStringLiteral("refresh")] = settings.resolution.refresh;

    QJsonObject obsObject;
    obsObject[QStringLiteral("mode")] = settings.obsMode;

    QJsonObject diagnosticsObject;
    diagnosticsObject[QStringLiteral("enabled")] = settings.diagnosticsEnabled;

    QJsonArray extra;
    bool redactNext = false;
    for (const QString &arg : settings.extraArgs) {
        if (redactNext) {
            extra.append(QStringLiteral("<redacted>"));
            redactNext = false;
        } else if (arg == QStringLiteral("-n")) {
            extra.append(arg);
            redactNext = true;
        } else {
            extra.append(redactText(arg, displayName, hostName));
        }
    }

    QJsonObject object;
    object[QStringLiteral("schema_version")] = settings.schemaVersion;
    object[QStringLiteral("display_name")] = QStringLiteral("<redacted>");
    object[QStringLiteral("ble_enabled")] = settings.bleEnabled;
    object[QStringLiteral("force_fullscreen")] = settings.forceFullscreen;
    object[QStringLiteral("renderer")] = settings.renderer;
    object[QStringLiteral("resolution")] = resolutionObject;
    object[QStringLiteral("no_hold")] = settings.noHold;
    object[QStringLiteral("server_enabled")] = settings.serverEnabled;
    object[QStringLiteral("obs")] = obsObject;
    object[QStringLiteral("diagnostics")] = diagnosticsObject;
    object[QStringLiteral("extra_args")] = extra;
    return object;
}

QString redactedEngineArgs(const AppSettings &settings,
                           const QString &displayName,
                           const QString &hostName) {
    const QStringList args =
        settings.engineArguments(QStringLiteral("<ble>"), false);
    QStringList redacted;
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("-n") && i + 1 < args.size()) {
            redacted << QStringLiteral("-n") << QStringLiteral("<redacted>");
            ++i;
        } else {
            redacted << redactText(args.at(i), displayName, hostName);
        }
    }
    return redacted.join(QLatin1Char(' '));
}

QString screenInfo() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return QStringLiteral("unavailable");
    }
    const QRect geometry = screen->geometry();
    return QStringLiteral("%1x%2 @ %3 Hz (dpr %4)")
        .arg(geometry.width())
        .arg(geometry.height())
        .arg(screen->refreshRate())
        .arg(screen->devicePixelRatio());
}

QString hardwareInfo() {
#ifdef Q_OS_WIN
    QProcess process;
    process.start(
        QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
         QStringLiteral("-Command"),
         QStringLiteral("Get-CimInstance Win32_VideoController | "
                        "ForEach-Object { 'GPU: ' + $_.Name }; "
                        "Get-CimInstance Win32_Processor | "
                        "ForEach-Object { 'CPU: ' + $_.Name }")});
    if (!process.waitForStarted(1500)) {
        return QStringLiteral("unavailable");
    }
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return QStringLiteral("timed out");
    }
    const QString output =
        QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    return output.isEmpty() ? QStringLiteral("unavailable") : output;
#else
    return QStringLiteral("unavailable");
#endif
}

bool writeText(const QString &path, const QString &text, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }
    if (file.write(text.toUtf8()) < 0) {
        if (error) {
            *error = QStringLiteral("Cannot write %1: %2")
                         .arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

bool writeJson(const QString &path, const QJsonDocument &document,
               QString *error) {
    return writeText(path, QString::fromUtf8(document.toJson(
                               QJsonDocument::Indented)),
                     error);
}

QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void writeRedactedLogs(const QString &bundleDir, const QString &displayName,
                       const QString &hostName) {
    const QString logPath = applog::filePath();
    if (logPath.isEmpty()) {
        return;
    }

    const QFileInfo info(logPath);
    const QDir sourceDir(info.absolutePath());
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    QStringList names;
    names << info.fileName();
    for (int i = 1; i <= 3; ++i) {
        names << QStringLiteral("%1.%2.%3").arg(base).arg(i).arg(suffix);
    }

    const QString logOutDir = bundleDir + QStringLiteral("/logs");
    QDir().mkpath(logOutDir);

    for (const QString &name : names) {
        const QString path = sourceDir.filePath(name);
        const QString text = readText(path);
        if (text.isEmpty()) {
            continue;
        }
        writeText(logOutDir + QLatin1Char('/') + name,
                  redactText(text, displayName, hostName), nullptr);
    }
}

bool zipBundle(const QString &rootDir, const QString &folderPath,
               const QString &folderName, const QString &zipPath) {
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (!tar.isEmpty()) {
        QProcess process;
        process.setWorkingDirectory(rootDir);
        process.start(tar, {QStringLiteral("-a"), QStringLiteral("-c"),
                            QStringLiteral("-f"), zipPath, folderName});
        if (process.waitForFinished(30000) && process.exitCode() == 0
            && QFile::exists(zipPath)) {
            return true;
        }
        QFile::remove(zipPath);
    }

    QString escapedFolder = folderPath;
    escapedFolder.replace(QLatin1Char('\''), QStringLiteral("''"));
    QString escapedZip = zipPath;
    escapedZip.replace(QLatin1Char('\''), QStringLiteral("''"));
    const QString script =
        QStringLiteral("Compress-Archive -LiteralPath '%1' "
                       "-DestinationPath '%2' -Force")
            .arg(escapedFolder, escapedZip);
    QProcess::execute(QStringLiteral("powershell"),
                      {QStringLiteral("-NoProfile"),
                       QStringLiteral("-NonInteractive"),
                       QStringLiteral("-Command"), script});
    return QFile::exists(zipPath);
}

void pruneBundles(const QString &rootDir) {
    QDir dir(rootDir);
    const QFileInfoList entries = dir.entryInfoList(
        {QStringLiteral("uxplay-diagnostics-*")},
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name | QDir::Reversed);

    QSet<QString> keep;
    for (const QFileInfo &entry : entries) {
        const QString stem =
            entry.isDir() ? entry.fileName() : entry.completeBaseName();
        if (!keep.contains(stem) && keep.size() < kMaxBundles) {
            keep.insert(stem);
        }
    }

    int removed = 0;
    for (const QFileInfo &entry : entries) {
        const QString stem =
            entry.isDir() ? entry.fileName() : entry.completeBaseName();
        if (keep.contains(stem)) {
            continue;
        }
        if (entry.isDir()) {
            if (QDir(entry.absoluteFilePath()).removeRecursively()) {
                ++removed;
            }
        } else if (QFile::remove(entry.absoluteFilePath())) {
            ++removed;
        }
    }
    if (removed > 0) {
        qInfo() << "Pruned old diagnostics entries:" << removed;
    }
}

#ifdef Q_OS_WIN
struct ProcessSnapshot {
    qint64 cpu100ns = 0;
    qint64 workingSet = 0;
    qint64 privateBytes = 0;
    int handles = 0;
    bool cpuValid = false;
    bool memoryValid = false;
};

qint64 fileTimeTo100ns(const FILETIME &time) {
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return static_cast<qint64>(value.QuadPart);
}

ProcessSnapshot readProcessSnapshot() {
    ProcessSnapshot snapshot;

    FILETIME creation = {};
    FILETIME exit = {};
    FILETIME kernel = {};
    FILETIME user = {};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
        snapshot.cpu100ns = fileTimeTo100ns(kernel) + fileTimeTo100ns(user);
        snapshot.cpuValid = true;
    }

    PROCESS_MEMORY_COUNTERS_EX memory = {};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
            sizeof(memory))) {
        snapshot.workingSet = static_cast<qint64>(memory.WorkingSetSize);
        snapshot.privateBytes = static_cast<qint64>(memory.PrivateUsage);
        snapshot.memoryValid = true;
    }

    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
        snapshot.handles = static_cast<int>(handles);
    }
    return snapshot;
}
#endif

} // namespace

DiagnosticsRecorder::DiagnosticsRecorder(QObject *parent) : QObject(parent) {
    m_startedAt = QDateTime::currentDateTime();
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    m_timer->setTimerType(Qt::CoarseTimer);
    connect(m_timer, &QTimer::timeout, this, &DiagnosticsRecorder::sample);
}

void DiagnosticsRecorder::setSettings(const AppSettings &settings) {
    m_settings = settings;
}

void DiagnosticsRecorder::setEnabled(bool enabled) {
    if (m_enabled == enabled) {
        if (enabled && m_timer && !m_timer->isActive()) {
            m_timer->start();
        }
        return;
    }

    m_enabled = enabled;
    if (!m_timer) {
        return;
    }

    if (enabled) {
        m_clock.restart();
        m_lastCpu100ns = -1;
        m_lastSampleMs = 0;
        m_timer->start();
        sample();
    } else {
        m_timer->stop();
    }
}

void DiagnosticsRecorder::setStatus(const QString &status) {
    m_status = status;
}

void DiagnosticsRecorder::event(const QString &category,
                                const QString &message,
                                const QJsonObject &data) {
    if (!m_enabled) {
        return;
    }

    EventRecord record;
    record.time = QDateTime::currentDateTime();
    record.category = category;
    record.message = message;
    record.data = data;
    m_events.append(record);

    if (m_events.size() > kMaxEvents) {
        m_events.remove(0, m_events.size() - kMaxEvents);
    }
}

void DiagnosticsRecorder::sample() {
#ifdef Q_OS_WIN
    const ProcessSnapshot snapshot = readProcessSnapshot();
#else
    const ProcessSnapshot snapshot;
#endif

    const qint64 nowMs = m_clock.elapsed();
    double cpuPercent = -1.0;
    if (snapshot.cpuValid && m_lastCpu100ns >= 0 && nowMs > m_lastSampleMs) {
        const double elapsedSeconds = (nowMs - m_lastSampleMs) / 1000.0;
        const double deltaSeconds =
            (snapshot.cpu100ns - m_lastCpu100ns) / 10000000.0;
        if (elapsedSeconds > 0.0) {
            cpuPercent = (deltaSeconds / elapsedSeconds) * 100.0
                         / qMax(1, QThread::idealThreadCount());
        }
    }

    MetricRecord record;
    record.time = QDateTime::currentDateTime();
    record.cpuPercent = cpuPercent;
    record.workingSet = snapshot.workingSet;
    record.privateBytes = snapshot.privateBytes;
    record.handles = snapshot.handles;
    record.status = m_status;
    m_metrics.append(record);

    if (m_metrics.size() > kMaxMetrics) {
        m_metrics.remove(0, m_metrics.size() - kMaxMetrics);
    }

    if (snapshot.cpuValid) {
        m_lastCpu100ns = snapshot.cpu100ns;
    }
    m_lastSampleMs = nowMs;
}

QString DiagnosticsRecorder::exportBundle(QString *error) {
    if (error) {
        error->clear();
    }

    QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath()
               + QStringLiteral("/AppData/Local/leapbtw/uxplay-windows");
    }

    const QString rootDir = base + QStringLiteral("/diagnostics");
    const QString stamp =
        QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString folderName =
        QStringLiteral("uxplay-diagnostics-") + stamp;
    const QString folderPath = rootDir + QLatin1Char('/') + folderName;
    const QString zipPath = rootDir + QLatin1Char('/') + folderName
                            + QStringLiteral(".zip");

    if (!QDir().mkpath(folderPath)) {
        if (error) {
            *error = QStringLiteral("Cannot create %1").arg(folderPath);
        }
        return {};
    }

    const QString hostName = QSysInfo::machineHostName();
    const QString displayName = m_settings.displayName;

    QJsonObject manifest;
    manifest[QStringLiteral("schema_version")] = 1;
    manifest[QStringLiteral("created_at")] =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("session_started_at")] =
        m_startedAt.toString(Qt::ISODateWithMs);
    manifest[QStringLiteral("app_version")] = QStringLiteral(UXPLAY_APP_VERSION);
    manifest[QStringLiteral("git_sha")] = QStringLiteral(UXPLAY_GIT_SHA);
    manifest[QStringLiteral("build_type")] = QStringLiteral(UXPLAY_BUILD_TYPE);
    manifest[QStringLiteral("qt_version")] = QString::fromLatin1(qVersion());
    manifest[QStringLiteral("gstreamer_version")] =
        QString::fromUtf8(gst_version_string());
    manifest[QStringLiteral("os")] = QSysInfo::prettyProductName();
    manifest[QStringLiteral("kernel")] = QSysInfo::kernelVersion();
    manifest[QStringLiteral("architecture")] =
        QSysInfo::currentCpuArchitecture();
    manifest[QStringLiteral("cpu_count")] = QThread::idealThreadCount();
    manifest[QStringLiteral("screen")] = screenInfo();
    manifest[QStringLiteral("app_dir")] =
        redactText(QCoreApplication::applicationDirPath(), displayName,
                   hostName);
    manifest[QStringLiteral("log_path")] =
        redactText(applog::filePath(), displayName, hostName);
    manifest[QStringLiteral("settings_path")] =
        redactText(AppSettings::filePath(), displayName, hostName);
    manifest[QStringLiteral("engine_args")] =
        redactedEngineArgs(m_settings, displayName, hostName);
    manifest[QStringLiteral("recording_enabled")] = m_enabled;
    manifest[QStringLiteral("event_count")] = m_events.size();
    manifest[QStringLiteral("metric_count")] = m_metrics.size();
    manifest[QStringLiteral("last_status")] =
        redactText(m_status, displayName, hostName);
    manifest[QStringLiteral("privacy")] =
        QStringLiteral("Locally generated and redacted; review before sharing.");

    QJsonObject lastClient;
    for (int i = m_events.size() - 1; i >= 0; --i) {
        if (m_events.at(i).category == QStringLiteral("client")
            && m_events.at(i).message == QStringLiteral("connected")) {
            lastClient = redactValue(m_events.at(i).data, displayName, hostName)
                             .toObject();
            break;
        }
    }
    manifest[QStringLiteral("last_client")] = lastClient;

    writeJson(folderPath + QStringLiteral("/manifest.json"),
              QJsonDocument(manifest), error);

    QString eventsJson;
    for (const EventRecord &record : m_events) {
        QJsonObject object;
        object[QStringLiteral("ts")] =
            record.time.toString(Qt::ISODateWithMs);
        object[QStringLiteral("category")] = record.category;
        object[QStringLiteral("message")] =
            redactText(record.message, displayName, hostName);
        object[QStringLiteral("data")] =
            redactValue(record.data, displayName, hostName);
        eventsJson += QString::fromUtf8(
                          QJsonDocument(object).toJson(QJsonDocument::Compact))
                      + QLatin1Char('\n');
    }
    writeText(folderPath + QStringLiteral("/events.jsonl"), eventsJson,
              error);

    QString metricsJson;
    for (const MetricRecord &record : m_metrics) {
        QJsonObject object;
        object[QStringLiteral("ts")] =
            record.time.toString(Qt::ISODateWithMs);
        object[QStringLiteral("cpu_pct")] =
            record.cpuPercent < 0 ? QJsonValue(QJsonValue::Null)
                                  : QJsonValue(record.cpuPercent);
        object[QStringLiteral("working_set_mb")] =
            record.workingSet / (1024.0 * 1024.0);
        object[QStringLiteral("private_mb")] =
            record.privateBytes / (1024.0 * 1024.0);
        object[QStringLiteral("handles")] = record.handles;
        object[QStringLiteral("status")] =
            redactText(record.status, displayName, hostName);
        metricsJson += QString::fromUtf8(
                           QJsonDocument(object).toJson(QJsonDocument::Compact))
                       + QLatin1Char('\n');
    }
    writeText(folderPath + QStringLiteral("/metrics.jsonl"), metricsJson,
              error);

    writeJson(folderPath + QStringLiteral("/settings.redacted.json"),
              QJsonDocument(redactedSettings(m_settings, displayName,
                                             hostName)),
              error);

    QStringList system;
    system << QStringLiteral("uxplay-windows diagnostics")
           << QStringLiteral("generated: %1")
                  .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
           << QStringLiteral("app_version: %1").arg(QStringLiteral(UXPLAY_APP_VERSION))
           << QStringLiteral("git_sha: %1").arg(QStringLiteral(UXPLAY_GIT_SHA))
           << QStringLiteral("build_type: %1").arg(QStringLiteral(UXPLAY_BUILD_TYPE))
           << QStringLiteral("qt: %1").arg(QString::fromLatin1(qVersion()))
           << QStringLiteral("gstreamer: %1")
                  .arg(QString::fromUtf8(gst_version_string()))
           << QStringLiteral("os: %1").arg(QSysInfo::prettyProductName())
           << QStringLiteral("kernel: %1").arg(QSysInfo::kernelVersion())
           << QStringLiteral("architecture: %1")
                  .arg(QSysInfo::currentCpuArchitecture())
           << QStringLiteral("cpu_count: %1").arg(QThread::idealThreadCount())
           << QStringLiteral("screen: %1").arg(screenInfo())
           << QStringLiteral("hardware: %1").arg(hardwareInfo())
           << QStringLiteral("app_dir: %1")
                  .arg(redactText(QCoreApplication::applicationDirPath(),
                                  displayName, hostName))
           << QStringLiteral("log: %1")
                  .arg(redactText(applog::filePath(), displayName, hostName))
           << QStringLiteral("settings: %1")
                  .arg(redactText(AppSettings::filePath(), displayName,
                                  hostName))
           << QStringLiteral("engine_args: %1")
                  .arg(redactedEngineArgs(m_settings, displayName, hostName))
           << QStringLiteral("arguments: %1")
                  .arg(redactText(QCoreApplication::arguments().join(
                                      QLatin1Char(' ')),
                                  displayName, hostName));
    writeText(folderPath + QStringLiteral("/system.txt"),
              system.join(QLatin1Char('\n')) + QLatin1Char('\n'), error);

    writeRedactedLogs(folderPath, displayName, hostName);

    const QString readme = QStringLiteral(
        "uxplay-windows diagnostics bundle\n"
        "---------------------------------\n"
        "This bundle was generated locally. It contains recent events and\n"
        "process metrics, logs, redacted settings, and system information.\n"
        "Nothing is uploaded automatically.\n"
        "\n"
        "Redaction:\n"
        "- device names, UDIDs, MAC addresses, IPv4 addresses and user\n"
        "  profile paths are replaced with placeholders;\n"
        "- the AirPlay client model and technical metadata are kept.\n"
        "\n"
        "How to use:\n"
        "1. Review the files before sharing.\n"
        "2. Attach the .zip to the project issue template.\n"
        "3. Describe what happened, when, and whether audio or video was\n"
        "   affected.\n");
    writeText(folderPath + QStringLiteral("/README.txt"), readme, error);

    const QString result =
        zipBundle(rootDir, folderPath, folderName, zipPath) ? zipPath
                                                            : folderPath;
    pruneBundles(rootDir);
    m_lastBundlePath = result;
    return result;
}
