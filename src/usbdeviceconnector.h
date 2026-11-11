#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

#include "usbpython.h"

class QTimer;

/// Runs the USB preflight for the CoreDevice screen stream:
/// detect -> pair/trust -> Developer Mode -> mount DDI -> ready.
class UsbDeviceConnector : public QObject {
    Q_OBJECT

public:
    explicit UsbDeviceConnector(QObject *parent = nullptr);
    ~UsbDeviceConnector() override;

    void connectDevice();
    void cancel();
    bool isReady() const { return m_ready; }
    QString powerSummary() const { return m_powerSummary; }
    bool isCharging() const { return m_charging; }
    int batteryPercent() const { return m_batteryPercent; }
    int adapterWatts() const { return m_adapterWatts; }

signals:
    void statusChanged(const QString &text, bool busy);
    void ready(const QString &deviceName);
    void failed(const QString &message);

private:
    using Done = std::function<void(bool, const QByteArray &)>;

    void run(const QStringList &args, int timeoutMs, Done done);
    void complete(bool ok, const QByteArray &output);
    void finish(bool ok, const QString &message);
    void parseDeviceList(bool ok, const QByteArray &output);
    void checkPairing();
    void pairDevice();
    void checkDeveloperMode();
    void mountDdi();
    void readPower();
    void buildPowerSummary();
    static QString lastLine(const QByteArray &output);

    UsbPython m_python;
    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    Done m_onDone;
    QString m_deviceName;
    QString m_udid;
    QString m_powerSummary;
    int m_batteryPercent = -1;
    int m_adapterWatts = 0;
    bool m_charging = false;
    bool m_ready = false;
    bool m_busy = false;
    bool m_completing = false;
};
