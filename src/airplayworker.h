#pragma once

#include <QThread>
#include <QStringList>

class AirPlayWorker : public QThread {
    Q_OBJECT

public:
    explicit AirPlayWorker(QObject *parent = nullptr);
    void setArgs(const QStringList &args);
    void publishMediaInfo(const QString &key, const QString &value);
    void publishClientEvent(const QString &event,
                            const QString &name,
                            const QString &model,
                            const QString &deviceid,
                            int openConnections);
    void publishEngineLog(int level, const QString &message);
    void publishEngineReady();
    void stopAirplay();

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &message);
    void mediaInfoChanged(const QString &key, const QString &value);
    void clientConnected(const QString &name, const QString &model, const QString &deviceid);
    void clientDisconnected();
    void clientRejected(const QString &name, const QString &model, const QString &deviceid);
    void connectionCountChanged(int count);

protected:
    void run() override;

private:
    QStringList m_args;
};
