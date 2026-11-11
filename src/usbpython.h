#pragma once

#include <QString>
#include <QStringList>

/// Locates the Python interpreter that runs the pymobiledevice3 sidecar.
///
/// UXPLAY_USB_PYTHON may contain a full command (for example
/// "py -3.11" or "C:/Python311/python.exe"). Otherwise the py launcher
/// is preferred, with a plain python fallback.
struct UsbPython {
    QString program;
    QStringList arguments;

    bool valid() const { return !program.isEmpty(); }
    QStringList command(const QStringList &extra) const {
        return arguments + extra;
    }

    static UsbPython find();
};
