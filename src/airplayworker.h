#pragma once

#include <QThread>
#include <QStringList>

class AirPlayWorker : public QThread {
    Q_OBJECT

public:
    explicit AirPlayWorker(QObject *parent = nullptr);
    void setArgs(const QStringList &args);
    void publishMediaInfo(const QString &key, const QString &value);
    void stopAirplay();

signals:
    void started();
    void stopped();
    void errorOccurred(const QString &message);
    void mediaInfoChanged(const QString &key, const QString &value);

protected:
    void run() override;

private:
    QStringList m_args;
};
