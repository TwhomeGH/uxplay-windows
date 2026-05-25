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

AirPlayWorker::AirPlayWorker(QObject *parent) : QThread(parent) {}

void AirPlayWorker::setArgs(const QStringList &args) {
    m_args = args;
}

void AirPlayWorker::publishMediaInfo(const QString &key, const QString &value) {
    emit mediaInfoChanged(key, value);
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
    activeWorker = nullptr;
    emit stopped();
}

void AirPlayWorker::stopAirplay() {
    stop_uxplay();
}
