#include "usbdeviceconnector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>

UsbDeviceConnector::UsbDeviceConnector(QObject *parent) : QObject(parent) {
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        QByteArray output;
        if (m_process) {
            output = m_process->readAllStandardOutput();
            m_process->kill();
        }
        complete(false, output + QByteArray("\nTimed out."));
    });
}

UsbDeviceConnector::~UsbDeviceConnector() {
    cancel();
}

void UsbDeviceConnector::connectDevice() {
    if (m_busy) {
        return;
    }

    m_ready = false;
    m_busy = true;
    m_deviceName.clear();
    m_udid.clear();
    m_python = UsbPython::find();
    if (!m_python.valid()) {
        finish(false,
               QStringLiteral("Python 3 with pymobiledevice3 was not found. "
                              "Set UXPLAY_USB_PYTHON to a Python interpreter."));
        return;
    }

    Q_EMIT statusChanged(QStringLiteral("Checking USB device..."), true);
    run(QStringList{QStringLiteral("usbmux"), QStringLiteral("list")}, 15000,
        [this](bool ok, const QByteArray &output) {
            parseDeviceList(ok, output);
        });
}

void UsbDeviceConnector::parseDeviceList(bool ok,
                                         const QByteArray &output) {
    if (!ok) {
        finish(false, QStringLiteral("Could not query usbmux. ") +
                          lastLine(output));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        finish(false, QStringLiteral("usbmux returned invalid JSON."));
        return;
    }

    const QJsonArray devices = document.array();
    if (devices.isEmpty()) {
        finish(false,
               QStringLiteral("No USB device found. Connect and unlock the "
                              "iPad, then press Connect again."));
        return;
    }

    const QJsonObject device = devices.first().toObject();
    m_deviceName = device.value(QStringLiteral("DeviceName"))
                       .toString(QStringLiteral("iPad"));
    m_udid = device.value(QStringLiteral("UniqueDeviceID"))
                 .toString(device.value(QStringLiteral("Identifier"))
                               .toString());
    Q_EMIT statusChanged(
        QStringLiteral("Found %1; checking pairing...").arg(m_deviceName),
        true);
    checkPairing();
}

void UsbDeviceConnector::checkPairing() {
    QStringList args{QStringLiteral("lockdown"), QStringLiteral("info")};
    if (!m_udid.isEmpty()) {
        args << QStringLiteral("--udid") << m_udid;
    }
    run(args, 20000, [this](bool ok, const QByteArray &) {
        if (ok) {
            Q_EMIT statusChanged(
                QStringLiteral("Paired; checking Developer Mode..."), true);
            checkDeveloperMode();
        } else {
            pairDevice();
        }
    });
}

void UsbDeviceConnector::pairDevice() {
    Q_EMIT statusChanged(
        QStringLiteral("Tap Trust This Computer on the iPad..."), true);

    QStringList args{QStringLiteral("lockdown"), QStringLiteral("pair")};
    if (!m_udid.isEmpty()) {
        args << QStringLiteral("--udid") << m_udid;
    }
    run(args, 90000, [this](bool ok, const QByteArray &output) {
        if (!ok) {
            finish(false,
                   QStringLiteral("Pairing failed. Unlock the iPad, tap Trust "
                                  "This Computer, then press Connect again. ") +
                       lastLine(output));
            return;
        }
        Q_EMIT statusChanged(
            QStringLiteral("Paired; checking Developer Mode..."), true);
        checkDeveloperMode();
    });
}

void UsbDeviceConnector::checkDeveloperMode() {
    run(QStringList{QStringLiteral("amfi"),
                    QStringLiteral("developer-mode-status")},
        20000, [this](bool ok, const QByteArray &output) {
            const QString text =
                QString::fromUtf8(output).toLower();
            if (!ok ||
                (!text.contains(QStringLiteral("true")) &&
                 !text.contains(QStringLiteral("false")))) {
                finish(false,
                       QStringLiteral("Could not read Developer Mode status. ") +
                           lastLine(output));
                return;
            }
            if (text.contains(QStringLiteral("false"))) {
                finish(false,
                       QStringLiteral("Developer Mode is off. Enable it in "
                                      "Settings > Privacy & Security > "
                                      "Developer Mode, reboot, then press "
                                      "Connect again."));
                return;
            }
            Q_EMIT statusChanged(
                QStringLiteral("Developer Mode on; mounting the DDI..."),
                true);
            mountDdi();
        });
}

void UsbDeviceConnector::mountDdi() {
    run(QStringList{QStringLiteral("mounter"),
                    QStringLiteral("auto-mount")},
        300000, [this](bool ok, const QByteArray &output) {
            if (!ok) {
                finish(false,
                       QStringLiteral("Developer Disk Image mount failed. ") +
                           lastLine(output));
                return;
            }
            finish(true, QStringLiteral("USB ready: %1").arg(m_deviceName));
        });
}

void UsbDeviceConnector::run(const QStringList &args, int timeoutMs,
                             Done done) {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_timeout) {
        m_timeout->stop();
    }

    m_onDone = std::move(done);
    m_completing = false;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    complete(false, m_process
                                        ? m_process->errorString().toUtf8()
                                        : QByteArray("Failed to start"));
                }
            });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
                const QByteArray output =
                    m_process ? m_process->readAllStandardOutput()
                              : QByteArray();
                complete(code == 0 && status == QProcess::NormalExit, output);
            });

    m_timeout->start(timeoutMs);
    QStringList fullArgs{QStringLiteral("-m"),
                         QStringLiteral("pymobiledevice3")};
    fullArgs << args;
    m_process->start(m_python.program, m_python.command(fullArgs));
}

void UsbDeviceConnector::complete(bool ok, const QByteArray &output) {
    if (m_completing || !m_onDone) {
        return;
    }
    m_completing = true;
    if (m_timeout) {
        m_timeout->stop();
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    Done callback = std::move(m_onDone);
    m_onDone = nullptr;
    m_completing = false;
    callback(ok, output);
}

void UsbDeviceConnector::finish(bool ok, const QString &message) {
    if (m_timeout) {
        m_timeout->stop();
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_onDone = nullptr;
    m_busy = false;
    m_ready = ok;

    Q_EMIT statusChanged(message, false);
    if (ok) {
        Q_EMIT ready(m_deviceName);
    } else {
        Q_EMIT failed(message);
    }
}

void UsbDeviceConnector::cancel() {
    if (m_timeout) {
        m_timeout->stop();
    }
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_onDone = nullptr;
    m_busy = false;
    m_ready = false;
    Q_EMIT statusChanged(QStringLiteral("USB check cancelled"), false);
}

QString UsbDeviceConnector::lastLine(const QByteArray &output) {
    const QList<QByteArray> lines = output.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString line = QString::fromUtf8(lines.at(i)).trimmed();
        if (!line.isEmpty()) {
            return line.left(240);
        }
    }
    return {};
}
