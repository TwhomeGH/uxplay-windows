#include "mainwindow.h"
#include "airplayworker.h"
#include "logging.h"
#include "usbobscontroller.h"
#include "usbdeviceconnector.h"
#include "cleanoutputwindow.h"
#include "diagnostics.h"
#include "mdns_responder.hpp"
#include "uxplay_api.h"
#include <windows.h>
#include <winsvc.h>

#include <QProcess>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScreen>

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
#include <QGroupBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QTextStream>

struct VideoWindowData {
    DWORD pid;
    QString newTitle;
    bool applyGeometry = false;
    RECT geometry = {};
    HWND found = nullptr;
};

// Callback method that Windows calls for every opened window
BOOL CALLBACK EnumWindowsProcVideo(HWND hwnd, LPARAM lParam) {
    VideoWindowData *data = reinterpret_cast<VideoWindowData*>(lParam);
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);

    if (windowPid == data->pid) {
        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }

        char windowTitle[512];
        if (GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle))) {
            const QString title = QString::fromLocal8Bit(windowTitle);
            const bool renderer =
                title.contains("Direct") && title.contains("enderer");
            const bool ours = title.startsWith("AirPlay Video Stream");

            RECT windowRect = {};
            GetWindowRect(hwnd, &windowRect);
            const bool usable =
                windowRect.right - windowRect.left >= 100
                && windowRect.bottom - windowRect.top >= 100;

            if (ours && usable) {
                data->found = hwnd;
                if (data->applyGeometry) {
                    SetWindowPos(hwnd, nullptr, data->geometry.left,
                                 data->geometry.top,
                                 data->geometry.right - data->geometry.left,
                                 data->geometry.bottom - data->geometry.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                SetWindowTextW(
                    hwnd, reinterpret_cast<const wchar_t*>(data->newTitle.utf16()));
                return FALSE;
            }

            if (renderer && usable && !data->found) {
                data->found = hwnd;
                if (data->applyGeometry) {
                    SetWindowPos(hwnd, nullptr, data->geometry.left,
                                 data->geometry.top,
                                 data->geometry.right - data->geometry.left,
                                 data->geometry.bottom - data->geometry.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                }
                SetWindowTextW(
                    hwnd, reinterpret_cast<const wchar_t*>(data->newTitle.utf16()));
            }
        }
    }
    return TRUE;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_settings = AppSettings::load();
    m_serverEnabled = m_settings.serverEnabled;

    m_diagnostics = new DiagnosticsRecorder(this);
    m_diagnostics->setSettings(m_settings);
    m_diagnostics->setEnabled(m_settings.diagnosticsEnabled);
    m_diagnostics->event(
        QStringLiteral("app"), QStringLiteral("started"),
        QJsonObject{{QStringLiteral("args"),
                     QJsonArray::fromStringList(
                         QCoreApplication::arguments())}});

    setupTray();
    setupUI();

    // obs.mode is a preference, not an instruction to capture at launch.
    // USB is started only by an explicit Connect, mode selection or --usb-obs.

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
    if (m_usbConnector) {
        m_usbConnector->cancel();
    }
    if (m_usbObs) {
        m_usbObs->stop();
    }
    stopServer();
}



void MainWindow::setupUI() {
    setWindowTitle("uxplay-windows");
    setWindowIcon(QApplication::windowIcon());
    setFixedSize(420, 680);

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

    auto *codecLayout = new QHBoxLayout();
    auto *codecLabel = new QLabel("Video Codec", this);
    m_videoCodecCombo = new QComboBox(this);
    m_videoCodecCombo->addItem("HEVC / H.265 (needs >1080p)", "hevc");
    m_videoCodecCombo->addItem("H.264 / AVC", "h264");
    m_videoCodecCombo->setToolTip(
        "HEVC requires a requested height above 1080p (for example 4K). "
        "At 1080p the client uses H.264 even when HEVC is enabled.");
    {
        const int saved = m_videoCodecCombo->findData(m_settings.videoCodec);
        m_videoCodecCombo->setCurrentIndex(saved >= 0 ? saved : 0);
    }
    connect(m_videoCodecCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onVideoCodecChanged);
    codecLayout->addWidget(codecLabel);
    codecLayout->addWidget(m_videoCodecCombo, 1);
    layout->addLayout(codecLayout);

    m_videoCodecHint = new QLabel(this);
    m_videoCodecHint->setWordWrap(true);
    m_videoCodecHint->setStyleSheet("color:#666;");
    layout->addWidget(m_videoCodecHint);

    auto *resolutionLayout = new QHBoxLayout();
    auto *resolutionLabel = new QLabel("Resolution", this);
    m_resolutionCombo = new QComboBox(this);
    m_resolutionCombo->addItem("Native (device)", "native");
    m_resolutionCombo->addItem("1280 x 720", "720p");
    m_resolutionCombo->addItem("1920 x 1080", "1080p");
    m_resolutionCombo->addItem("2560 x 1440", "1440p");
    m_resolutionCombo->addItem("3840 x 2160", "4k");
    m_resolutionCombo->addItem("Custom", "custom");
    m_resolutionCombo->setToolTip(
        "Native uses the configured width/height below; set Custom first "
        "to change them. It is a request, not an automatic device query.");
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

    m_outputLabel = new QLabel(this);
    m_outputLabel->setStyleSheet("color:#666;");
    layout->addWidget(m_outputLabel);
    updateOutputLabel();
    updateResolutionControls();

    auto *obsGroup = new QGroupBox("OBS output (USB capture)", this);
    auto *obsLayout = new QVBoxLayout(obsGroup);
    m_obsModeCombo = new QComboBox(obsGroup);
    m_obsModeCombo->addItem("Off", "off");
    m_obsModeCombo->addItem("Clean window (Window Capture)", "window");
    m_obsModeCombo->addItem("Virtual camera (not implemented)", "camera");
    if (auto *model = qobject_cast<QStandardItemModel *>(m_obsModeCombo->model())) {
        if (QStandardItem *cameraItem = model->item(2)) {
            cameraItem->setEnabled(false);
        }
    }
    {
        const int saved = m_obsModeCombo->findData(m_settings.obsMode);
        m_obsModeCombo->setCurrentIndex(saved >= 0 ? saved : 0);
    }
    connect(m_obsModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onObsModeChanged);
    m_usbStatusLabel = new QLabel("USB: not checked", obsGroup);
    if (m_settings.obsMode == QStringLiteral("window")) {
        m_usbStatusLabel->setText("USB: not started (press Connect)");
    }
    m_usbStatusLabel->setWordWrap(true);
    m_usbConnectBtn = new QPushButton("Connect USB device", obsGroup);
    connect(m_usbConnectBtn, &QPushButton::clicked, this,
            &MainWindow::onConnectUsbClicked);
    m_obsHint = new QLabel(
        "Clean window: press Connect USB device, then select the window in OBS.",
        obsGroup);
    m_obsHint->setWordWrap(true);
    obsLayout->addWidget(m_obsModeCombo);
    obsLayout->addWidget(m_usbStatusLabel);
    obsLayout->addWidget(m_usbConnectBtn);
    obsLayout->addWidget(m_obsHint);
    layout->addWidget(obsGroup);

    m_settingsBtn = new QPushButton("Edit Settings (Advanced)", this);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettingsFile);
    layout->addWidget(m_settingsBtn);

    m_listargsBtn = new QPushButton("List UxPlay arguments", this);
    connect(m_listargsBtn, &QPushButton::clicked, this, &MainWindow::openListArgsFile);
    layout->addWidget(m_listargsBtn);

    m_autostartBtn = new QPushButton(this);
    connect(m_autostartBtn, &QPushButton::clicked, this, &MainWindow::toggleAutostart);
    layout->addWidget(m_autostartBtn);

    m_licenseBtn = new QPushButton("License Information", this);
    connect(m_licenseBtn, &QPushButton::clicked, this, &MainWindow::showLicense);

    m_openLogBtn = new QToolButton(this);
    m_openLogBtn->setText("Open Log");
    m_openLogBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_openLogBtn->setPopupMode(QToolButton::InstantPopup);
    m_openLogBtn->setToolTip(
        "Open the log, choose a viewer, or open log/diagnostics folders.");
    auto *logMenu = new QMenu(this);
    populateLogMenu(logMenu);
    m_openLogBtn->setMenu(logMenu);

    auto *diagnosticsLayout = new QHBoxLayout();
    m_diagnosticsCheckbox = new QCheckBox("Keep diagnostics", this);
    m_diagnosticsCheckbox->setChecked(m_settings.diagnosticsEnabled);
    m_diagnosticsCheckbox->setToolTip(
        "Keep the last 30 minutes of events and metrics in memory for export.");
    connect(m_diagnosticsCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleDiagnostics);
    m_diagnosticsBtn = new QPushButton("Save Diagnostics", this);
    connect(m_diagnosticsBtn, &QPushButton::clicked, this,
            [this]() { saveDiagnostics(true); });
    diagnosticsLayout->addWidget(m_diagnosticsCheckbox, 1);
    diagnosticsLayout->addWidget(m_diagnosticsBtn);
    layout->addLayout(diagnosticsLayout);

    m_hudCheckbox = new QCheckBox("Media Info HUD", this);
    m_hudCheckbox->setChecked(m_settings.hudEnabled);
    m_hudCheckbox->setToolTip(
        "Draw the media information in the AirPlay video frame.");
    connect(m_hudCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleHud);
    layout->addWidget(m_hudCheckbox);

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(m_licenseBtn);
    bottomLayout->addWidget(m_openLogBtn);
    layout->addLayout(bottomLayout);

    layout->addStretch();
    updateVideoCodecHint();
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
    QMenu *logsMenu = m_trayMenu->addMenu("Logs & Diagnostics");
    populateLogMenu(logsMenu);
    m_diagnosticsRecordAction =
        m_trayMenu->addAction("Keep recent diagnostics");
    m_diagnosticsRecordAction->setCheckable(true);
    m_diagnosticsRecordAction->setChecked(m_settings.diagnosticsEnabled);
    connect(m_diagnosticsRecordAction, &QAction::toggled, this,
            &MainWindow::toggleDiagnostics);
    m_saveDiagnosticsAction = m_trayMenu->addAction("Save Diagnostics");
    connect(m_saveDiagnosticsAction, &QAction::triggered, this,
            [this]() { saveDiagnostics(true); });
    m_hudAction = m_trayMenu->addAction("Media Info HUD");
    m_hudAction->setCheckable(true);
    m_hudAction->setChecked(m_settings.hudEnabled);
    connect(m_hudAction, &QAction::toggled, this, &MainWindow::toggleHud);
    QMenu *hudPosition = m_trayMenu->addMenu("HUD position");
    m_hudTopLeftAction = hudPosition->addAction("Top left");
    m_hudTopLeftAction->setCheckable(true);
    m_hudTopRightAction = hudPosition->addAction("Top right");
    m_hudTopRightAction->setCheckable(true);
    connect(m_hudTopLeftAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("top-left"));
    });
    connect(m_hudTopRightAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("top-right"));
    });
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Rebuild AirPlay stream", this,
                         &MainWindow::rebuildAirplayStream);
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

void MainWindow::onVideoCodecChanged(int /*index*/) {
    if (!m_videoCodecCombo) return;

    const QString codec = m_videoCodecCombo->currentData().toString();
    if (m_settings.videoCodec == codec) return;

    m_settings.videoCodec = codec;
    m_settings.save();
    if (m_diagnostics) {
        m_diagnostics->setSettings(m_settings);
        m_diagnostics->event(
            QStringLiteral("settings"), QStringLiteral("video codec changed"),
            QJsonObject{{QStringLiteral("codec"), codec}});
    }
    updateVideoCodecHint();

    const bool resAllowsHevc =
        m_settings.resolution.mode == QStringLiteral("auto")
        || m_settings.resolution.height > 1080;
    if (codec == QStringLiteral("hevc") && !resAllowsHevc) {
        m_tray->showMessage(
            "uxplay-windows",
            "HEVC needs height above 1080p; this resolution will use H.264.",
            QSystemTrayIcon::Information, 5000);
        return;
    }

    m_tray->showMessage("uxplay-windows",
                        "Video codec preference saved. Restart to apply.",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::updateVideoCodecHint() {
    if (!m_videoCodecHint) {
        return;
    }

    const bool hevc = m_settings.videoCodec == QStringLiteral("hevc");
    const bool resAllowsHevc =
        m_settings.resolution.mode == QStringLiteral("auto")
        || m_settings.resolution.height > 1080;

    if (!hevc) {
        m_videoCodecHint->setText(
            "H.264 only: HEVC is not advertised to the client.");
    } else if (!resAllowsHevc) {
        m_videoCodecHint->setText(
            "HEVC needs height above 1080p; this resolution will use "
            "H.264.");
    } else {
        m_videoCodecHint->setText(
            "HEVC advertised; the client may still choose H.264.");
    }
}

void MainWindow::onResolutionChanged() {
    if (!m_resolutionCombo || !m_widthSpin || !m_heightSpin || !m_refreshSpin) return;

    const QString previousMode = m_settings.resolution.mode;
    const int editedWidth = m_widthSpin->value();
    const int editedHeight = m_heightSpin->value();
    QString mode = m_resolutionCombo->currentData().toString();
    if (mode == "native") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(m_settings.resolution.nativeWidth);
        m_heightSpin->setValue(m_settings.resolution.nativeHeight);
    } else if (mode == "720p") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(1280);
        m_heightSpin->setValue(720);
    } else if (mode == "1080p") {
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
    if (mode == "native") {
        if (previousMode == "custom") {
            next.nativeWidth = editedWidth;
            next.nativeHeight = editedHeight;
        }
        next.width = next.nativeWidth;
        next.height = next.nativeHeight;
    } else {
        next.width = m_widthSpin->value();
        next.height = m_heightSpin->value();
    }
    next.refresh = m_refreshSpin->value();
    if (next.mode == m_settings.resolution.mode
        && next.width == m_settings.resolution.width
        && next.height == m_settings.resolution.height
        && next.refresh == m_settings.resolution.refresh) {
        return;
    }

    m_settings.resolution = next;
    m_settings.save();
    updateVideoCodecHint();
    updateOutputLabel();
    m_tray->showMessage("uxplay-windows",
                        "Please restart the uxplay-windows to apply changes.\n(Right-click the Tray Icon)",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::updateResolutionControls() {
    if (!m_resolutionCombo || !m_widthSpin || !m_heightSpin || !m_refreshSpin) return;

    QString mode = m_resolutionCombo->currentData().toString();
    if (mode == "720p" || mode == "1080p" || mode == "1440p" || mode == "4k") {
        int width = 1920;
        int height = 1080;
        if (mode == "720p") {
            width = 1280;
            height = 720;
        } else if (mode == "1080p") {
            width = 1920;
            height = 1080;
        } else if (mode == "1440p") {
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
    } else if (mode == "native") {
        QSignalBlocker widthBlocker(m_widthSpin);
        QSignalBlocker heightBlocker(m_heightSpin);
        m_widthSpin->setValue(m_settings.resolution.nativeWidth);
        m_heightSpin->setValue(m_settings.resolution.nativeHeight);
    }

    bool isAuto = mode == "auto";
    bool isCustom = mode == "custom";
    bool isNative = mode == "native";
    m_widthSpin->setEnabled(isCustom);
    m_heightSpin->setEnabled(isCustom);
    m_refreshSpin->setEnabled(!isAuto && !isNative);
    if (isNative) {
        m_widthSpin->setEnabled(false);
        m_heightSpin->setEnabled(false);
    }
}

void MainWindow::resetMediaInfo(const QString &status) {
    m_streaming = false;
    m_streamWidth = 0;
    m_streamHeight = 0;
    updateOutputLabel();
    if (m_mediaStatusAction) m_mediaStatusAction->setText("Status: " + status);
    if (m_videoCodecAction) m_videoCodecAction->setText("Video Codec: -");
    if (m_videoDecoderAction) m_videoDecoderAction->setText("Video Decoder: -");
    if (m_videoRendererAction) m_videoRendererAction->setText("Video Renderer: -");
    if (m_streamSizeAction) m_streamSizeAction->setText("Stream Size: -");
    if (m_audioCodecAction) m_audioCodecAction->setText("Audio Codec: -");
    if (m_audioFormatAction) m_audioFormatAction->setText("Audio Format: -");
}

void MainWindow::updateMediaInfo(const QString &key, const QString &value) {
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("media"), key,
            QJsonObject{{QStringLiteral("value"), value}});
    }
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
        static const QRegularExpression sizePattern(
            QStringLiteral("(\\d+)x(\\d+)"));
        const QRegularExpressionMatch match = sizePattern.match(value);
        if (match.hasMatch()) {
            m_streamWidth = match.captured(1).toInt();
            m_streamHeight = match.captured(2).toInt();
            updateOutputLabel();
            if (m_videoWindowHandle && !m_videoWindowApplied) {
                syncVideoWindow();
            }
        }
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

    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("client"), QStringLiteral("connected"),
            QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("model"), model},
                        {QStringLiteral("deviceid"), deviceid},
                        {QStringLiteral("connections"), m_connectionCount}});
    }

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
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("client"), QStringLiteral("disconnected"),
            QJsonObject{{QStringLiteral("name"), m_clientName},
                        {QStringLiteral("model"), m_clientModel},
                        {QStringLiteral("deviceid"), m_clientDeviceId}});
    }
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
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("client"), QStringLiteral("rejected"),
            QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("model"), model},
                        {QStringLiteral("deviceid"), deviceid}});
    }
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
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("client"),
                             QStringLiteral("connection count"),
                             QJsonObject{{QStringLiteral("count"), count}});
    }
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
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("settings"),
                             QStringLiteral("display name changed"),
                             QJsonObject{{QStringLiteral("name"), displayName}});
    }

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
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("engine"), QStringLiteral("starting"),
            QJsonObject{{QStringLiteral("args"),
                         QJsonArray::fromStringList(args)}});
    }
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
    saveVideoWindowGeometry();
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("engine"),
                             QStringLiteral("stopping"));
    }
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
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("engine"),
                             QStringLiteral("ready"));
    }
    updateStatus();

    if (!m_videoTitleTimer) {
        m_videoTitleTimer = new QTimer(this);
        m_videoTitleTimer->setInterval(250);
        connect(m_videoTitleTimer, &QTimer::timeout, this,
                &MainWindow::syncVideoWindow);
    }
    m_videoTitleTimer->start(250);
}

void MainWindow::syncVideoWindow() {
    if (!m_running || m_quitting) {
        return;
    }

    VideoWindowData data;
    data.pid = GetCurrentProcessId();
    data.newTitle = "AirPlay Video Stream (ALT+ENTER for Fullscreen)";
    EnumWindows(EnumWindowsProcVideo, reinterpret_cast<LPARAM>(&data));

    if (!data.found) {
        if (m_videoWindowHandle
            && !IsWindow(reinterpret_cast<HWND>(m_videoWindowHandle))) {
            m_videoWindowHandle = 0;
            m_videoWindowApplied = false;
            m_lastVideoWindowRect = QRect();
        }
        updateHud();
        return;
    }

    const quintptr handle = reinterpret_cast<quintptr>(data.found);
    if (handle != m_videoWindowHandle) {
        m_videoWindowHandle = handle;
        m_videoWindowApplied = false;
        m_lastVideoWindowRect = QRect();
    }

    if (!m_videoWindowApplied) {
        if (m_settings.videoWindow.remember
            && m_settings.videoWindow.hasGeometry) {
            QRect target(m_settings.videoWindow.x, m_settings.videoWindow.y,
                         m_settings.videoWindow.width,
                         m_settings.videoWindow.height);
            bool onScreen = false;
            for (QScreen *screen : QGuiApplication::screens()) {
                if (screen->availableGeometry().intersects(target)) {
                    onScreen = true;
                    break;
                }
            }
            if (onScreen && target.width() >= 320 && target.height() >= 200) {
                applyVideoWindowGeometry(target);
            } else if (m_streamWidth > 0 && m_streamHeight > 0) {
                fitVideoWindowToStream(m_streamWidth, m_streamHeight);
            }
        } else if (m_streamWidth > 0 && m_streamHeight > 0) {
            fitVideoWindowToStream(m_streamWidth, m_streamHeight);
        }
    }

    if (m_videoWindowApplied) {
        saveVideoWindowGeometry();
    }
    updateHud();
}

void MainWindow::applyVideoWindowGeometry(const QRect &geometry) {
    if (!m_videoWindowHandle) {
        return;
    }

    HWND handle = reinterpret_cast<HWND>(m_videoWindowHandle);
    SetWindowPos(handle, nullptr, geometry.x(), geometry.y(),
                 geometry.width(), geometry.height(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    m_lastVideoWindowRect = geometry;
    m_videoWindowApplied = true;
}

void MainWindow::fitVideoWindowToStream(int width, int height) {
    if (!m_videoWindowHandle || width <= 0 || height <= 0) {
        return;
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const int maxWidth = qMax(640, available.width() * 8 / 10);
    const int maxHeight = qMax(480, available.height() * 8 / 10);
    const double aspect = static_cast<double>(width) / height;

    int targetWidth = maxWidth;
    int targetHeight = static_cast<int>(targetWidth / aspect);
    if (targetHeight > maxHeight) {
        targetHeight = maxHeight;
        targetWidth = static_cast<int>(targetHeight * aspect);
    }
    const int x = available.x() + (available.width() - targetWidth) / 2;
    const int y = available.y() + (available.height() - targetHeight) / 2;

    applyVideoWindowGeometry(QRect(x, y, targetWidth, targetHeight));

    if (m_settings.videoWindow.remember) {
        m_settings.videoWindow.hasGeometry = true;
        m_settings.videoWindow.x = x;
        m_settings.videoWindow.y = y;
        m_settings.videoWindow.width = targetWidth;
        m_settings.videoWindow.height = targetHeight;
        m_settings.save();
    }
}

void MainWindow::saveVideoWindowGeometry() {
    if (!m_settings.videoWindow.remember || !m_videoWindowApplied
        || !m_videoWindowHandle) {
        return;
    }

    HWND handle = reinterpret_cast<HWND>(m_videoWindowHandle);
    if (!IsWindow(handle) || IsIconic(handle) || IsZoomed(handle)) {
        return;
    }

    RECT rect = {};
    if (!GetWindowRect(handle, &rect)) {
        return;
    }

    const QRect current(rect.left, rect.top, rect.right - rect.left,
                        rect.bottom - rect.top);
    if (current == m_lastVideoWindowRect) {
        return;
    }

    m_lastVideoWindowRect = current;
    m_settings.videoWindow.hasGeometry = true;
    m_settings.videoWindow.x = current.x();
    m_settings.videoWindow.y = current.y();
    m_settings.videoWindow.width = current.width();
    m_settings.videoWindow.height = current.height();
    m_settings.save();
}

void MainWindow::onAirplayStopped() {
    m_running = false;
    if (m_videoTitleTimer) m_videoTitleTimer->stop();
    m_videoWindowHandle = 0;
    m_videoWindowApplied = false;
    m_lastVideoWindowRect = QRect();
    m_streamWidth = 0;
    m_streamHeight = 0;
    clearClientInfo();
    resetMediaInfo("Not connected");
    qInfo() << "UxPlay engine stopped";
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("engine"), QStringLiteral("stopped"),
            QJsonObject{{QStringLiteral("restart_attempt"),
                         m_restartAttempt}});
    }

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
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("engine"), QStringLiteral("error"),
            QJsonObject{{QStringLiteral("message"), message}});
    }
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
        qInfo() << "USB Clean Output stopped";
    });
    connect(m_usbObs, &UsbObsController::outputClosed, this, [this]() {
        setObsMode(QStringLiteral("off"), true);
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

void MainWindow::ensureUsbConnector() {
    if (m_usbConnector) {
        return;
    }

    m_usbConnector = new UsbDeviceConnector(this);
    connect(m_usbConnector, &UsbDeviceConnector::statusChanged, this,
            [this](const QString &text, bool busy) {
                if (m_usbStatusLabel) {
                    m_usbStatusLabel->setText(text);
                    m_usbStatusLabel->setStyleSheet(busy ? "color:#666;" : "");
                }
                if (m_usbConnectBtn) {
                    m_usbConnectBtn->setEnabled(!busy);
                }
                if (m_usbObsAction) {
                    if (busy) {
                        m_usbObsAction->setText("Cancel USB check");
                    } else if (!(m_usbObs && m_usbObs->isRunning())) {
                        m_usbObsAction->setText(
                            "USB Clean Output (experimental)");
                    }
                }
                qInfo() << "[usb-device]" << text;
            });
    connect(m_usbConnector, &UsbDeviceConnector::ready, this,
            [this](const QString &name) {
                m_usbDeviceReady = true;
                const bool charging =
                    m_usbConnector ? m_usbConnector->isCharging() : true;
                const int percent =
                    m_usbConnector ? m_usbConnector->batteryPercent() : -1;
                const int watts =
                    m_usbConnector ? m_usbConnector->adapterWatts() : 0;
                if (m_usbStatusLabel) {
                    m_usbStatusLabel->setStyleSheet(
                        charging ? "color:#1b7f3a;" : "color:#b26a00;");
                    QString tooltip =
                        m_usbConnector ? m_usbConnector->powerSummary()
                                       : QString();
                    if (!charging) {
                        tooltip += QStringLiteral(
                            "\nUSB uses the iPad's only port. Unplug to "
                            "charge, or use a PD dock.");
                    }
                    m_usbStatusLabel->setToolTip(tooltip);
                }
                if (m_usbConnectBtn) {
                    m_usbConnectBtn->setText("Reconnect USB device");
                }
                if (m_obsModeCombo &&
                    m_obsModeCombo->currentData().toString() ==
                        QStringLiteral("window")) {
                    ensureUsbObsController();
                    if (m_usbObs && !m_usbObs->isRunning()) {
                        m_usbObs->start();
                    }
                }
                if (m_tray) {
                    QString warning;
                    if (!charging && watts > 0 && watts < 10) {
                        warning = QStringLiteral(
                                      "iPad is not charging (%1W adapter); "
                                      "USB uses its only port. Unplug to "
                                      "charge, or use a PD dock.")
                                      .arg(watts);
                    } else if (!charging && percent >= 0 && percent < 20) {
                        warning = QStringLiteral("iPad battery is low (%1%).")
                                      .arg(percent);
                    }
                    if (!warning.isEmpty()) {
                        m_tray->showMessage("uxplay-windows", warning,
                                            QSystemTrayIcon::Warning, 10000);
                    } else {
                        m_tray->showMessage(
                            "uxplay-windows",
                            QStringLiteral("USB ready: %1").arg(name),
                            QSystemTrayIcon::Information, 3000);
                    }
                }
            });
    connect(m_usbConnector, &UsbDeviceConnector::failed, this,
            [this](const QString &message) {
                m_usbDeviceReady = false;
                if (m_usbStatusLabel) {
                    m_usbStatusLabel->setStyleSheet("color:#b00020;");
                }
                if (m_usbConnectBtn) {
                    m_usbConnectBtn->setText("Retry USB connection");
                }
                if (m_tray) {
                    m_tray->showMessage("uxplay-windows", message,
                                        QSystemTrayIcon::Warning, 7000);
                }
            });
}

void MainWindow::setObsMode(const QString &mode, bool persist) {
    QString normalized = mode;
    if (normalized != QStringLiteral("off") &&
        normalized != QStringLiteral("window")) {
        normalized = QStringLiteral("off");
    }

    if (m_obsModeCombo) {
        const int index = m_obsModeCombo->findData(normalized);
        if (index >= 0) {
            QSignalBlocker blocker(m_obsModeCombo);
            m_obsModeCombo->setCurrentIndex(index);
        }
    }

    if (persist && m_settings.obsMode != normalized) {
        m_settings.obsMode = normalized;
        m_settings.save();
    }
    if (m_diagnostics) {
        m_diagnostics->setSettings(m_settings);
        m_diagnostics->event(QStringLiteral("obs"),
                             QStringLiteral("mode"),
                             QJsonObject{{QStringLiteral("mode"), normalized}});
    }

    if (normalized == QStringLiteral("window")) {
        ensureUsbConnector();
        ensureUsbObsController();
        if (!m_usbObs || !m_usbConnector) {
            return;
        }
        if (m_usbDeviceReady) {
            if (m_usbObs->isRunning()) {
                if (m_usbObs->outputWindow()) {
                    m_usbObs->outputWindow()->show();
                    m_usbObs->outputWindow()->raise();
                }
            } else {
                m_usbObs->start();
            }
        } else {
            m_usbConnector->connectDevice();
        }
    } else {
        if (m_usbConnector) {
            m_usbConnector->cancel();
        }
        if (m_usbObs) {
            m_usbObs->stop();
        }
    }
}

void MainWindow::onObsModeChanged(int /*index*/) {
    if (!m_obsModeCombo) {
        return;
    }
    const QString mode = m_obsModeCombo->currentData().toString();
    if (mode == QStringLiteral("camera")) {
        if (m_tray) {
            m_tray->showMessage(
                "uxplay-windows",
                "Virtual camera is not implemented yet. Use Clean window for now.",
                QSystemTrayIcon::Information, 5000);
        }
        setObsMode(QStringLiteral("off"), true);
        return;
    }
    setObsMode(mode, true);
}

void MainWindow::onConnectUsbClicked() {
    ensureUsbConnector();
    if (!m_obsModeCombo ||
        m_obsModeCombo->currentData().toString() != QStringLiteral("window")) {
        setObsMode(QStringLiteral("window"), true);
        return;
    }
    m_usbDeviceReady = false;
    if (m_usbConnectBtn) {
        m_usbConnectBtn->setText("Connect USB device");
    }
    if (m_usbConnector) {
        m_usbConnector->connectDevice();
    }
}

void MainWindow::toggleUsbObs() {
    if (m_usbObs && m_usbObs->isRunning()) {
        setObsMode(QStringLiteral("off"), true);
    } else {
        setObsMode(QStringLiteral("window"), true);
    }
}

void MainWindow::startUsbObs(bool minimize) {
    setObsMode(QStringLiteral("window"), true);
    if (minimize) {
        hide();
    }
}

void MainWindow::rebuildAirplayStream() {
    if (!m_serverEnabled || m_quitting) {
        return;
    }

    qInfo() << "Rebuilding AirPlay stream (manual)";
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("engine"),
                             QStringLiteral("rebuild requested"));
    }
    if (m_tray && !m_quitting) {
        m_tray->showMessage("uxplay-windows",
                            "Rebuilding AirPlay stream...",
                            QSystemTrayIcon::Information, 2000);
    }

    stopServer();
    QTimer::singleShot(300, this, [this]() {
        if (m_serverEnabled && !m_quitting) {
            startServer();
        }
    });
}

void MainWindow::setServerEnabled(bool enabled) {
    if (m_serverEnabled == enabled) return;

    m_serverEnabled = enabled;
    m_settings.serverEnabled = enabled;
    m_settings.save();
    m_restartAttempt = 0;
    if (m_diagnostics) {
        m_diagnostics->setSettings(m_settings);
        m_diagnostics->event(
            QStringLiteral("engine"),
            enabled ? QStringLiteral("server enabled")
                    : QStringLiteral("server disabled"));
    }

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

void MainWindow::populateLogMenu(QMenu *menu) {
    QAction *open = menu->addAction("Open Log");
    connect(open, &QAction::triggered, this, &MainWindow::openLogFile);

    QAction *openWith = menu->addAction("Open Log With...");
    connect(openWith, &QAction::triggered, this,
            []() { applog::openLogFileWith(); });

    QAction *logFolder = menu->addAction("Open Log Folder");
    connect(logFolder, &QAction::triggered, this,
            []() { applog::openLogFolder(); });

    QAction *diagnosticsFolder =
        menu->addAction("Open Diagnostics Folder");
    connect(diagnosticsFolder, &QAction::triggered, this,
            []() { applog::openDiagnosticsFolder(); });
}

void MainWindow::toggleDiagnostics(bool enabled) {
    if (m_settings.diagnosticsEnabled != enabled) {
        m_settings.diagnosticsEnabled = enabled;
        if (m_diagnostics) {
            m_diagnostics->setSettings(m_settings);
        }
        m_settings.save();
    }

    if (m_diagnostics) {
        if (enabled) {
            m_diagnostics->setEnabled(true);
            m_diagnostics->event(QStringLiteral("diagnostics"),
                                 QStringLiteral("recording enabled"));
        } else {
            m_diagnostics->event(QStringLiteral("diagnostics"),
                                 QStringLiteral("recording disabled"));
            m_diagnostics->setEnabled(false);
        }
    }

    if (m_diagnosticsCheckbox
        && m_diagnosticsCheckbox->isChecked() != enabled) {
        QSignalBlocker blocker(m_diagnosticsCheckbox);
        m_diagnosticsCheckbox->setChecked(enabled);
    }
    if (m_diagnosticsRecordAction
        && m_diagnosticsRecordAction->isChecked() != enabled) {
        QSignalBlocker blocker(m_diagnosticsRecordAction);
        m_diagnosticsRecordAction->setChecked(enabled);
    }
}

void MainWindow::toggleHud(bool enabled) {
    m_settings.hudEnabled = enabled;
    m_settings.save();
    updateHud();
}

void MainWindow::setHudCorner(const QString &corner) {    if (corner != QStringLiteral("top-left")
        && corner != QStringLiteral("top-right")) {
        return;
    }
    m_settings.hudCorner = corner;
    m_settings.save();
    updateHud();
}

void MainWindow::updateHud() {
    if (m_hudCheckbox
        && m_hudCheckbox->isChecked() != m_settings.hudEnabled) {
        QSignalBlocker blocker(m_hudCheckbox);
        m_hudCheckbox->setChecked(m_settings.hudEnabled);
    }
    if (m_hudAction
        && m_hudAction->isChecked() != m_settings.hudEnabled) {
        QSignalBlocker blocker(m_hudAction);
        m_hudAction->setChecked(m_settings.hudEnabled);
    }
    if (m_hudTopLeftAction) {
        m_hudTopLeftAction->setChecked(
            m_settings.hudCorner == QStringLiteral("top-left"));
    }
    if (m_hudTopRightAction) {
        m_hudTopRightAction->setChecked(
            m_settings.hudCorner == QStringLiteral("top-right"));
    }

    if (!m_settings.hudEnabled || !m_running || m_clientName.isEmpty()) {
        set_uxplay_hud_text("", 0);
        return;
    }

    QStringList lines;
    auto actionText = [](QAction *action) {
        return action ? action->text() : QString();
    };

    if (m_clientAction) {
        lines << actionText(m_clientAction);
    }
    if (m_mediaStatusAction) {
        lines << actionText(m_mediaStatusAction);
    }
    if (m_videoCodecAction) {
        lines << actionText(m_videoCodecAction);
    }

    const QString requested =
        m_settings.resolution.mode == QStringLiteral("native")
            ? QStringLiteral("%1x%2")
                  .arg(m_settings.resolution.nativeWidth)
                  .arg(m_settings.resolution.nativeHeight)
            : QStringLiteral("%1x%2")
                  .arg(m_settings.resolution.width)
                  .arg(m_settings.resolution.height);
    const QString actual =
        (m_streamWidth > 0 && m_streamHeight > 0)
            ? QStringLiteral("%1x%2").arg(m_streamWidth).arg(m_streamHeight)
            : QStringLiteral("waiting");
    lines << QStringLiteral("Output: ") + actual;
    lines << QStringLiteral("Requested: ") + requested;

    if (m_videoDecoderAction) {
        lines << actionText(m_videoDecoderAction);
    }
    if (m_videoRendererAction) {
        lines << actionText(m_videoRendererAction);
    }
    if (m_audioCodecAction) {
        lines << actionText(m_audioCodecAction);
    }
    if (m_audioFormatAction) {
        lines << actionText(m_audioFormatAction);
    }

    const QByteArray text = lines.join(QLatin1Char('\n')).toUtf8();
    set_uxplay_hud_text(text.constData(),
                        m_settings.hudCorner == QStringLiteral("top-right"));
}

void MainWindow::updateOutputLabel() {
    if (!m_outputLabel) {
        return;
    }

    const QString requested =
        m_settings.resolution.mode == QStringLiteral("native")
            ? QStringLiteral("%1x%2")
                  .arg(m_settings.resolution.nativeWidth)
                  .arg(m_settings.resolution.nativeHeight)
            : QStringLiteral("%1x%2")
                  .arg(m_settings.resolution.width)
                  .arg(m_settings.resolution.height);
    const QString actual =
        (m_streamWidth > 0 && m_streamHeight > 0)
            ? QStringLiteral("%1x%2").arg(m_streamWidth).arg(m_streamHeight)
            : QStringLiteral("waiting");
    m_outputLabel->setText(
        QStringLiteral("Output: %1   (requested %2)").arg(actual, requested));
}

void MainWindow::saveDiagnostics(bool showDialog) {
    if (!m_diagnostics) {
        return;
    }

    m_diagnostics->setSettings(m_settings);
    m_diagnostics->event(QStringLiteral("diagnostics"),
                         QStringLiteral("saving"));

    QString error;
    const QString path = m_diagnostics->exportBundle(&error);
    if (path.isEmpty()) {
        const QString message =
            error.isEmpty() ? QStringLiteral("Could not save diagnostics.")
                            : error;
        qCritical() << "Diagnostics export failed:" << message;
        if (showDialog) {
            QMessageBox::warning(this, "Diagnostics", message);
        } else if (m_tray) {
            m_tray->showMessage("uxplay-windows", message,
                                QSystemTrayIcon::Warning, 6000);
        }
        return;
    }

    qInfo() << "Diagnostics bundle saved to" << path;
    if (!showDialog) {
        if (m_tray) {
            m_tray->showMessage("Diagnostics saved", path,
                                QSystemTrayIcon::Information, 5000);
        }
        return;
    }

    QWidget *dialogParent = isVisible() ? this : nullptr;
    QMessageBox box(dialogParent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle("Diagnostics saved");
    box.setText("Recent diagnostics were saved.");
    box.setInformativeText(path);
    box.setDetailedText(
        "The bundle is generated locally and redacted, but review it before "
        "sharing. It contains logs, settings, events, metrics and system "
        "information.");
    QPushButton *openFolder =
        box.addButton("Open folder", QMessageBox::AcceptRole);
    QPushButton *copyPath =
        box.addButton("Copy path", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Ok);
    box.exec();

    if (box.clickedButton() == openFolder) {
        const QFileInfo info(path);
        const QString folder = info.isDir() ? path : info.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    } else if (box.clickedButton() == copyPath) {
        QApplication::clipboard()->setText(path);
    }
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
    if (m_usbConnector) {
        m_usbConnector->cancel();
    }
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

    if (m_diagnostics) {
        m_diagnostics->setStatus(status);
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
    updateHud();
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
