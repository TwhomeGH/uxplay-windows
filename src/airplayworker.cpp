#include "airplayworker.h"
#include "uxplay_api.h"
#include <QDebug>
#include <vector>

static AirPlayWorker *activeWorker = nullptr;

static void mediaInfoCallback(const char *key, const char *value) {
    if (!activeWorker || !key || !value) return;

    activeWorker->publishMediaInfo(QString::fromUtf8(key),
                                   QString::fromUtf8(value));
}

static void clientCallback(const char *event, const char *name, const char *model,
                           const char *deviceid, int openConnections) {
    if (!activeWorker || !event) return;

    activeWorker->publishClientEvent(QString::fromUtf8(event),
                                     QString::fromUtf8(name ? name : ""),
                                     QString::fromUtf8(model ? model : ""),
                                     QString::fromUtf8(deviceid ? deviceid : ""),
                                     openConnections);
}

static void logCallback(int level, const char *message) {
    if (!activeWorker || !message) return;

    activeWorker->publishEngineLog(level, QString::fromUtf8(message));
}

static bool isHighFrequencyEngineLog(int level, const QString &rawMessage) {
    const QString message = rawMessage.trimmed();
    // DEBUG_DATA / DEBUG packet and HTTP chatter is useful when explicitly
    // tracing, but it drowns the incident timeline during normal debugging.
    if (level >= 7) {
        static const QStringList noisyPrefixes = {
            QStringLiteral("raop_rtp video:"),
            QStringLiteral("raop_rtp audio:"),
            QStringLiteral("raop_rtp type_c"),
            QStringLiteral("raop_rtp sync:"),
            QStringLiteral("raop_ntp"),
            QStringLiteral("httpd receiving on socket"),
            QStringLiteral("httpd: current connections:"),
            QStringLiteral("connection "),
            QStringLiteral("new request, connection"),
            QStringLiteral("conn_request"),
            QStringLiteral("client closed connection"),
            QStringLiteral("removing connection"),
            QStringLiteral("Destroying connection"),
            QStringLiteral("Open connections:"),
            QStringLiteral("GStreamer unhandled audio bus message"),
            QStringLiteral("Received video streaming performance info packet"),
            QStringLiteral("GStreamer h264 bus message"),
            QStringLiteral("GStreamer h265 bus message"),
        };
        for (const QString &prefix : noisyPrefixes) {
            if (message.startsWith(prefix)) {
                return true;
            }
        }
        if (message.startsWith(QStringLiteral("RTSP/1.0"))
            || message.startsWith(QStringLiteral("POST "))
            || message.startsWith(QStringLiteral("TEARDOWN "))
            || message.startsWith(QStringLiteral("CSeq:"))
            || message.startsWith(QStringLiteral("DACP-ID:"))
            || message.startsWith(QStringLiteral("Active-Remote:"))) {
            return true;
        }
    }

    // /feedback is an AirPlay keepalive. A healthy connection sends it rarely;
    // a retry loop can produce dozens per second, so keep the first useful
    // lifecycle lines but do not write every request.
    return message.contains(QStringLiteral("/feedback"))
        || message.contains(QStringLiteral("raop_handler_feedback"))
        || message.contains(QStringLiteral("GStreamer h264 bus message"))
        || message.contains(QStringLiteral("GStreamer h265 bus message"));
}

static void readyCallback() {
    if (!activeWorker) return;

    activeWorker->publishEngineReady();
}

AirPlayWorker::AirPlayWorker(QObject *parent) : QThread(parent) {}

void AirPlayWorker::setArgs(const QStringList &args) {
    m_args = args;
}

void AirPlayWorker::publishMediaInfo(const QString &key, const QString &value) {
    emit mediaInfoChanged(key, value);
}

void AirPlayWorker::publishClientEvent(const QString &event,
                                       const QString &name,
                                       const QString &model,
                                       const QString &deviceid,
                                       int openConnections) {
    if (event == "connected") {
        emit clientConnected(name, model, deviceid);
    } else if (event == "disconnected") {
        emit clientDisconnected();
    } else if (event == "rejected") {
        emit clientRejected(name, model, deviceid);
    }
    emit connectionCountChanged(openConnections);
}

void AirPlayWorker::setEngineTrace(bool enabled) {
    m_engineTrace = enabled;
}

void AirPlayWorker::publishEngineLog(int level, const QString &message) {
    if (!m_engineTrace && isHighFrequencyEngineLog(level, message)) {
        return;
    }
    switch (level) {
    case 0:
    case 1:
    case 2:
    case 3:
        qCritical().noquote() << "[engine]" << message;
        break;
    case 4:
        qWarning().noquote() << "[engine]" << message;
        break;
    case 7:
    case 8:
        qDebug().noquote() << "[engine]" << message;
        break;
    default:
        qInfo().noquote() << "[engine]" << message;
        break;
    }
}

void AirPlayWorker::publishEngineReady() {
    emit started();
}

void AirPlayWorker::run() {
    std::vector<QByteArray> argBytes;
    std::vector<char *> argv;
    
    argBytes.push_back(QByteArray("uxplay"));
    argv.push_back(argBytes.back().data());

    for (const auto &arg : m_args) {
        if (arg.trimmed().isEmpty()) continue;
        argBytes.push_back(arg.toUtf8());
        argv.push_back(argBytes.back().data());
    }

    qDebug() << "Starting UxPlay engine with arguments:" << m_args;
    activeWorker = this;
    set_uxplay_media_info_callback(mediaInfoCallback);
    set_uxplay_client_callback(clientCallback);
    set_uxplay_log_callback(logCallback);
    set_uxplay_ready_callback(readyCallback);

    const int ret = start_uxplay(static_cast<int>(argv.size()), argv.data());

    set_uxplay_ready_callback(nullptr);
    set_uxplay_media_info_callback(nullptr);
    set_uxplay_client_callback(nullptr);
    set_uxplay_log_callback(nullptr);
    activeWorker = nullptr;

    if (ret != 0 && !isInterruptionRequested()) {
        emit errorOccurred(QString("Engine exited with code %1").arg(ret));
    }
    emit stopped();
}

void AirPlayWorker::stopAirplay() {
    stop_uxplay();
}
