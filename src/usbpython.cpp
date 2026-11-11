#include "usbpython.h"

#include <QProcess>
#include <QStandardPaths>

UsbPython UsbPython::find() {
    UsbPython result;

    const QString override = qEnvironmentVariable("UXPLAY_USB_PYTHON");
    if (!override.isEmpty()) {
        const QStringList parts = QProcess::splitCommand(override);
        if (!parts.isEmpty()) {
            result.program = parts.first();
            result.arguments = parts.mid(1);
        }
    }

    if (result.program.isEmpty()) {
        const QString py =
            QStandardPaths::findExecutable(QStringLiteral("py"));
        if (!py.isEmpty()) {
            result.program = py;
            result.arguments << QStringLiteral("-3.11");
        } else {
            const QString python =
                QStandardPaths::findExecutable(QStringLiteral("python"));
            if (!python.isEmpty()) {
                result.program = python;
            }
        }
    }

    return result;
}
