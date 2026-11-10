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

void AirPlayWorker::publishEngineLog(int level, const QString &message) {
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
    emit started();
    activeWorker = this;
    set_uxplay_media_info_callback(mediaInfoCallback);
    set_uxplay_client_callback(clientCallback);
    set_uxplay_log_callback(logCallback);

    int ret = 0;

    // Loop con controllo di interruzione
    while (!isInterruptionRequested()) {
        ret = start_uxplay(static_cast<int>(argv.size()), argv.data());

        // Se il processo termina, esci dal loop
        if (ret != 0) {
            emit errorOccurred(QString("Engine exited with code %1").arg(ret));
            break;
        }
    }

    set_uxplay_media_info_callback(nullptr);
    set_uxplay_client_callback(nullptr);
    set_uxplay_log_callback(nullptr);
    activeWorker = nullptr;
    emit stopped();
}

void AirPlayWorker::stopAirplay() {
    stop_uxplay();
}
