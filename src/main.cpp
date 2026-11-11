#include "logging.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // if the process was started from a console (CMD/PowerShell), attach to it
    // so we can see qDebug() output.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // redirect stdout and stderr to the console
        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);
        std::ios::sync_with_stdio();
    }
#endif

    QApplication app(argc, argv);
    app.setOrganizationName("leapbtw");
    app.setApplicationName("uxplay-windows");
    app.setWindowIcon(
        QIcon(QApplication::applicationDirPath() + "/resources/icon.ico"));

    // Only one tray process may own the engine and the mDNS registration.
    // A second launch asks the first instance to show its window instead.
    const QString instanceKey = QStringLiteral("uxplay-windows-single-instance");
    const QStringList startupArgs = QCoreApplication::arguments();

    QByteArray remoteCommand;
    if (startupArgs.contains(QStringLiteral("--stop"))) {
        remoteCommand = "stop";
    } else if (startupArgs.contains(QStringLiteral("--start"))) {
        remoteCommand = "start";
    }
    if (!remoteCommand.isEmpty()) {
        QLocalSocket socket;
        socket.connectToServer(instanceKey);
        if (socket.waitForConnected(1000)) {
            socket.write(remoteCommand);
            socket.flush();
            socket.waitForBytesWritten(500);
            return 0;
        }
        qWarning() << "No running uxplay-windows instance for command:"
                   << remoteCommand;
        return 0;
    }

    const bool restarting =
        startupArgs.contains(QStringLiteral("--restart"));
    QLockFile instanceLock(
        QDir(QDir::tempPath()).filePath(QStringLiteral("uxplay-windows.lock")));
    if (!instanceLock.tryLock(restarting ? 10000 : 100)) {
        if (!restarting) {
            QLocalSocket socket;
            socket.connectToServer(instanceKey);
            if (socket.waitForConnected(1000)) {
                socket.write("show");
                socket.flush();
                socket.waitForBytesWritten(500);
            } else {
                QMessageBox::information(
                    nullptr, "uxplay-windows",
                    "uxplay-windows is already running.\nCheck the system tray.");
            }
        } else {
            QMessageBox::warning(
                nullptr, "uxplay-windows",
                "Another uxplay-windows instance is still running.");
        }
        return 0;
    }

    applog::install();
    qInfo() << "uxplay-windows starting";
    qInfo() << "Executable:" << QCoreApplication::applicationFilePath();
    qInfo() << "Arguments:" << QCoreApplication::arguments();

    QString appPath = QApplication::applicationDirPath();

    const QString bundledPlugins = appPath + "/lib/gstreamer-1.0";
    if (QDir(bundledPlugins).exists()) {
        qputenv("GST_PLUGIN_PATH",
                QDir::toNativeSeparators(bundledPlugins).toUtf8());
    } else {
        qInfo() << "No bundled GStreamer plugins; using GST_PLUGIN_PATH:"
                << qgetenv("GST_PLUGIN_PATH");
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = QDir::toNativeSeparators(appPath) + ";" + env.value("PATH");
    qputenv("PATH", path.toUtf8());

    QLocalServer instanceServer;
    QLocalServer::removeServer(instanceKey);
    if (!instanceServer.listen(instanceKey)) {
        qWarning() << "Single-instance IPC server failed:"
                   << instanceServer.errorString();
    }

    app.setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qCritical() << "System tray not available";
        QMessageBox::critical(nullptr, "Error", "System tray not available.");
        applog::shutdown();
        return 1;
    }

    MainWindow window;
    auto dispatchCommand = [&window](QLocalSocket *socket) {
        if (!socket) return;
        const QByteArray command = socket->readAll().trimmed();
        if (command.isEmpty()) return;
        if (command == "stop") {
            window.setServerEnabled(false);
        } else if (command == "start") {
            window.setServerEnabled(true);
        } else {
            window.showNormal();
            window.raise();
            window.activateWindow();
        }
        socket->deleteLater();
    };
    QObject::connect(
        &instanceServer, &QLocalServer::newConnection, &app,
        [&instanceServer, dispatchCommand]() {
            while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, socket,
                                 [dispatchCommand, socket]() {
                                     dispatchCommand(socket);
                                 });
                dispatchCommand(socket);
            }
        });

    const int rc = app.exec();
    applog::shutdown();
    return rc;
}
