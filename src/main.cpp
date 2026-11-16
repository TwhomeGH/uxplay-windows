#include "logging.h"
#include "mainwindow.h"
#include "settings.h"

#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QLockFile>

#ifndef UXPLAY_APP_VERSION
#define UXPLAY_APP_VERSION "dev"
#endif

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

#ifdef _WIN32
// A kernel mutex is the primary single-instance guard: it is atomic, cannot
// go stale after a crash, and is released automatically when the process exits.
// The old QLockFile could remove a live lock when getLockInfo() failed and then
// let a second process start its own engine.
static constexpr wchar_t kInstanceMutexName[] =
    L"Local\\uxplay-windows-single-instance";

static HANDLE acquireInstanceMutex(int timeoutMs) {
    const int stepMs = 100;
    int waitedMs = 0;
    while (true) {
        SetLastError(ERROR_SUCCESS);
        HANDLE mutex = CreateMutexW(nullptr, FALSE, kInstanceMutexName);
        if (!mutex) {
            return nullptr;
        }
        const DWORD wait = WaitForSingleObject(mutex, 0);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
            // WAIT_ABANDONED means the previous owner died while holding it;
            // the kernel transfers ownership to this process immediately.
            return mutex;
        }
        CloseHandle(mutex);
        if (waitedMs >= timeoutMs) {
            return nullptr;
        }
        const int remaining = timeoutMs - waitedMs;
        const int sleepMs = remaining < stepMs ? remaining : stepMs;
        Sleep(static_cast<DWORD>(sleepMs));
        waitedMs += sleepMs;
    }
}
#endif

static bool showExistingInstance(int timeoutMs) {
    const QString instanceKey =
        QStringLiteral("uxplay-windows-single-instance");
    int waitedMs = 0;
    while (true) {
        QLocalSocket socket;
        socket.connectToServer(instanceKey);
        if (socket.waitForConnected(500)) {
            socket.write("show");
            socket.flush();
            socket.waitForBytesWritten(500);
            return true;
        }
        if (waitedMs >= timeoutMs) {
            return false;
        }
        const int remaining = timeoutMs - waitedMs;
        const int sleepMs = remaining < 100 ? remaining : 100;
        QThread::msleep(static_cast<unsigned long>(sleepMs));
        waitedMs += sleepMs;
    }
}

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
    app.setApplicationVersion(QStringLiteral(UXPLAY_APP_VERSION));
    app.setWindowIcon(
        QIcon(QApplication::applicationDirPath() + "/resources/icon.ico"));

    // The source language is English. A missing .qm, an empty translation,
    // or an unavailable locale falls back to the English source string.
    const AppSettings startupSettings = AppSettings::load();
    QString requestedLanguage = startupSettings.language;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--lang="))) {
            requestedLanguage = argument.mid(7);
        }
    }
    QStringList languageCandidates;
    if (requestedLanguage.isEmpty()
        || requestedLanguage == QStringLiteral("system")) {
        for (const QString &uiLanguage : QLocale::system().uiLanguages()) {
            languageCandidates << QString(uiLanguage).replace('-', '_');
        }
    } else {
        languageCandidates << QString(requestedLanguage).replace('-', '_');
    }
    languageCandidates.removeDuplicates();

    auto *translator = new QTranslator(&app);
    bool translatorLoaded = false;
    QString loadedLanguage;
    for (const QString &code : languageCandidates) {
        if (code == QStringLiteral("en") || code.isEmpty()) {
            continue;
        }
        const QString resource =
            QStringLiteral(":/i18n/uxplay-windows_%1.qm").arg(code);
        const QString file = QApplication::applicationDirPath()
                             + QStringLiteral("/translations/uxplay-windows_")
                             + code + QStringLiteral(".qm");
        const QString appDirFile =
            QApplication::applicationDirPath()
            + QStringLiteral("/uxplay-windows_") + code
            + QStringLiteral(".qm");
        if (translator->load(resource) || translator->load(file)
            || translator->load(appDirFile)) {
            app.installTranslator(translator);
            translatorLoaded = true;
            loadedLanguage = code;
            qInfo() << "Loaded translation:" << code;
            break;
        }
    }
    if (!translatorLoaded) {
        qInfo() << "No translation loaded; using English source strings";
    }

    // Only one tray process may own the engine and the mDNS registration.
    // A second launch asks the first instance to show its window instead.
    const QString instanceKey = QStringLiteral("uxplay-windows-single-instance");
    const QStringList startupArgs = QCoreApplication::arguments();

    QByteArray remoteCommand;
    const bool launchUsbObs = startupArgs.contains(QStringLiteral("--usb-obs"));
    if (startupArgs.contains(QStringLiteral("--stop"))) {
        remoteCommand = "stop";
    } else if (startupArgs.contains(QStringLiteral("--start"))) {
        remoteCommand = "start";
    } else if (startupArgs.contains(QStringLiteral("--save-diagnostics"))) {
        remoteCommand = "save-diagnostics";
    } else if (startupArgs.contains(QStringLiteral("--diagnostics"))) {
        remoteCommand = "diagnostics";
    } else if (launchUsbObs) {
        remoteCommand = "usb-obs";
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
        if (remoteCommand != "usb-obs") {
            qWarning() << "No running uxplay-windows instance for command:"
                       << remoteCommand;
            return 0;
        }
    }

    const bool restarting =
        startupArgs.contains(QStringLiteral("--restart"));
#ifdef _WIN32
    HANDLE instanceMutex = acquireInstanceMutex(restarting ? 10000 : 0);
    if (!instanceMutex) {
        if (!restarting && showExistingInstance(2000)) {
            return 0;
        }
        if (!restarting) {
            QMessageBox::information(
                nullptr, QStringLiteral("uxplay-windows"),
                QCoreApplication::translate(
                    "main",
                    "uxplay-windows is already running.\nCheck the system tray."));
        } else {
            QMessageBox::warning(
                nullptr, QStringLiteral("uxplay-windows"),
                QCoreApplication::translate(
                    "main",
                    "Another uxplay-windows instance is still running."));
        }
        return 0;
    }
#else
    const QString lockPath =
        QDir(QDir::tempPath()).filePath(QStringLiteral("uxplay-windows.lock"));
    QLockFile instanceLock(lockPath);
    const bool lockAcquired = instanceLock.tryLock();
    if (!lockAcquired) {
        if (!showExistingInstance(2000)) {
            QMessageBox::information(
                nullptr, QStringLiteral("uxplay-windows"),
                QCoreApplication::translate(
                    "main",
                    "uxplay-windows is already running.\nCheck the system tray."));
        }
        return 0;
    }
#endif

    applog::install();
    qInfo() << "uxplay-windows starting";
    qInfo() << "UI language:"
            << (translatorLoaded ? loadedLanguage
                                 : QStringLiteral("en (source; no translation loaded)"));
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
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "Error"),
            QCoreApplication::translate("main", "System tray not available."));
        applog::shutdown();
#ifdef _WIN32
        CloseHandle(instanceMutex);
#endif
        return 1;
    }

    MainWindow window;
    if (launchUsbObs) {
        QTimer::singleShot(0, &window,
                           [&window]() { window.startUsbObs(true); });
    }
    auto dispatchCommand = [&window](QLocalSocket *socket) {
        if (!socket) return;
        const QByteArray command = socket->readAll().trimmed();
        if (command.isEmpty()) return;
        if (command == "stop") {
            window.setServerEnabled(false);
        } else if (command == "start") {
            window.setServerEnabled(true);
        } else if (command == "usb-obs") {
            window.startUsbObs(false);
        } else if (command == "save-diagnostics") {
            window.saveDiagnostics(false);
        } else if (command == "diagnostics") {
            window.showDiagnosticsWindow();
        } else {
            window.showAndRaise();
        }
        socket->deleteLater();
    };
    QObject::connect(
        &instanceServer, &QLocalServer::newConnection, &app,
        [&instanceServer, dispatchCommand]() {
            while (QLocalSocket *socket = instanceServer.nextPendingConnection()) {
                auto handle = [dispatchCommand, socket]() {
                    if (socket->bytesAvailable() <= 0) {
                        return;
                    }
                    dispatchCommand(socket);
                };
                QObject::connect(socket, &QLocalSocket::readyRead, socket, handle);
                // Data may already be buffered before the readyRead connection
                // is made on Windows; handle it now instead of waiting for a
                // signal that will never arrive.
                handle();
            }
        });

    const int rc = app.exec();
    applog::shutdown();
#ifdef _WIN32
    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
#endif
    return rc;
}
