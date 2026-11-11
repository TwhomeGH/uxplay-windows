#include "mainwindow.h"
#include "airplayworker.h"
#include "logging.h"
#include "usbobscontroller.h"
#include "cleanoutputwindow.h"
#include "mdns_responder.hpp"
#include <windows.h>
#include <winsvc.h>

#include <QProcess>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QComboBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QTextStream>

struct RenameData {
    DWORD pid;
    QString newTitle;
};

// Callback method that Windows calls for every opened window
BOOL CALLBACK EnumWindowsProcRename(HWND hwnd, LPARAM lParam) {
    RenameData *data = reinterpret_cast<RenameData*>(lParam);
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == data->pid) {
        char windowTitle[512];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle))) {
            QString title = QString::fromLocal8Bit(windowTitle);
            
            if (title.contains("Direct") && title.contains("enderer")) {
                printf("found window to rename, setting new name...\n");
                SetWindowTextW(hwnd, reinterpret_cast<const wchar_t*>(data->newTitle.utf16()));
                return FALSE;
            }
        }
    }
    return TRUE;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_settings = AppSettings::load();
    m_serverEnabled = m_settings.serverEnabled;

    setupTray();
    setupUI();

    // Force creation of the hidden top-level HWND so Windows power events reach us.
    winId();

    m_wakeRestartTimer = new QTimer(this);
    m_wakeRestartTimer->setSingleShot(true);
    connect(m_wakeRestartTimer, &QTimer::timeout,
            this, &MainWindow::restartApplicationAfterWake);

    if (!m_serverEnabled) {
        qInfo() << "AirPlay disabled by server_enabled=false; engine not started";
        updateStatus();
        return;
    }

    if (!ensureBonjourServiceReady()) {
        m_quitting = true;
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }

    startServer();
}

MainWindow::~MainWindow() {
    m_quitting = true;
    if (m_usbObs) {
        m_usbObs->stop();
    }
    stopServer();
}



void MainWindow::setupUI() {
    setWindowTitle("uxplay-windows");
    setWindowIcon(QApplication::windowIcon());
    setFixedSize(420, 520);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel("Initializing...", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: #b00020;");
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);
    

    // Display Name
    auto *nameLayout = new QHBoxLayout();
    auto *nameLabel = new QLabel("AirPlay Name", this);
    m_displayNameEdit = new QLineEdit(this);
    m_displayNameEdit->setText(m_settings.displayName);
    m_displayNameEdit->setPlaceholderText("uxplay-windows");
    auto *applyNameBtn = new QPushButton("Apply", this);
    connect(applyNameBtn, &QPushButton::clicked, this, &MainWindow::applyDisplayName);
    connect(m_displayNameEdit, &QLineEdit::returnPressed, this, &MainWindow::applyDisplayName);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_displayNameEdit, 1);
    nameLayout->addWidget(applyNameBtn);
    layout->addLayout(nameLayout);


    // Bluetooth Discovery Checkbox
    m_bleCheckbox = new QCheckBox("Enable Bluetooth Discovery", this);
    m_bleCheckbox->setChecked(m_settings.bleEnabled);
    connect(m_bleCheckbox, &QCheckBox::toggled, this, &MainWindow::toggleBle);
    layout->addWidget(m_bleCheckbox);

    // Force Fullscreen Checkbox
    m_fullscreenCheckbox = new QCheckBox("Force Fullscreen (must select renderer)", this);
    m_fullscreenCheckbox->setChecked(m_settings.forceFullscreen);
    connect(m_fullscreenCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleForceFullscreen);
    layout->addWidget(m_fullscreenCheckbox);

    // Renderer dropdown
    m_rendererCombo = new QComboBox(this);
    m_rendererCombo->addItem("Video Renderer (Auto)", "auto");
    m_rendererCombo->addItem("D3D11", "d3d11");
    m_rendererCombo->addItem("D3D12", "d3d12");

    {
        QString saved = m_settings.renderer;
        int idx = m_rendererCombo->findData(saved);
        if (idx >= 0) m_rendererCombo->setCurrentIndex(idx);
    }

    connect(m_rendererCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRendererChanged);

    layout->addWidget(m_rendererCombo);

    auto *resolutionLayout = new QHBoxLayout();
    auto *resolutionLabel = new QLabel("Resolution", this);
    m_resolutionCombo = new QComboBox(this);
    m_resolutionCombo->addItem("Auto", "auto");
    m_resolutionCombo->addItem("1920 x 1080", "1080p");
    m_resolutionCombo->addItem("2560 x 1440", "1440p");
    m_resolutionCombo->addItem("3840 x 2160", "4k");
    m_resolutionCombo->addItem("Custom", "custom");
    {
        QString saved = m_settings.resolution.mode;
        int idx = m_resolutionCombo->findData(saved);
        if (idx < 0) idx = m_resolutionCombo->findData("1080p");
        m_resolutionCombo->setCurrentIndex(idx);
    }
    connect(m_resolutionCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onResolutionChanged);
    resolutionLayout->addWidget(resolutionLabel);
    resolutionLayout->addWidget(m_resolutionCombo, 1);
    layout->addLayout(resolutionLayout);

    auto *sizeLayout = new QHBoxLayout();
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 9999);
    m_widthSpin->setSuffix(" w");
    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(1, 9999);
    m_heightSpin->setSuffix(" h");
    m_refreshSpin = new QSpinBox(this);
    m_refreshSpin->setRange(1, 255);
    m_refreshSpin->setSuffix(" Hz");
    m_widthSpin->setValue(m_settings.resolution.width);
    m_heightSpin->setValue(m_settings.resolution.height);
    m_refreshSpin->setValue(m_settings.resolution.refresh);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onResolutionChanged);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onResolutionChanged);
    connect(m_refreshSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onResolutionChanged);
    sizeLayout->addWidget(m_widthSpin);
    sizeLayout->addWidget(m_heightSpin);
    sizeLayout->addWidget(m_refreshSpin);
    layout->addLayout(sizeLayout);
    updateResolutionControls();

    m_settingsBtn = new QPushButton("Edit Settings (Advanced)", this);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettingsFile);
    layout->addWidget(m_settingsBtn);

    m_listargsBtn = new QPushButton("List UxPlay arguments", this);
    connect(m_listargsBtn, &QPushButton::clicked, this, &MainWindow::openListArgsFile);
    layout->addWidget(m_listargsBtn);

    m_usbObsBtn = new QPushButton("USB Clean Output (OBS, experimental)", this);
    connect(m_usbObsBtn, &QPushButton::clicked, this, &MainWindow::toggleUsbObs);
    layout->addWidget(m_usbObsBtn);

    m_autostartBtn = new QPushButton(this);
    connect(m_autostartBtn, &QPushButton::clicked, this, &MainWindow::toggleAutostart);
    layout->addWidget(m_autostartBtn);

    m_licenseBtn = new QPushButton("License Information", this);
    connect(m_licenseBtn, &QPushButton::clicked, this, &MainWindow::showLicense);

    m_openLogBtn = new QPushButton("Open Log", this);
    connect(m_openLogBtn, &QPushButton::clicked, this, &MainWindow::openLogFile);

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(m_licenseBtn);
    bottomLayout->addWidget(m_openLogBtn);
    layout->addLayout(bottomLayout);

    layout->addStretch();
    updateStatus();
}

void MainWindow::openSettingsFile() {
    QString filePath = AppSettings::filePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));

    m_tray->showMessage("Settings",
                        "Edit extra_args in settings.json, then restart to apply.",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::openListArgsFile() {
    QString filePath = QApplication::applicationDirPath() + "/resources/uxplay_arguments_list.txt";
    QFile::setPermissions(filePath, QFile::ReadOwner | QFile::ReadGroup | QFile::ReadOther);
    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
}

void MainWindow::setupTray() {
    m_tray = new QSystemTrayIcon(this);
    QIcon trayIcon;
    QString icoPath = QApplication::applicationDirPath() + "/resources/icon.ico";
    trayIcon = QIcon(icoPath);
    if (trayIcon.isNull()) {
        trayIcon = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
    }
    m_tray->setIcon(trayIcon);
    m_tray->setToolTip("uxplay-windows");

    m_trayMenu = new QMenu(this);
    m_mediaInfoMenu = m_trayMenu->addMenu("Media Info");
    m_clientAction = m_mediaInfoMenu->addAction("Client: -");
    m_connectionsAction = m_mediaInfoMenu->addAction("Connections: 0");
    m_mediaStatusAction = m_mediaInfoMenu->addAction("Status: Not connected");
    m_videoCodecAction = m_mediaInfoMenu->addAction("Video Codec: -");
    m_videoDecoderAction = m_mediaInfoMenu->addAction("Video Decoder: -");
    m_videoRendererAction = m_mediaInfoMenu->addAction("Video Renderer: -");
    m_streamSizeAction = m_mediaInfoMenu->addAction("Stream Size: -");
    m_audioCodecAction = m_mediaInfoMenu->addAction("Audio Codec: -");
    m_audioFormatAction = m_mediaInfoMenu->addAction("Audio Format: -");
    for (QAction *action : {m_clientAction, m_connectionsAction, m_mediaStatusAction,
                            m_videoCodecAction, m_videoDecoderAction,
                            m_videoRendererAction, m_streamSizeAction, m_audioCodecAction,
                            m_audioFormatAction}) {
        action->setEnabled(false);
    }
    m_trayMenu->addSeparator();
    m_toggleServerAction = m_trayMenu->addAction("Stop AirPlay", this, &MainWindow::toggleServer);
    m_usbObsAction = m_trayMenu->addAction("USB Clean Output (experimental)", this,
                                           &MainWindow::toggleUsbObs);
    m_openLogAction = m_trayMenu->addAction("Open Log", this, &MainWindow::openLogFile);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Restart", this, &MainWindow::restartApplication);
    m_trayMenu->addAction("Quit", this, &MainWindow::quit);

    m_tray->setContextMenu(m_trayMenu);
    
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        isVisible() ? hide() : showNormal();
    }
}

void MainWindow::toggleBle(bool checked) {
    if (m_settings.bleEnabled == checked) {
        return;
    }

    m_settings.bleEnabled = checked;
    m_settings.save();

    m_tray->showMessage("uxplay-windows",
                        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::toggleForceFullscreen(bool checked) {
    if (m_settings.forceFullscreen == checked) {
        return;
    }

    m_settings.forceFullscreen = checked;
    m_settings.save();
    m_tray->showMessage("uxplay-windows",
                        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::onRendererChanged(int /*index*/) {
    if (!m_rendererCombo) return;

    const QString mode = m_rendererCombo->currentData().toString();
    if (m_settings.renderer == mode) return;

    m_settings.renderer = mode;
    m_settings.save();
    m_tray->showMessage("uxplay-windows",
                        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::onResolutionChanged() {
    if (!m_resolutionCombo || !m_widthSpin || !m_heightSpin || !m_refreshSpin) return;

    QString mode = m_resolutionCombo->currentData().toString();
    if (mode == "1080p") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(1920);
        m_heightSpin->setValue(1080);
    } else if (mode == "1440p") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(2560);
        m_heightSpin->setValue(1440);
    } else if (mode == "4k") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(3840);
        m_heightSpin->setValue(2160);
    }

    updateResolutionControls();

    ResolutionSettings next = m_settings.resolution;
    next.mode = mode;
    next.width = m_widthSpin->value();
    next.height = m_heightSpin->value();
    next.refresh = m_refreshSpin->value();
    if (next.mode == m_settings.resolution.mode
        && next.width == m_settings.resolution.width
        && next.height == m_settings.resolution.height
        && next.refresh == m_settings.resolution.refresh) {
        return;
    }

    m_settings.resolution = next;
    m_settings.save();
    m_tray->showMessage("uxplay-windows",
                        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::updateResolutionControls() {
    if (!m_resolutionCombo || !m_widthSpin || !m_heightSpin || !m_refreshSpin) return;

    QString mode = m_resolutionCombo->currentData().toString();
    if (mode == "1080p" || mode == "1440p" || mode == "4k") {
        int width = 1920;
        int height = 1080;
        if (mode == "1440p") {
            width = 2560;
            height = 1440;
        } else if (mode == "4k") {
            width = 3840;
            height = 2160;
        }
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(width);
        m_heightSpin->setValue(height);
    }

    bool isAuto = mode == "auto";
    bool isCustom = mode == "custom";
    m_widthSpin->setEnabled(isCustom);
    m_heightSpin->setEnabled(isCustom);
    m_refreshSpin->setEnabled(!isAuto);
}

void MainWindow::resetMediaInfo(const QString &status) {
    m_streaming = false;
    if (m_mediaStatusAction) m_mediaStatusAction->setText("Status: " + status);
    if (m_videoCodecAction) m_videoCodecAction->setText("Video Codec: -");
    if (m_videoDecoderAction) m_videoDecoderAction->setText("Video Decoder: -");
    if (m_videoRendererAction) m_videoRendererAction->setText("Video Renderer: -");
    if (m_streamSizeAction) m_streamSizeAction->setText("Stream Size: -");
    if (m_audioCodecAction) m_audioCodecAction->setText("Audio Codec: -");
    if (m_audioFormatAction) m_audioFormatAction->setText("Audio Format: -");
}

void MainWindow::updateMediaInfo(const QString &key, const QString &value) {
    if (m_mediaStatusAction && key != "Video Decoder" && key != "Video Renderer") {
        m_mediaStatusAction->setText("Status: Streaming");
    }

    if (key == "Video Codec" && m_videoCodecAction) {
        m_videoCodecAction->setText("Video Codec: " + value);
    } else if (key == "Video Decoder" && m_videoDecoderAction) {
        m_videoDecoderAction->setText("Video Decoder: " + value);
    } else if (key == "Video Renderer" && m_videoRendererAction) {
        m_videoRendererAction->setText("Video Renderer: " + value);
    } else if (key == "Stream Size" && m_streamSizeAction) {
        m_streamSizeAction->setText("Stream Size: " + value);
    } else if (key == "Audio Codec" && m_audioCodecAction) {
        m_audioCodecAction->setText("Audio Codec: " + value);
    } else if (key == "Audio Format" && m_audioFormatAction) {
        m_audioFormatAction->setText("Audio Format: " + value);
    }

    m_streaming = true;
    if (m_errorLabel) m_errorLabel->setVisible(false);
    m_appState = AppState::Streaming;
    updateStatus();
}

void MainWindow::clearClientInfo() {
    m_clientName.clear();
    m_clientModel.clear();
    m_clientDeviceId.clear();
    m_connectionCount = 0;
    m_streaming = false;
    if (m_clientAction) m_clientAction->setText("Client: -");
    if (m_connectionsAction) m_connectionsAction->setText("Connections: 0");
}

void MainWindow::onClientConnected(const QString &name, const QString &model,
                                   const QString &deviceid) {
    const bool isNew = (deviceid != m_clientDeviceId) || m_clientName.isEmpty();
    m_clientName = name;
    m_clientModel = model;
    m_clientDeviceId = deviceid;

    if (m_clientAction) {
        QString text = "Client: " + (name.isEmpty() ? QStringLiteral("unknown") : name);
        if (!model.isEmpty()) {
            text += " (" + model + ")";
        }
        m_clientAction->setText(text);
    }
    if (!m_streaming && m_mediaStatusAction) {
        m_mediaStatusAction->setText("Status: Connected");
    }

    if (isNew) {
        qInfo() << "Client connected:" << name << "model:" << model
                << "deviceid:" << deviceid;
        if (m_tray && !m_quitting) {
            m_tray->showMessage(
                "Device connected",
                name.isEmpty() ? QStringLiteral("A device connected") : name,
                QSystemTrayIcon::Information, 3000);
        }
    }

    if (m_errorLabel) m_errorLabel->setVisible(false);
    m_appState = AppState::Streaming;
    updateStatus();
}

void MainWindow::onClientDisconnected() {
    if (!m_clientName.isEmpty()) {
        qInfo() << "Client disconnected:" << m_clientName;
        if (m_tray && !m_quitting) {
            m_tray->showMessage("Device disconnected", m_clientName,
                                QSystemTrayIcon::Information, 3000);
        }
    }

    clearClientInfo();
    resetMediaInfo("Not connected");
    if (m_running && m_serverEnabled) {
        m_appState = AppState::Waiting;
    }
    updateStatus();
}

void MainWindow::onClientRejected(const QString &name, const QString &model,
                                  const QString &deviceid) {
    qWarning() << "Client rejected:" << name << "model:" << model
               << "deviceid:" << deviceid;
    if (m_tray && !m_quitting) {
        const QString who = name.isEmpty() ? QStringLiteral("A device") : name;
        m_tray->showMessage("Connection rejected", who,
                            QSystemTrayIcon::Warning, 4000);
    }
}

void MainWindow::onConnectionCountChanged(int count) {
    m_connectionCount = count;
    if (m_connectionsAction) {
        m_connectionsAction->setText(QString("Connections: %1").arg(count));
    }
}

void MainWindow::applyDisplayName() {
    if (!m_displayNameEdit) return;

    const QString displayName = m_displayNameEdit->text().trimmed();
    if (displayName.isEmpty()) {
        QMessageBox::warning(this, "AirPlay Name",
                             "Enter a name for this AirPlay receiver.");
        m_displayNameEdit->setText(m_settings.displayName);
        return;
    }

    if (m_settings.displayName == displayName) {
        return;
    }

    m_settings.displayName = displayName;
    m_settings.save();
    m_displayNameEdit->setText(displayName);

    m_tray->showMessage("uxplay-windows",
                        "Restarting uxplay-windows with the new AirPlay name.",
                        QSystemTrayIcon::Information, 3000);

    restartApplication();
}


void MainWindow::startServer() {
    if (!m_serverEnabled) return;
    if (m_worker && m_worker->isRunning()) return;

    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);

    const QString bleFilePath = QDir::toNativeSeparators(appData + "/uxplay_status.ble");
    const bool engineDebug =
        QCoreApplication::arguments().contains(QStringLiteral("--engine-debug"));
    QStringList args = m_settings.engineArguments(bleFilePath, engineDebug);

    if (m_settings.bleEnabled) {
        startBluetoothBeacon(bleFilePath);
    } else {
        stopBluetoothBeacon();
    }

    qInfo() << "Starting UxPlay engine with arguments:" << args;
    clearClientInfo();
    resetMediaInfo("Waiting for stream");
    if (m_lastEngineError.isEmpty() && m_errorLabel) {
        m_errorLabel->setVisible(false);
    }
    if (m_lastEngineError.isEmpty()) {
        m_appState = AppState::Starting;
    }
    updateStatus();

    m_worker = new AirPlayWorker(this);
    m_worker->setArgs(args);

    connect(m_worker, &AirPlayWorker::started, this, &MainWindow::onAirplayStarted);
    connect(m_worker, &AirPlayWorker::stopped, this, &MainWindow::onAirplayStopped);
    connect(m_worker, &AirPlayWorker::errorOccurred, this, &MainWindow::onAirplayError);
    connect(m_worker, &AirPlayWorker::mediaInfoChanged,
            this, &MainWindow::updateMediaInfo);
    connect(m_worker, &AirPlayWorker::clientConnected,
            this, &MainWindow::onClientConnected);
    connect(m_worker, &AirPlayWorker::clientDisconnected,
            this, &MainWindow::onClientDisconnected);
    connect(m_worker, &AirPlayWorker::clientRejected,
            this, &MainWindow::onClientRejected);
    connect(m_worker, &AirPlayWorker::connectionCountChanged,
            this, &MainWindow::onConnectionCountChanged);

    connect(m_worker, &AirPlayWorker::finished, m_worker, &QObject::deleteLater);

    m_worker->start();
}

void MainWindow::stopServer() {
    qInfo() << "Stopping UxPlay engine";
    stopBluetoothBeacon();
    if (m_videoTitleTimer) m_videoTitleTimer->stop();
    if (m_worker) {
        AirPlayWorker *worker = m_worker;
        m_worker = nullptr;
        worker->disconnect();
        worker->requestInterruption();
        worker->stopAirplay();
        if (!worker->wait(5000)) {
            qWarning() << "Engine did not stop within 5s; terminating thread";
            worker->terminate();
            worker->wait(1000);
        }
        if (!worker->isRunning()) {
            worker->deleteLater();
        } else {
            qWarning() << "Engine thread is still running; leaving it to process exit";
        }
    }
    m_running = false;
    clearClientInfo();
    resetMediaInfo("Not connected");
    updateStatus();
}


void MainWindow::onAirplayStarted() {
    m_running = true;
    m_appState = AppState::Waiting;
    m_restartAttempt = 0;
    m_lastEngineError.clear();
    if (m_errorLabel) m_errorLabel->setVisible(false);
    qInfo() << "UxPlay engine ready";
    updateStatus();

    if (!m_videoTitleTimer) {
        m_videoTitleTimer = new QTimer(this);
        connect(m_videoTitleTimer, &QTimer::timeout, this, [this]() {
            if (!m_running) {
                m_videoTitleTimer->stop();
                return;
            }

            RenameData info;
            info.pid = GetCurrentProcessId();
            info.newTitle = "AirPlay Video Stream (ALT+ENTER for Fullscreen)";

            EnumWindows(EnumWindowsProcRename, reinterpret_cast<LPARAM>(&info));
        });
    }
    m_videoTitleTimer->start(5000);
}

void MainWindow::onAirplayStopped() {
    m_running = false;
    if (m_videoTitleTimer) m_videoTitleTimer->stop();
    clearClientInfo();
    resetMediaInfo("Not connected");
    qInfo() << "UxPlay engine stopped";

    if (!m_quitting && m_serverEnabled) {
        static const int delays[] = {1000, 2000, 5000, 10000};
        const int delay = delays[qMin(m_restartAttempt, 3)];
        m_appState = AppState::Error;
        qInfo() << "Engine stopped; restarting in" << delay
                << "ms (attempt" << (m_restartAttempt + 1) << ")";
        ++m_restartAttempt;
        QTimer::singleShot(delay, this, &MainWindow::startServer);
    } else if (!m_serverEnabled) {
        m_appState = AppState::Disabled;
    }
    updateStatus();
}

void MainWindow::onAirplayError(const QString &message) {
    qCritical() << "UxPlay engine error:" << message;
    m_lastEngineError = message;
    m_appState = AppState::Error;
    if (m_errorLabel) {
        m_errorLabel->setText(message);
        m_errorLabel->setVisible(true);
    }
    if (m_tray) {
        m_tray->showMessage("uxplay-windows", message,
                            QSystemTrayIcon::Warning, 5000);
    }
    updateStatus();
}

void MainWindow::toggleServer() {
    const bool enable = !m_serverEnabled;
    if (m_tray) {
        m_tray->showMessage(
            "uxplay-windows",
            enable ? "Starting AirPlay" : "Stopping AirPlay",
            QSystemTrayIcon::Information, 2000);
    }
    setServerEnabled(enable);
}

void MainWindow::ensureUsbObsController() {
    if (m_usbObs) {
        return;
    }

    m_usbObs = new UsbObsController(this);
    connect(m_usbObs, &UsbObsController::started, this, [this]() {
        if (m_usbObsAction) {
            m_usbObsAction->setText("Stop USB Clean Output");
        }
        if (m_usbObsBtn) {
            m_usbObsBtn->setText("Stop USB Clean Output");
        }
        if (m_tray) {
            m_tray->showMessage(
                "uxplay-windows",
                "USB Clean Output started. Add it in OBS with Window Capture.",
                QSystemTrayIcon::Information, 4000);
        }
        qInfo() << "USB Clean Output started";
    });
    connect(m_usbObs, &UsbObsController::stopped, this, [this]() {
        if (m_usbObsAction) {
            m_usbObsAction->setText("USB Clean Output (experimental)");
        }
        if (m_usbObsBtn) {
            m_usbObsBtn->setText("USB Clean Output (OBS, experimental)");
        }
        qInfo() << "USB Clean Output stopped";
    });
    connect(m_usbObs, &UsbObsController::errorOccurred, this,
            [this](const QString &message) {
                qCritical() << "USB Clean Output error:" << message;
                if (m_tray) {
                    m_tray->showMessage("uxplay-windows", message,
                                        QSystemTrayIcon::Warning, 6000);
                }
            });
}

void MainWindow::toggleUsbObs() {
    ensureUsbObsController();
    if (!m_usbObs) {
        return;
    }
    if (m_usbObs->isRunning()) {
        m_usbObs->stop();
    } else {
        m_usbObs->start();
    }
}

void MainWindow::startUsbObs(bool minimize) {
    ensureUsbObsController();
    if (!m_usbObs) {
        return;
    }
    if (m_usbObs->isRunning()) {
        if (m_usbObs->outputWindow()) {
            m_usbObs->outputWindow()->show();
            m_usbObs->outputWindow()->raise();
        }
    } else {
        m_usbObs->start();
    }
    if (minimize) {
        hide();
    }
}

void MainWindow::setServerEnabled(bool enabled) {
    if (m_serverEnabled == enabled) return;

    m_serverEnabled = enabled;
    m_settings.serverEnabled = enabled;
    m_settings.save();
    m_restartAttempt = 0;

    if (enabled) {
        if (!ensureBonjourServiceReady()) {
            m_serverEnabled = false;
            m_settings.serverEnabled = false;
            m_settings.save();
            qWarning() << "Cannot start AirPlay: Bonjour Service is not ready";
            updateStatus();
            return;
        }
        qInfo() << "Starting AirPlay (hot)";
        startServer();
    } else {
        qInfo() << "Stopping AirPlay (hot)";
        stopServer();
    }
    updateStatus();
}

void MainWindow::openLogFile() {
    applog::openLogFile();
}

void MainWindow::updateTrayIcon() {
    if (!m_tray) return;

    QIcon base = QIcon(QApplication::applicationDirPath() + "/resources/icon.ico");
    if (base.isNull()) {
        base = QApplication::style()->standardIcon(QStyle::SP_MediaPlay);
    }

    QPixmap pixmap = base.pixmap(32, 32);
    if (pixmap.isNull()) {
        pixmap = QPixmap(32, 32);
        pixmap.fill(Qt::transparent);
    }

    QColor color;
    switch (m_appState) {
    case AppState::Disabled:
        color = QColor(128, 128, 128);
        break;
    case AppState::Starting:
        color = QColor(230, 160, 0);
        break;
    case AppState::Waiting:
        color = QColor(0, 120, 215);
        break;
    case AppState::Streaming:
        color = QColor(0, 160, 80);
        break;
    case AppState::Error:
        color = QColor(200, 40, 40);
        break;
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(color);
    painter.drawEllipse(QRect(20, 20, 11, 11));
    painter.end();

    m_tray->setIcon(QIcon(pixmap));
}

void MainWindow::toggleAutostart() {
    bool enable = !isAutostartEnabled();
    setAutostart(enable);
    if (m_autostartAction)
        m_autostartAction->setChecked(enable);
    updateStatus();
}

bool MainWindow::isAutostartEnabled() const {
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return reg.contains("uxplay-windows");
}

void MainWindow::setAutostart(bool enabled) {
    QSettings reg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        QString path = QDir::toNativeSeparators(QApplication::applicationFilePath());
        reg.setValue("uxplay-windows", "\"" + path + "\"");
    } else {
        reg.remove("uxplay-windows");
    }
}

void MainWindow::startBluetoothBeacon(const QString &path) {
    if (m_beacon && m_beacon->state() != QProcess::NotRunning)
        return;

    QString exe = QApplication::applicationDirPath() + "/uxplay-bluetooth-beacon.exe";
    if (!QFile::exists(exe)) {
        qDebug() << "uxplay-bluetooth-beacon.exe not found";
        return;
    }

    m_beacon = new QProcess(this);
    m_beacon->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_beacon, &QProcess::readyRead, this, [this]() {
        // Forward beacon logs to our debug console
        qDebug() << "[beacon output]" << m_beacon->readAll().trimmed();
    });

    // Pass the explicit path to the beacon file
    m_beacon->start(exe, {"--path", path});
    qDebug() << "Beacon process started watching:" << path;
}

void MainWindow::stopBluetoothBeacon() {
    if (!m_beacon) return;
    
    qDebug() << "Stopping beacon process";
    if (m_beacon->state() != QProcess::NotRunning) {
        m_beacon->terminate();
        if (!m_beacon->waitForFinished(500)) {
            qDebug() << "Beacon didn't terminate, killing";
            m_beacon->kill();
            m_beacon->waitForFinished(100);
        }
    }
    
    delete m_beacon;
    m_beacon = nullptr;
    qDebug() << "Beacon stopped and cleaned up";
}

void MainWindow::showLicense() {
    QString path = QApplication::applicationDirPath() + "/LICENSE.rtf";
    if (QFile::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else qDebug() << "License file not found:" << path;
}

void MainWindow::quit() {
    m_quitting = true;
    if (m_usbObs) {
        m_usbObs->stop();
    }
    stopServer();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_quitting) {
        hide();
        event->ignore();
    } else {
        event->accept();
    }
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_POWERBROADCAST) {
        switch (msg->wParam) {
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
        case PBT_APMRESUMECRITICAL:
            scheduleWakeRestart();
            break;
        default:
            break;
        }
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::scheduleWakeRestart() {
    if (m_quitting || !m_wakeRestartTimer) {
        return;
    }

    qDebug() << "Windows resumed from sleep; scheduling application restart to refresh discovery";
    m_wakeRestartTimer->start(5000);
}

void MainWindow::restartApplicationAfterWake() {
    if (m_quitting) {
        return;
    }

    if (m_tray) {
        m_tray->showMessage("uxplay-windows",
                            "Windows woke from sleep. Restarting to refresh AirPlay discovery.",
                            QSystemTrayIcon::Information, 3000);
    }

    restartApplication();
}


void MainWindow::updateStatus() {
    QString status;
    if (!m_serverEnabled) {
        m_appState = AppState::Disabled;
        status = "AirPlay stopped";
    } else if (m_appState == AppState::Error) {
        status = "Engine failing - see log";
    } else if (!m_running) {
        m_appState = AppState::Starting;
        status = "UxPlay server starting...";
    } else if (!m_clientName.isEmpty()) {
        m_appState = AppState::Streaming;
        status = m_streaming ? ("Streaming: " + m_clientName)
                             : ("Connected: " + m_clientName);
    } else {
        m_appState = AppState::Waiting;
        status = "UxPlay server running - waiting for connection";
    }

    if (m_statusLabel) m_statusLabel->setText(status);
    if (m_autostartBtn) {
        m_autostartBtn->setText(isAutostartEnabled()
                                    ? "Open uxplay-windows on login: ON "
                                    : "Open uxplay-windows on login: OFF");
    }
    if (m_toggleServerAction) {
        m_toggleServerAction->setText(m_serverEnabled ? "Stop AirPlay"
                                                      : "Start AirPlay");
    }
    if (m_tray) {
        QString tip = "uxplay-windows - " + status;
        if (!m_clientModel.isEmpty()) {
            tip += " (" + m_clientModel + ")";
        }
        m_tray->setToolTip(tip);
    }
    updateTrayIcon();
}

MainWindow::BonjourState MainWindow::queryBonjourState() const {
    const std::wstring serviceName = L"Bonjour Service";
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        qWarning() << "OpenSCManagerW failed:" << GetLastError();
        return BonjourState::Unknown;
    }

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (!hSvc) {
        const DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            return BonjourState::Missing;
        }
        qWarning() << "OpenServiceW(Bonjour Service) failed:" << err;
        return BonjourState::Unknown;
    }

    SERVICE_STATUS status{};
    const BOOL ok = QueryServiceStatus(hSvc, &status);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    if (!ok) {
        qWarning() << "QueryServiceStatus(Bonjour Service) failed:" << err;
        return BonjourState::Unknown;
    }
    return status.dwCurrentState == SERVICE_RUNNING ? BonjourState::Running
                                                    : BonjourState::Stopped;
}

bool MainWindow::startBonjourService() {
    const std::wstring serviceName = L"Bonjour Service";
    SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        qWarning() << "OpenSCManagerW failed:" << GetLastError();
        return false;
    }

    SC_HANDLE hSvc = OpenServiceW(hSCM, serviceName.c_str(),
                                  SERVICE_START | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        qWarning() << "OpenServiceW(Bonjour Service, START) failed:" << GetLastError();
        CloseServiceHandle(hSCM);
        return false;
    }

    bool started = false;
    if (StartServiceW(hSvc, 0, nullptr)) {
        started = true;
    } else {
        const DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            started = true;
        } else {
            qWarning() << "StartServiceW(Bonjour Service) failed:" << err;
        }
    }

    if (started) {
        for (int i = 0; i < 50; ++i) {
            SERVICE_STATUS status{};
            if (QueryServiceStatus(hSvc, &status)
                && status.dwCurrentState == SERVICE_RUNNING) {
                started = true;
                break;
            }
            Sleep(100);
        }
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);
    return started;
}

bool MainWindow::ensureBonjourServiceReady() {
    BonjourState state = queryBonjourState();

    if (state == BonjourState::Running) {
        qInfo() << "Bonjour Service is running";
        return true;
    }

    if (state == BonjourState::Stopped) {
        qInfo() << "Bonjour Service is stopped; attempting to start it";
        if (startBonjourService() && queryBonjourState() == BonjourState::Running) {
            qInfo() << "Bonjour Service started";
            return true;
        }
        QMessageBox::critical(
            this, "Bonjour Service Not Running",
            "Bonjour Service is installed but could not be started.\n\n"
            "Open Services (services.msc), start 'Bonjour Service', "
            "then relaunch uxplay-windows.");
        return false;
    }

    if (state == BonjourState::Missing) {
        const int choice = QMessageBox::question(
            this, "Bonjour Service Required",
            "Bonjour Service is required for discovery (mDNS). It is not "
            "installed.\n\nDo you want to install it now?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (choice != QMessageBox::Yes) {
            QMessageBox::critical(
                this, "Bonjour Service Missing",
                "Bonjour Service is required. The application will now exit.");
            return false;
        }

        QMessageBox::information(
            this, "Installing Bonjour Service",
            "Starting installation of 'Bonjour Service'. Follow any UAC prompt.");

        const int rc = mdns::MdnsResponder::install();
        if (rc != 0) {
            QMessageBox::critical(
                this, "Installation Failed",
                "Failed to install 'Bonjour Service'.\n\n"
                "The application will now exit.");
            return false;
        }

        if (queryBonjourState() == BonjourState::Running) {
            qInfo() << "Bonjour Service installed and running";
            return true;
        }
        if (startBonjourService() && queryBonjourState() == BonjourState::Running) {
            qInfo() << "Bonjour Service installed and started";
            return true;
        }

        QMessageBox::critical(
            this, "Installation Failed",
            "Bonjour Service was installed but could not be started.\n\n"
            "Open Services (services.msc), start 'Bonjour Service', "
            "then relaunch uxplay-windows.");
        return false;
    }

    // Access denied or another service-manager error. Discovery may still work,
    // so continue and leave the details in the log instead of exiting.
    qWarning() << "Bonjour Service state is unknown; continuing in best-effort mode";
    QMessageBox::warning(
        this, "Bonjour Service Status Unknown",
        "Could not query Bonjour Service status (access denied or service "
        "manager unavailable).\n\nDiscovery may still work. Check the log for "
        "details.");
    return true;
}

void MainWindow::restartApplication() {
    qInfo() << "Restarting application";
    m_quitting = true;
    stopServer();

    QString exePath = QApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) args.removeFirst(); // remove exe path
    if (!args.contains("--restart")) args << "--restart";

    qInfo() << "Launching new instance with arguments:" << args;
    if (!QProcess::startDetached(exePath, args)) {
        qCritical() << "Failed to launch new instance";
        m_quitting = false;
        QMessageBox::critical(
            this, "Restart Failed",
            "Could not restart uxplay-windows.\n\nPlease close and open it manually.");
        if (m_serverEnabled) startServer();
        updateStatus();
        return;
    }

    // close GUI of current process after a short delay
    QTimer::singleShot(200, qApp, &QCoreApplication::quit);
}
