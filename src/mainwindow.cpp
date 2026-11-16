#include "mainwindow.h"
#include "airplayworker.h"
#include "logging.h"
#include "usbobscontroller.h"
#include "usbdeviceconnector.h"
#include "cleanoutputwindow.h"
#include "diagnostics.h"
#include "diagnosticsexportdialog.h"
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
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
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

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this,
            &MainWindow::updateFrameStats);

    m_settingsSaveTimer = new QTimer(this);
    m_settingsSaveTimer->setSingleShot(true);
    m_settingsSaveTimer->setInterval(1200);
    connect(m_settingsSaveTimer, &QTimer::timeout, this,
            [this]() { m_settings.save(); });

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
    setWindowTitle(tr("uxplay-windows"));
    setWindowIcon(QApplication::windowIcon());
    setFixedSize(420, 745);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel(tr("Initializing..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: #b00020;");
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    m_pendingChangesRow = new QWidget(this);
    auto *pendingLayout = new QHBoxLayout(m_pendingChangesRow);
    pendingLayout->setContentsMargins(0, 0, 0, 0);
    m_pendingChangesLabel = new QLabel(
        tr("Engine settings changed. Apply to restart the engine."),
        m_pendingChangesRow);
    m_pendingChangesLabel->setWordWrap(true);
    m_pendingChangesLabel->setStyleSheet("color:#8a6d00;");
    m_applyChangesButton = new QPushButton(
        tr("Apply & restart engine"), m_pendingChangesRow);
    connect(m_applyChangesButton, &QPushButton::clicked, this,
            &MainWindow::applyPendingEngineChanges);
    pendingLayout->addWidget(m_pendingChangesLabel, 1);
    pendingLayout->addWidget(m_applyChangesButton);
    layout->addWidget(m_pendingChangesRow);
    m_pendingChangesRow->setVisible(false);
    

    // Display Name
    auto *nameLayout = new QHBoxLayout();
    auto *nameLabel = new QLabel(tr("AirPlay Name"), this);
    m_displayNameEdit = new QLineEdit(this);
    m_displayNameEdit->setText(m_settings.displayName);
    m_displayNameEdit->setPlaceholderText(tr("uxplay-windows"));
    auto *applyNameBtn = new QPushButton(tr("Apply"), this);
    connect(applyNameBtn, &QPushButton::clicked, this, &MainWindow::applyDisplayName);
    connect(m_displayNameEdit, &QLineEdit::returnPressed, this, &MainWindow::applyDisplayName);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(m_displayNameEdit, 1);
    nameLayout->addWidget(applyNameBtn);
    layout->addLayout(nameLayout);


    // Bluetooth Discovery Checkbox
    m_bleCheckbox = new QCheckBox(tr("Enable Bluetooth Discovery"), this);
    m_bleCheckbox->setChecked(m_settings.bleEnabled);
    connect(m_bleCheckbox, &QCheckBox::toggled, this, &MainWindow::toggleBle);
    layout->addWidget(m_bleCheckbox);

    // Force Fullscreen Checkbox
    m_fullscreenCheckbox = new QCheckBox(tr("Force Fullscreen (must select renderer)"), this);
    m_fullscreenCheckbox->setChecked(m_settings.forceFullscreen);
    connect(m_fullscreenCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleForceFullscreen);
    layout->addWidget(m_fullscreenCheckbox);

    // Renderer dropdown
    m_rendererCombo = new QComboBox(this);
    m_rendererCombo->addItem(tr("Video Renderer (Auto)"), "auto");
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
    auto *codecLabel = new QLabel(tr("Video Codec"), this);
    m_videoCodecCombo = new QComboBox(this);
    m_videoCodecCombo->addItem(tr("HEVC / H.265 (needs >1080p)"), "hevc");
    m_videoCodecCombo->addItem(tr("H.264 / AVC"), "h264");
    m_videoCodecCombo->setToolTip(
        tr("HEVC requires a requested height above 1080p (for example 4K). "
           "At 1080p the client uses H.264 even when HEVC is enabled."));
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
    auto *resolutionLabel = new QLabel(tr("Resolution"), this);
    m_resolutionCombo = new QComboBox(this);
    m_resolutionCombo->addItem(tr("Native (device)"), "native");
    m_resolutionCombo->addItem("1280 x 720", "720p");
    m_resolutionCombo->addItem("1920 x 1080", "1080p");
    m_resolutionCombo->addItem("2560 x 1440", "1440p");
    m_resolutionCombo->addItem("3840 x 2160", "4k");
    m_resolutionCombo->addItem(tr("Custom"), "custom");
    m_resolutionCombo->setToolTip(
        tr("Native uses the configured width/height below; set Custom first "
           "to change them. It is a request, not an automatic device query."));
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

    auto *obsGroup = new QGroupBox(tr("OBS output (USB capture)"), this);
    auto *obsLayout = new QVBoxLayout(obsGroup);
    m_obsModeCombo = new QComboBox(obsGroup);
    m_obsModeCombo->addItem(tr("Off"), "off");
    m_obsModeCombo->addItem(tr("Clean window (Window Capture)"), "window");
    m_obsModeCombo->addItem(tr("Virtual camera (not implemented)"), "camera");
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
    m_usbStatusLabel = new QLabel(tr("USB: not checked"), obsGroup);
    if (m_settings.obsMode == QStringLiteral("window")) {
        m_usbStatusLabel->setText(tr("USB: not started (press Connect)"));
    }
    m_usbStatusLabel->setWordWrap(true);
    m_usbConnectBtn = new QPushButton(tr("Connect USB device"), obsGroup);
    connect(m_usbConnectBtn, &QPushButton::clicked, this,
            &MainWindow::onConnectUsbClicked);
    m_obsHint = new QLabel(
        tr("Clean window: press Connect USB device, then select the window in OBS."),
        obsGroup);
    m_obsHint->setWordWrap(true);
    obsLayout->addWidget(m_obsModeCombo);
    obsLayout->addWidget(m_usbStatusLabel);
    obsLayout->addWidget(m_usbConnectBtn);
    obsLayout->addWidget(m_obsHint);
    layout->addWidget(obsGroup);

    m_settingsBtn = new QPushButton(tr("Edit Settings (Advanced)"), this);
    connect(m_settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettingsFile);
    layout->addWidget(m_settingsBtn);

    m_listargsBtn = new QPushButton(tr("List UxPlay arguments"), this);
    connect(m_listargsBtn, &QPushButton::clicked, this, &MainWindow::openListArgsFile);
    layout->addWidget(m_listargsBtn);

    m_autostartBtn = new QPushButton(this);
    connect(m_autostartBtn, &QPushButton::clicked, this, &MainWindow::toggleAutostart);
    layout->addWidget(m_autostartBtn);

    m_licenseBtn = new QPushButton(tr("License Information"), this);
    connect(m_licenseBtn, &QPushButton::clicked, this, &MainWindow::showLicense);

    m_openLogBtn = new QToolButton(this);
    m_openLogBtn->setText(tr("Open Log"));
    m_openLogBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_openLogBtn->setPopupMode(QToolButton::InstantPopup);
    m_openLogBtn->setToolTip(
        tr("Open the log, choose a viewer, or open log/diagnostics folders."));
    auto *logMenu = new QMenu(this);
    populateLogMenu(logMenu);
    m_openLogBtn->setMenu(logMenu);

    auto *diagnosticsLayout = new QHBoxLayout();
    m_diagnosticsCheckbox = new QCheckBox(tr("Keep diagnostics"), this);
    m_diagnosticsCheckbox->setChecked(m_settings.diagnosticsEnabled);
    m_diagnosticsCheckbox->setToolTip(
        tr("Keep recent events and metrics in memory for export."));
    connect(m_diagnosticsCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleDiagnostics);
    m_diagnosticsBtn = new QPushButton(tr("Save Diagnostics"), this);
    connect(m_diagnosticsBtn, &QPushButton::clicked, this,
            [this]() { saveDiagnostics(true); });
    diagnosticsLayout->addWidget(m_diagnosticsCheckbox, 1);
    diagnosticsLayout->addWidget(m_diagnosticsBtn);
    layout->addLayout(diagnosticsLayout);

    m_hudCheckbox = new QCheckBox(tr("Media Info HUD"), this);
    m_hudCheckbox->setChecked(m_settings.hudEnabled);
    m_hudCheckbox->setToolTip(
        tr("Draw the media information in the AirPlay video frame."));
    connect(m_hudCheckbox, &QCheckBox::toggled, this,
            &MainWindow::toggleHud);
    layout->addWidget(m_hudCheckbox);

    auto *hudMarginLayout = new QHBoxLayout();
    hudMarginLayout->addWidget(new QLabel(tr("HUD margin"), this));
    m_hudXSpin = new QSpinBox(this);
    m_hudXSpin->setRange(0, 1000);
    m_hudXSpin->setSuffix(" x");
    m_hudXSpin->setValue(m_settings.hudMarginX);
    m_hudYSpin = new QSpinBox(this);
    m_hudYSpin->setRange(0, 1000);
    m_hudYSpin->setSuffix(" y");
    m_hudYSpin->setValue(m_settings.hudMarginY);
    connect(m_hudXSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                m_settings.hudMarginX = value;
                saveSettingsSoon();
                updateHud();
            });
    connect(m_hudYSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                m_settings.hudMarginY = value;
                saveSettingsSoon();
                updateHud();
            });
    hudMarginLayout->addWidget(m_hudXSpin);
    hudMarginLayout->addWidget(m_hudYSpin);
    m_hudFieldsButton = new QPushButton(tr("Fields..."), this);
    m_hudFieldsButton->setToolTip(tr("Choose which lines the HUD shows."));
    m_hudFieldsMenu = new QMenu(this);
    buildHudFieldsMenu(m_hudFieldsMenu);
    m_hudFieldsButton->setMenu(m_hudFieldsMenu);
    hudMarginLayout->addWidget(m_hudFieldsButton);
    layout->addLayout(hudMarginLayout);

    m_rebuildButton = new QPushButton(tr("Rebuild AirPlay stream"), this);
    m_rebuildButton->setToolTip(
        tr("Stop and restart the engine if the video window is frozen or missing."));
    connect(m_rebuildButton, &QPushButton::clicked, this,
            &MainWindow::rebuildAirplayStream);
    layout->addWidget(m_rebuildButton);

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

    m_tray->showMessage(tr("Settings"),
                        tr("Edit extra_args in settings.json, then restart to apply."),
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
    m_tray->setToolTip(tr("uxplay-windows"));

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(tr("Show Settings"), this, &MainWindow::showAndRaise);
    m_trayMenu->addSeparator();
    m_mediaInfoMenu = m_trayMenu->addMenu(tr("Media Info"));
    m_clientAction = m_mediaInfoMenu->addAction(tr("Client: -"));
    m_connectionsAction = m_mediaInfoMenu->addAction(tr("Connections: 0"));
    m_mediaStatusAction = m_mediaInfoMenu->addAction(tr("Status: Not connected"));
    m_videoCodecAction = m_mediaInfoMenu->addAction(tr("Video Codec: -"));
    m_videoDecoderAction = m_mediaInfoMenu->addAction(tr("Video Decoder: -"));
    m_videoRendererAction = m_mediaInfoMenu->addAction(tr("Video Renderer: -"));
    m_streamSizeAction = m_mediaInfoMenu->addAction(tr("Stream Size: -"));
    m_audioCodecAction = m_mediaInfoMenu->addAction(tr("Audio Codec: -"));
    m_audioFormatAction = m_mediaInfoMenu->addAction(tr("Audio Format: -"));
    for (QAction *action : {m_clientAction, m_connectionsAction, m_mediaStatusAction,
                            m_videoCodecAction, m_videoDecoderAction,
                            m_videoRendererAction, m_streamSizeAction, m_audioCodecAction,
                            m_audioFormatAction}) {
        action->setEnabled(false);
    }
    m_trayMenu->addSeparator();
    m_toggleServerAction = m_trayMenu->addAction(tr("Stop AirPlay"), this, &MainWindow::toggleServer);
    m_usbObsAction = m_trayMenu->addAction(tr("USB Clean Output (experimental)"), this,
                                           &MainWindow::toggleUsbObs);
    QMenu *logsMenu = m_trayMenu->addMenu(tr("Logs & Diagnostics"));
    populateLogMenu(logsMenu);
    m_diagnosticsRecordAction =
        m_trayMenu->addAction(tr("Keep recent diagnostics"));
    m_diagnosticsRecordAction->setCheckable(true);
    m_diagnosticsRecordAction->setChecked(m_settings.diagnosticsEnabled);
    connect(m_diagnosticsRecordAction, &QAction::toggled, this,
            &MainWindow::toggleDiagnostics);
    m_saveDiagnosticsAction = m_trayMenu->addAction(tr("Save Diagnostics"));
    connect(m_saveDiagnosticsAction, &QAction::triggered, this,
            [this]() { saveDiagnostics(true); });
    m_hudAction = m_trayMenu->addAction(tr("Media Info HUD"));
    m_hudAction->setCheckable(true);
    m_hudAction->setChecked(m_settings.hudEnabled);
    connect(m_hudAction, &QAction::toggled, this, &MainWindow::toggleHud);
    m_videoOnTopAction = m_trayMenu->addAction(tr("Video always on top"));
    m_videoOnTopAction->setCheckable(true);
    m_videoOnTopAction->setChecked(m_settings.videoWindow.alwaysOnTop);
    connect(m_videoOnTopAction, &QAction::toggled, this,
            &MainWindow::toggleVideoAlwaysOnTop);
    QMenu *languageMenu = m_trayMenu->addMenu(tr("Language"));
    m_languageSystemAction = languageMenu->addAction(tr("System language"));
    m_languageSystemAction->setCheckable(true);
    m_languageEnglishAction = languageMenu->addAction(tr("English"));
    m_languageEnglishAction->setCheckable(true);
    m_languageChineseAction = languageMenu->addAction(tr("Traditional Chinese"));
    m_languageChineseAction->setCheckable(true);
    connect(m_languageSystemAction, &QAction::triggered, this,
            [this]() { setLanguage(QStringLiteral("system")); });
    connect(m_languageEnglishAction, &QAction::triggered, this,
            [this]() { setLanguage(QStringLiteral("en")); });
    connect(m_languageChineseAction, &QAction::triggered, this,
            [this]() { setLanguage(QStringLiteral("zh_TW")); });
    QMenu *hudPosition = m_trayMenu->addMenu(tr("HUD position"));
    m_hudTopLeftAction = hudPosition->addAction(tr("Top left"));
    m_hudTopLeftAction->setCheckable(true);
    m_hudTopRightAction = hudPosition->addAction(tr("Top right"));
    m_hudTopRightAction->setCheckable(true);
    m_hudBottomLeftAction = hudPosition->addAction(tr("Bottom left"));
    m_hudBottomLeftAction->setCheckable(true);
    m_hudBottomRightAction = hudPosition->addAction(tr("Bottom right"));
    m_hudBottomRightAction->setCheckable(true);
    connect(m_hudTopLeftAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("top-left"));
    });
    connect(m_hudTopRightAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("top-right"));
    });
    connect(m_hudBottomLeftAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("bottom-left"));
    });
    connect(m_hudBottomRightAction, &QAction::triggered, this, [this]() {
        setHudCorner(QStringLiteral("bottom-right"));
    });
    QMenu *hudFields = m_trayMenu->addMenu(tr("HUD fields"));
    buildHudFieldsMenu(hudFields);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Rebuild AirPlay stream"), this,
                         &MainWindow::rebuildAirplayStream);
    m_trayMenu->addAction(tr("Restart"), this, &MainWindow::restartApplication);
    m_trayMenu->addAction(tr("Quit"), this, &MainWindow::quit);

    m_tray->setContextMenu(m_trayMenu);
    
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        if (isVisible() && isActiveWindow() && !isMinimized()) {
            hide();
        } else {
            showAndRaise();
        }
    }
}

void MainWindow::showAndRaise() {
    showNormal();
    raise();
    activateWindow();
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (IsWindow(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
    }
#endif
}

void MainWindow::toggleBle(bool checked) {
    if (m_settings.bleEnabled == checked) {
        return;
    }

    m_settings.bleEnabled = checked;
    markEngineSettingsChanged(QStringLiteral("bluetooth discovery changed"));
}

void MainWindow::toggleForceFullscreen(bool checked) {
    if (m_settings.forceFullscreen == checked) {
        return;
    }

    m_settings.forceFullscreen = checked;
    markEngineSettingsChanged(QStringLiteral("fullscreen changed"));
}

void MainWindow::onRendererChanged(int /*index*/) {
    if (!m_rendererCombo) return;

    const QString mode = m_rendererCombo->currentData().toString();
    if (m_settings.renderer == mode) return;

    m_settings.renderer = mode;
    markEngineSettingsChanged(QStringLiteral("renderer changed"));
}

void MainWindow::onVideoCodecChanged(int /*index*/) {
    if (!m_videoCodecCombo) return;

    const QString codec = m_videoCodecCombo->currentData().toString();
    if (m_settings.videoCodec == codec) return;

    m_settings.videoCodec = codec;
    markEngineSettingsChanged(QStringLiteral("video codec changed"));
    updateVideoCodecHint();

    const bool resAllowsHevc =
        m_settings.resolution.mode == QStringLiteral("auto")
        || m_settings.resolution.height > 1080;
    if (codec == QStringLiteral("hevc") && !resAllowsHevc) {
        m_tray->showMessage(tr("uxplay-windows"),
                        tr("HEVC needs height above 1080p; this resolution will use H.264."),
                        QSystemTrayIcon::Information, 5000);
        return;
    }
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
            tr("H.264 only: HEVC is not advertised to the client."));
    } else if (!resAllowsHevc) {
        m_videoCodecHint->setText(
            tr("HEVC needs height above 1080p; this resolution will use "
               "H.264."));
    } else {
        m_videoCodecHint->setText(
            tr("HEVC advertised; the client may still choose H.264."));
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
    markEngineSettingsChanged(QStringLiteral("resolution changed"));
    updateVideoCodecHint();
    updateOutputLabel();
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
    if (m_mediaStatusAction) m_mediaStatusAction->setText(tr("Status: %1").arg(status));
    if (m_videoCodecAction) m_videoCodecAction->setText(tr("Video Codec: -"));
    if (m_videoDecoderAction) m_videoDecoderAction->setText(tr("Video Decoder: -"));
    if (m_videoRendererAction) m_videoRendererAction->setText(tr("Video Renderer: -"));
    if (m_streamSizeAction) m_streamSizeAction->setText(tr("Stream Size: -"));
    if (m_audioCodecAction) m_audioCodecAction->setText(tr("Audio Codec: -"));
    if (m_audioFormatAction) m_audioFormatAction->setText(tr("Audio Format: -"));
}

void MainWindow::updateMediaInfo(const QString &key, const QString &value) {
    if (m_diagnostics) {
        m_diagnostics->event(
            QStringLiteral("media"), key,
            QJsonObject{{QStringLiteral("value"), value}});
    }
    if (m_mediaStatusAction && key != "Video Decoder" && key != "Video Renderer") {
        m_mediaStatusAction->setText(tr("Status: Streaming"));
    }

    if (key == "Video Codec" && m_videoCodecAction) {
        m_videoCodecAction->setText(tr("Video Codec: %1").arg(value));
    } else if (key == "Video Decoder" && m_videoDecoderAction) {
        m_videoDecoderAction->setText(tr("Video Decoder: %1").arg(value));
    } else if (key == "Video Renderer" && m_videoRendererAction) {
        m_videoRendererAction->setText(tr("Video Renderer: %1").arg(value));
    } else if (key == "Stream Size" && m_streamSizeAction) {
        m_streamSizeAction->setText(tr("Stream Size: %1").arg(value));
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
        m_audioCodecAction->setText(tr("Audio Codec: %1").arg(value));
    } else if (key == "Audio Format" && m_audioFormatAction) {
        m_audioFormatAction->setText(tr("Audio Format: %1").arg(value));
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
    if (m_clientAction) m_clientAction->setText(tr("Client: -"));
    if (m_connectionsAction) m_connectionsAction->setText(tr("Connections: 0"));
}

void MainWindow::onClientConnected(const QString &name, const QString &model,
                                   const QString &deviceid) {
    const bool isNew = (deviceid != m_clientDeviceId) || m_clientName.isEmpty();
    if (isNew) {
        m_stalledFrameSamples = 0;
        m_stallRebuildCount = 0;
        m_lastStallRebuildMs = 0;
    }
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
        QString text = tr("Client: %1")
                           .arg(name.isEmpty() ? tr("unknown") : name);
        if (!model.isEmpty()) {
            text += " (" + model + ")";
        }
        m_clientAction->setText(text);
    }
    if (!m_streaming && m_mediaStatusAction) {
        m_mediaStatusAction->setText(tr("Status: Connected"));
    }

    if (isNew) {
        qInfo() << "Client connected:" << name << "model:" << model
                << "deviceid:" << deviceid;
        if (m_tray && !m_quitting) {
            m_tray->showMessage(
                tr("Device connected"),
                name.isEmpty() ? tr("A device connected") : name,
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
            m_tray->showMessage(tr("Device disconnected"), m_clientName,
                                QSystemTrayIcon::Information, 3000);
        }
    }

    clearClientInfo();
    resetMediaInfo(tr("Not connected"));
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
        const QString who = name.isEmpty() ? tr("A device") : name;
        m_tray->showMessage(tr("Connection rejected"), who,
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
        m_connectionsAction->setText(tr("Connections: %1").arg(count));
    }
}

void MainWindow::applyDisplayName() {
    if (!m_displayNameEdit) return;

    const QString displayName = m_displayNameEdit->text().trimmed();
    if (displayName.isEmpty()) {
        QMessageBox::warning(this, tr("AirPlay Name"),
                             tr("Enter a name for this AirPlay receiver."));
        m_displayNameEdit->setText(m_settings.displayName);
        return;
    }

    if (m_settings.displayName == displayName) {
        return;
    }

    m_settings.displayName = displayName;
    m_displayNameEdit->setText(displayName);
    markEngineSettingsChanged(QStringLiteral("display name changed"));

    m_tray->showMessage(tr("uxplay-windows"),
                        tr("AirPlay name saved. Apply engine changes to use it."),
                        QSystemTrayIcon::Information, 3000);
}

QString MainWindow::currentEngineArguments() const {
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString bleFilePath =
        QDir::toNativeSeparators(appData + "/uxplay_status.ble");
    const bool engineDebug =
        QCoreApplication::arguments().contains(QStringLiteral("--engine-debug"));
    return m_settings.engineArguments(bleFilePath, engineDebug).join(QChar(' '));
}

void MainWindow::refreshPendingChanges() {
    const bool pending =
        m_serverEnabled
        && !m_appliedEngineArguments.isEmpty()
        && currentEngineArguments() != m_appliedEngineArguments;
    m_engineChangesPending = pending;
    if (m_pendingChangesRow) {
        m_pendingChangesRow->setVisible(pending);
    }
    if (m_applyChangesButton) {
        m_applyChangesButton->setEnabled(pending);
    }
}

void MainWindow::markEngineSettingsChanged(const QString &reason) {
    m_settings.save();
    if (m_diagnostics) {
        m_diagnostics->setSettings(m_settings);
        m_diagnostics->event(
            QStringLiteral("settings"), reason,
            QJsonObject{{QStringLiteral("pending_engine_restart"), true}});
    }
    qInfo() << "Engine settings changed:" << reason;
    refreshPendingChanges();
}

void MainWindow::applyPendingEngineChanges() {
    if (!m_engineChangesPending) {
        return;
    }

    qInfo() << "Applying pending engine settings";
    if (m_diagnostics) {
        m_diagnostics->event(QStringLiteral("engine"),
                             QStringLiteral("apply pending settings"));
    }

    // Keep the row hidden while the requested restart is in progress.
    m_engineChangesPending = false;
    if (m_pendingChangesRow) {
        m_pendingChangesRow->setVisible(false);
    }

    if (!m_serverEnabled) {
        m_appliedEngineArguments = currentEngineArguments();
        return;
    }

    if (m_worker) {
        rebuildAirplayStream();
    } else {
        startServer();
    }
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
    m_appliedEngineArguments = args.join(QChar(' '));
    refreshPendingChanges();

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
    resetMediaInfo(tr("Waiting for stream"));
    if (m_lastEngineError.isEmpty() && m_errorLabel) {
        m_errorLabel->setVisible(false);
    }
    if (m_lastEngineError.isEmpty()) {
        m_appState = AppState::Starting;
    }
    updateStatus();

    m_worker = new AirPlayWorker(this);
    m_worker->setArgs(args);
    const bool engineTrace =
        QCoreApplication::arguments().contains(QStringLiteral("--engine-trace"));
    m_worker->setEngineTrace(engineTrace);
    if (engineDebug && !engineTrace) {
        qInfo() << "Engine packet/HTTP debug logs are filtered; use --engine-trace for full output";
    }

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
    if (m_statsTimer) m_statsTimer->stop();
    m_lastFrameCount = 0;
    m_lastStatsMs = 0;
    m_fps = 0.0;
    m_frameIntervalMs = 0.0;
    m_frameAgeMs = -1.0;
    m_lowFpsSamples = 0;
    m_lastStatsLogMs = 0;
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
    resetMediaInfo(tr("Not connected"));
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
    refreshPendingChanges();
    updateStatus();

    if (!m_videoTitleTimer) {
        m_videoTitleTimer = new QTimer(this);
        m_videoTitleTimer->setInterval(250);
        connect(m_videoTitleTimer, &QTimer::timeout, this,
                &MainWindow::syncVideoWindow);
    }
    m_videoTitleTimer->start(250);
    if (m_statsTimer) {
        m_lastFrameCount = 0;
        m_lastInputFrameCount = 0;
        m_lastStatsMs = 0;
        m_stalledFrameSamples = 0;
        m_inputFrameAgeMs = -1.0;
        m_statsTimer->start(1000);
    }
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
    const bool newWindow = handle != m_videoWindowHandle;
    if (newWindow) {
        m_videoWindowHandle = handle;
        m_videoWindowApplied = false;
        m_lastVideoWindowRect = QRect();

        HWND window = reinterpret_cast<HWND>(handle);
        ShowWindow(window, SW_RESTORE);
        /* Raise once above normal windows; do not stay topmost by default. */
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        if (m_settings.videoWindow.alwaysOnTop) {
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        } else {
            SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }

        /* SetForegroundWindow is often rejected for a background process.
         * Attach to the foreground thread for the call, and keep the
         * mirror window topmost so it is not hidden behind other windows. */
        HWND foreground = GetForegroundWindow();
        DWORD foregroundThread =
            foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
        const bool attached = foregroundThread && targetThread
                              && foregroundThread != targetThread
                              && AttachThreadInput(foregroundThread,
                                                   targetThread, TRUE);
        SetForegroundWindow(window);
        BringWindowToTop(window);
        if (attached) {
            AttachThreadInput(foregroundThread, targetThread, FALSE);
        }
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastGeometrySaveMs < 1000) {
        return;
    }
    m_lastGeometrySaveMs = now;

    m_lastVideoWindowRect = current;
    m_settings.videoWindow.hasGeometry = true;
    m_settings.videoWindow.x = current.x();
    m_settings.videoWindow.y = current.y();
    m_settings.videoWindow.width = current.width();
    m_settings.videoWindow.height = current.height();
    saveSettingsSoon();
}

void MainWindow::onAirplayStopped() {
    m_running = false;
    if (m_videoTitleTimer) m_videoTitleTimer->stop();
    if (m_statsTimer) m_statsTimer->stop();
    m_lastFrameCount = 0;
    m_lastStatsMs = 0;
    m_fps = 0.0;
    m_frameIntervalMs = 0.0;
    m_frameAgeMs = -1.0;
    m_lowFpsSamples = 0;
    m_lastStatsLogMs = 0;
    m_videoWindowHandle = 0;
    m_videoWindowApplied = false;
    m_lastVideoWindowRect = QRect();
    m_streamWidth = 0;
    m_streamHeight = 0;
    clearClientInfo();
    resetMediaInfo(tr("Not connected"));
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
            tr("uxplay-windows"),
            enable ? tr("Starting AirPlay") : tr("Stopping AirPlay"),
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
            m_usbObsAction->setText(tr("Stop USB Clean Output"));
        }
        if (m_tray) {
            m_tray->showMessage(
                tr("uxplay-windows"),
                tr("USB Clean Output started. Add it in OBS with Window Capture."),
                QSystemTrayIcon::Information, 4000);
        }
        qInfo() << "USB Clean Output started";
    });
    connect(m_usbObs, &UsbObsController::stopped, this, [this]() {
        if (m_usbObsAction) {
            m_usbObsAction->setText(tr("USB Clean Output (experimental)"));
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
                        m_usbObsAction->setText(tr("Cancel USB check"));
                    } else if (!(m_usbObs && m_usbObs->isRunning())) {
                        m_usbObsAction->setText(
                            tr("USB Clean Output (experimental)"));
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
                        tooltip += tr(
                            "\nUSB uses the iPad's only port. Unplug to "
                            "charge, or use a PD dock.");
                    }
                    m_usbStatusLabel->setToolTip(tooltip);
                }
                if (m_usbConnectBtn) {
                    m_usbConnectBtn->setText(tr("Reconnect USB device"));
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
                        warning = tr(
                                      "iPad is not charging (%1W adapter); "
                                      "USB uses its only port. Unplug to "
                                      "charge, or use a PD dock.")
                                      .arg(watts);
                    } else if (!charging && percent >= 0 && percent < 20) {
                        warning = tr("iPad battery is low (%1%).")
                                      .arg(percent);
                    }
                    if (!warning.isEmpty()) {
                        m_tray->showMessage(tr("uxplay-windows"), warning,
                                            QSystemTrayIcon::Warning, 10000);
                    } else {
                        m_tray->showMessage(
                            tr("uxplay-windows"),
                            tr("USB ready: %1").arg(name),
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
                    m_usbConnectBtn->setText(tr("Retry USB connection"));
                }
                if (m_tray) {
                    m_tray->showMessage(tr("uxplay-windows"), message,
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
                tr("uxplay-windows"),
                tr("Virtual camera is not implemented yet. Use Clean window for now."),
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
        m_usbConnectBtn->setText(tr("Connect USB device"));
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
        m_tray->showMessage(tr("uxplay-windows"),
                            tr("Rebuilding AirPlay stream..."),
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
    refreshPendingChanges();
    updateStatus();
}

void MainWindow::openLogFile() {
    applog::openLogFile();
}

void MainWindow::populateLogMenu(QMenu *menu) {
    QAction *open = menu->addAction(tr("Open Log"));
    connect(open, &QAction::triggered, this, &MainWindow::openLogFile);

    QAction *openWith = menu->addAction(tr("Open Log With..."));
    connect(openWith, &QAction::triggered, this,
            []() { applog::openLogFileWith(); });

    QAction *logFolder = menu->addAction(tr("Open Log Folder"));
    connect(logFolder, &QAction::triggered, this,
            []() { applog::openLogFolder(); });

    QAction *diagnosticsFolder =
        menu->addAction(tr("Open Diagnostics Folder"));
    connect(diagnosticsFolder, &QAction::triggered, this,
            []() { applog::openDiagnosticsFolder(); });

    QAction *viewer = menu->addAction(tr("Open Diagnostics Viewer"));
    connect(viewer, &QAction::triggered, this,
            &MainWindow::openDiagnosticsViewer);
}

void MainWindow::openDiagnosticsViewer() {
    QString script;
    const QStringList candidates = {
        QApplication::applicationDirPath() + QStringLiteral("/scripts/"),
        QApplication::applicationDirPath() + QStringLiteral("/../../scripts/"),
        QDir::currentPath() + QStringLiteral("/scripts/")};
    for (const QString &directory : candidates) {
        const QString candidate = directory + QStringLiteral("diagnostic_viewer.py");
        if (QFileInfo::exists(candidate)) {
            script = QFileInfo(candidate).absoluteFilePath();
            break;
        }
    }

    QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    }
    if (python.isEmpty()) {
        python = QStandardPaths::findExecutable(QStringLiteral("py"));
    }
    if (script.isEmpty() || python.isEmpty()) {
        QMessageBox::warning(
            this, tr("Diagnostics Viewer"),
            tr("The viewer could not start. Run this from the project folder:\n"
               "python scripts/diagnostic_viewer.py"));
        return;
    }

    QStringList arguments{script};
    const QString lastBundle =
        m_diagnostics ? m_diagnostics->lastBundlePath() : QString();
    if (!lastBundle.isEmpty() && QFileInfo::exists(lastBundle)) {
        arguments << lastBundle;
    }
    if (!QProcess::startDetached(python, arguments)) {
        QMessageBox::warning(this, tr("Diagnostics Viewer"),
                             tr("Could not launch the diagnostics viewer."));
    }
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

void MainWindow::saveSettingsSoon() {
    if (m_settingsSaveTimer && !m_settingsSaveTimer->isActive()) {
        m_settingsSaveTimer->start();
    }
}

void MainWindow::toggleHud(bool enabled) {
    m_settings.hudEnabled = enabled;
    m_settings.save();
    updateHud();
}

void MainWindow::toggleVideoAlwaysOnTop(bool enabled) {
    m_settings.videoWindow.alwaysOnTop = enabled;
    m_settings.save();
    if (m_videoWindowHandle) {
        HWND window = reinterpret_cast<HWND>(m_videoWindowHandle);
        if (IsWindow(window)) {
            SetWindowPos(window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
    }
}

void MainWindow::toggleHudField(const QString &field, bool enabled) {
    if (enabled) {
        if (!m_settings.hudFields.contains(field)) {
            m_settings.hudFields.append(field);
        }
    } else {
        m_settings.hudFields.removeAll(field);
    }
    m_settings.save();
    for (QAction *action : m_hudFieldActions) {
        if (action && action->data().toString() == field
            && action->isChecked() != enabled) {
            const bool blocked = action->blockSignals(true);
            action->setChecked(enabled);
            action->blockSignals(blocked);
        }
    }
    updateHud();
}

void MainWindow::showAllHudFields() {
    m_settings.hudFields = {
        QStringLiteral("client"), QStringLiteral("status"),
        QStringLiteral("video"), QStringLiteral("output"),
        QStringLiteral("decoder"), QStringLiteral("renderer"),
        QStringLiteral("fps"), QStringLiteral("audio")};
    m_settings.save();
    for (QAction *action : m_hudFieldActions) {
        if (!action) {
            continue;
        }
        const bool blocked = action->blockSignals(true);
        action->setChecked(true);
        action->blockSignals(blocked);
    }
    updateHud();
}

void MainWindow::buildHudFieldsMenu(QMenu *menu) {
    if (!menu) {
        return;
    }
    const QList<QPair<QString, QString>> fields = {
        {QStringLiteral("client"), tr("Client")},
        {QStringLiteral("status"), tr("Status")},
        {QStringLiteral("video"), tr("Video codec")},
        {QStringLiteral("output"), tr("Output size")},
        {QStringLiteral("decoder"), tr("Decoder")},
        {QStringLiteral("renderer"), tr("Renderer")},
        {QStringLiteral("fps"), tr("FPS")},
        {QStringLiteral("audio"), tr("Audio")}};
    for (const auto &entry : fields) {
        QAction *action = menu->addAction(entry.second);
        action->setCheckable(true);
        action->setChecked(m_settings.hudFields.contains(entry.first));
        action->setData(entry.first);
        m_hudFieldActions.append(action);
        connect(action, &QAction::toggled, this,
                [this, field = entry.first](bool enabled) {
                    toggleHudField(field, enabled);
                });
    }
    menu->addSeparator();
    QAction *all = menu->addAction(tr("Show all"));
    connect(all, &QAction::triggered, this, &MainWindow::showAllHudFields);
}

void MainWindow::setLanguage(const QString &language) {
    QString normalized = language;
    if (normalized != QStringLiteral("en")
        && normalized != QStringLiteral("zh_TW")) {
        normalized = QStringLiteral("system");
    }
    if (m_settings.language != normalized) {
        m_settings.language = normalized;
        m_settings.save();
    }
    updateStatus();
    if (m_tray) {
        m_tray->showMessage(tr("uxplay-windows"),
                            tr("Restart uxplay-windows to apply the language change."),
                            QSystemTrayIcon::Information, 4000);
    }
}

void MainWindow::setHudCorner(const QString &corner) {
    if (corner != QStringLiteral("top-left")
        && corner != QStringLiteral("top-right")
        && corner != QStringLiteral("bottom-left")
        && corner != QStringLiteral("bottom-right")) {
        return;
    }
    m_settings.hudCorner = corner;
    m_settings.save();
    updateHud();
}

void MainWindow::updateFrameStats() {
    if (!m_running) {
        return;
    }

    uint64_t frames = 0;
    double ageMs = -1.0;
    uint64_t inputFrames = 0;
    double inputAgeMs = -1.0;
    get_uxplay_frame_stats(&frames, &ageMs);
    get_uxplay_video_input_stats(&inputFrames, &inputAgeMs);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool inputAdvanced =
        m_lastInputFrameCount > 0 && inputFrames > m_lastInputFrameCount;
    if (m_lastStatsMs > 0 && now > m_lastStatsMs
        && frames >= m_lastFrameCount) {
        const double seconds = (now - m_lastStatsMs) / 1000.0;
        const double delta = static_cast<double>(frames - m_lastFrameCount);
        m_fps = seconds > 0.0 ? delta / seconds : 0.0;
        m_frameIntervalMs = m_fps > 0.0 ? 1000.0 / m_fps : 0.0;
    }
    m_frameAgeMs = ageMs;
    m_inputFrameAgeMs = inputAgeMs;
    m_lastFrameCount = frames;
    m_lastStatsMs = now;

    // A stalled decoder/sink still accepts RTP input, so input activity plus a
    // frozen presented frame is the signature of a pipeline stall. Do not use
    // frame age alone: pause/lock-screen also stops frame presentation.
    const bool pipelineStalled =
        m_streaming && frames > 1 && m_frameAgeMs > 5000.0
        && inputAdvanced && m_inputFrameAgeMs >= 0.0
        && m_inputFrameAgeMs < 1500.0;
    if (pipelineStalled) {
        ++m_stalledFrameSamples;
    } else {
        m_stalledFrameSamples = 0;
    }
    if (m_stalledFrameSamples >= 2
        && now - m_lastStallRebuildMs >= 15000
        && m_stallRebuildCount < 3) {
        qWarning().noquote()
            << QStringLiteral("[stats] pipeline_stall fps=%1 frame_age_ms=%2 "
                              "input_age_ms=%3 frames=%4 input_frames=%5")
                   .arg(m_fps, 0, 'f', 1)
                   .arg(m_frameAgeMs, 0, 'f', 0)
                   .arg(m_inputFrameAgeMs, 0, 'f', 0)
                   .arg(frames)
                   .arg(inputFrames);
        if (m_diagnostics) {
            m_diagnostics->event(
                QStringLiteral("video"), QStringLiteral("pipeline_stall"),
                QJsonObject{{QStringLiteral("fps"), m_fps},
                            {QStringLiteral("frame_age_ms"), m_frameAgeMs},
                            {QStringLiteral("input_age_ms"), m_inputFrameAgeMs},
                            {QStringLiteral("attempt"),
                             m_stallRebuildCount + 1}});
        }
        ++m_stallRebuildCount;
        m_stalledFrameSamples = 0;
        m_lastStallRebuildMs = now;
        rebuildAirplayStream();
        m_lastInputFrameCount = inputFrames;
        return;
    }
    m_lastInputFrameCount = inputFrames;

    if (m_fps > 0.0 && m_fps < 30.0) {
        m_lowFpsSamples++;
        if (m_lowFpsSamples == 5) {
            qWarning().noquote()
                << QStringLiteral("[stats] low_fps fps=%1 interval_ms=%2 "
                                  "frame_age_ms=%3 duration_s=5")
                       .arg(m_fps, 0, 'f', 1)
                       .arg(m_frameIntervalMs, 0, 'f', 1)
                       .arg(m_frameAgeMs, 0, 'f', 0);
            if (m_diagnostics) {
                m_diagnostics->event(
                    QStringLiteral("video"), QStringLiteral("low_fps"),
                    QJsonObject{{QStringLiteral("fps"), m_fps},
                                {QStringLiteral("interval_ms"),
                                 m_frameIntervalMs},
                                {QStringLiteral("frame_age_ms"),
                                 m_frameAgeMs}});
            }
        }
    } else {
        m_lowFpsSamples = 0;
    }
    if (!m_clientName.isEmpty() && now - m_lastStatsLogMs >= 5000) {
        qInfo().noquote()
            << QStringLiteral("[stats] fps=%1 interval_ms=%2 frame_age_ms=%3 "
                              "frames=%4")
                   .arg(m_fps, 0, 'f', 1)
                   .arg(m_frameIntervalMs, 0, 'f', 1)
                   .arg(m_frameAgeMs, 0, 'f', 0)
                   .arg(frames);
        m_lastStatsLogMs = now;
    }

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
    if (m_hudBottomLeftAction) {
        m_hudBottomLeftAction->setChecked(
            m_settings.hudCorner == QStringLiteral("bottom-left"));
    }
    if (m_hudBottomRightAction) {
        m_hudBottomRightAction->setChecked(
            m_settings.hudCorner == QStringLiteral("bottom-right"));
    }

    if (!m_settings.hudEnabled || !m_running || m_clientName.isEmpty()) {
        set_uxplay_hud_text("", 0, 0, 0, 0);
        return;
    }

    QStringList lines;
    auto actionText = [](QAction *action) {
        return action ? action->text() : QString();
    };
    auto withoutPrefix = [](const QString &text, const QString &prefix) {
        QString value = text;
        if (value.startsWith(prefix)) {
            value.remove(0, prefix.size());
        }
        const int asciiSeparator = value.indexOf(QStringLiteral(": "));
        if (asciiSeparator >= 0) {
            value = value.mid(asciiSeparator + 2);
        } else {
            const int wideSeparator = value.indexOf(QStringLiteral("："));
            if (wideSeparator >= 0) {
                value = value.mid(wideSeparator + 1);
            }
        }
        return value;
    };
    auto escapeMarkup = [](QString value) {
        value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
        value.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
        value.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        return value;
    };
    auto line = [&escapeMarkup](const QString &label, const QString &value,
                                const QString &color) {
        return QStringLiteral("<span foreground=\"#9FB3C8\">%1:</span> "
                              "<span foreground=\"%2\">%3</span>")
            .arg(label, color, escapeMarkup(value));
    };
    auto hasField = [this](const char *key) {
        return m_settings.hudFields.contains(QString::fromLatin1(key));
    };

    if (hasField("client") && m_clientAction) {
        lines << line(tr("Client"),
                      withoutPrefix(actionText(m_clientAction),
                                    tr("Client: ")),
                      QStringLiteral("#FFFFFF"));
    }
    if (hasField("status") && m_mediaStatusAction) {
        const QString status = withoutPrefix(
            actionText(m_mediaStatusAction), tr("Status: "));
        const QString color = m_streaming
                                  ? QStringLiteral("#7EE787")
                                  : m_clientName.isEmpty()
                                        ? QStringLiteral("#FFFFFF")
                                        : QStringLiteral("#FFD166");
        lines << line(tr("Status"), status, color);
    }
    if (hasField("video") && m_videoCodecAction) {
        lines << line(tr("Video"),
                      withoutPrefix(actionText(m_videoCodecAction),
                                    tr("Video Codec: ")),
                      QStringLiteral("#FFFFFF"));
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
            : tr("waiting");
    if (hasField("output")) {
        lines << line(tr("Output"),
                      tr("%1  (requested %2)").arg(actual, requested),
                      QStringLiteral("#FFFFFF"));
    }

    if (hasField("decoder") && m_videoDecoderAction) {
        lines << line(tr("Decoder"),
                      withoutPrefix(actionText(m_videoDecoderAction),
                                    tr("Video Decoder: ")),
                      QStringLiteral("#FFFFFF"));
    }
    if (hasField("renderer") && m_videoRendererAction) {
        lines << line(tr("Renderer"),
                      withoutPrefix(actionText(m_videoRendererAction),
                                    tr("Video Renderer: ")),
                      QStringLiteral("#FFFFFF"));
    }
    if (hasField("fps") && m_lastStatsMs > 0 && m_frameAgeMs >= 0.0) {
        const QString interval =
            m_fps > 0.0
                ? QStringLiteral("%1 ms").arg(m_frameIntervalMs, 0, 'f', 1)
                : QStringLiteral("--");
        const QString fpsText = tr("%1 (%2, age %3 ms)")
                                    .arg(m_fps, 0, 'f', 1)
                                    .arg(interval)
                                    .arg(m_frameAgeMs, 0, 'f', 0);
        const QString color = m_frameAgeMs > 1500.0
                                  ? QStringLiteral("#FF7B72")
                                  : m_fps < 30.0
                                        ? QStringLiteral("#FFB86B")
                                        : QStringLiteral("#7EE787");
        lines << line(tr("FPS"), fpsText, color);
    }
    if (hasField("audio") && m_audioCodecAction) {
        QString audio = withoutPrefix(actionText(m_audioCodecAction),
                                      tr("Audio Codec: "));
        if (m_audioFormatAction) {
            static const QRegularExpression formatPattern(
                QStringLiteral("ct=(\\d+)\\s+spf=(\\d+)"));
            const QRegularExpressionMatch match =
                formatPattern.match(actionText(m_audioFormatAction));
            if (match.hasMatch()) {
                audio += QStringLiteral(" · ct=%1 spf=%2")
                             .arg(match.captured(1), match.captured(2));
            }
        }
        lines << line(tr("Audio"), audio,
                      QStringLiteral("#FFFFFF"));
    }

    const QByteArray text = lines.join(QLatin1Char('\n')).toUtf8();
    const bool right = m_settings.hudCorner.endsWith(QStringLiteral("right"));
    const bool bottom = m_settings.hudCorner.startsWith(QStringLiteral("bottom"));
    set_uxplay_hud_text(text.constData(), right, bottom,
                        m_settings.hudMarginX, m_settings.hudMarginY);
}

void MainWindow::updateOutputLabel() {    if (!m_outputLabel) {
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
            : tr("waiting");
    m_outputLabel->setText(
        tr("Output: %1   (requested %2)").arg(actual, requested));
}

void MainWindow::saveDiagnostics(bool showDialog) {
    if (!m_diagnostics) {
        return;
    }

    m_diagnostics->setSettings(m_settings);
    m_diagnostics->event(QStringLiteral("diagnostics"),
                         QStringLiteral("saving"));

    DiagnosticsExportRange exportRange;
    bool createZip = false;
    if (showDialog) {
        DiagnosticsExportDialog dialog(m_diagnostics->lastClientConnection(),
                                       m_diagnostics->sessionStartedAt(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        exportRange = dialog.range();
        createZip = dialog.createZip();
    } else {
        const QDateTime now = QDateTime::currentDateTime();
        const QDateTime last = m_diagnostics->lastClientConnection();
        exportRange.from = last.isValid() ? last : now.addSecs(-3600);
        exportRange.to = now;
        exportRange.label = last.isValid()
                                ? tr("Since last AirPlay connection")
                                : tr("Last 1 hour");
    }

    QString error;
    const QString path =
        m_diagnostics->exportBundle(exportRange, createZip, &error);
    if (path.isEmpty()) {
        const QString message =
            error.isEmpty() ? tr("Could not save diagnostics.")
                            : error;
        qCritical() << "Diagnostics export failed:" << message;
        if (showDialog) {
            QMessageBox::warning(this, tr("Diagnostics"), message);
        } else if (m_tray) {
            m_tray->showMessage(tr("uxplay-windows"), message,
                                QSystemTrayIcon::Warning, 6000);
        }
        return;
    }

    qInfo() << "Diagnostics bundle saved to" << path;
    if (!showDialog) {
        if (m_tray) {
            m_tray->showMessage(tr("Diagnostics saved"), path,
                                QSystemTrayIcon::Information, 5000);
        }
        return;
    }

    QWidget *dialogParent = isVisible() ? this : nullptr;
    QMessageBox box(dialogParent);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Diagnostics saved"));
    box.setText(tr("Recent diagnostics were saved."));
    box.setInformativeText(path);
    box.setDetailedText(
        tr("The bundle is generated locally and redacted, but review it before "
           "sharing. It contains logs, settings, events, metrics and system "
           "information."));
    QPushButton *openFolder =
        box.addButton(tr("Open folder"), QMessageBox::AcceptRole);
    QPushButton *copyPath =
        box.addButton(tr("Copy path"), QMessageBox::ActionRole);
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
        m_tray->showMessage(tr("uxplay-windows"),
                            tr("Windows woke from sleep. Restarting to refresh AirPlay discovery."),
                            QSystemTrayIcon::Information, 3000);
    }

    restartApplication();
}


void MainWindow::updateStatus() {
    QString status;
    if (!m_serverEnabled) {
        m_appState = AppState::Disabled;
        status = tr("AirPlay stopped");
    } else if (m_appState == AppState::Error) {
        status = tr("Engine failing - see log");
    } else if (!m_running) {
        m_appState = AppState::Starting;
        status = tr("UxPlay server starting...");
    } else if (!m_clientName.isEmpty()) {
        m_appState = AppState::Streaming;
        status = m_streaming ? tr("Streaming: %1").arg(m_clientName)
                             : tr("Connected: %1").arg(m_clientName);
    } else {
        m_appState = AppState::Waiting;
        status = tr("UxPlay server running - waiting for connection");
    }

    if (m_diagnostics) {
        m_diagnostics->setStatus(status);
    }
    if (m_statusLabel) m_statusLabel->setText(status);
    if (m_autostartBtn) {
        m_autostartBtn->setText(isAutostartEnabled()
                                    ? tr("Open uxplay-windows on login: ON")
                                    : tr("Open uxplay-windows on login: OFF"));
    }
    if (m_toggleServerAction) {
        m_toggleServerAction->setText(m_serverEnabled ? tr("Stop AirPlay")
                                                      : tr("Start AirPlay"));
    }
    if (m_languageSystemAction) {
        m_languageSystemAction->setChecked(
            m_settings.language == QStringLiteral("system"));
        m_languageEnglishAction->setChecked(
            m_settings.language == QStringLiteral("en"));
        m_languageChineseAction->setChecked(
            m_settings.language == QStringLiteral("zh_TW"));
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
            this, tr("Bonjour Service Not Running"),
            tr("Bonjour Service is installed but could not be started.\n\n"
               "Open Services (services.msc), start 'Bonjour Service', "
               "then relaunch uxplay-windows."));
        return false;
    }

    if (state == BonjourState::Missing) {
        const int choice = QMessageBox::question(
            this, tr("Bonjour Service Required"),
            tr("Bonjour Service is required for discovery (mDNS). It is not "
               "installed.\n\nDo you want to install it now?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (choice != QMessageBox::Yes) {
            QMessageBox::critical(
                this, tr("Bonjour Service Missing"),
                tr("Bonjour Service is required. The application will now exit."));
            return false;
        }

        QMessageBox::information(
            this, tr("Installing Bonjour Service"),
            tr("Starting installation of 'Bonjour Service'. Follow any UAC prompt."));

        const int rc = mdns::MdnsResponder::install();
        if (rc != 0) {
            QMessageBox::critical(
                this, tr("Installation Failed"),
                tr("Failed to install 'Bonjour Service'.\n\n"
                   "The application will now exit."));
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
            this, tr("Installation Failed"),
            tr("Bonjour Service was installed but could not be started.\n\n"
               "Open Services (services.msc), start 'Bonjour Service', "
               "then relaunch uxplay-windows."));
        return false;
    }

    // Access denied or another service-manager error. Discovery may still work,
    // so continue and leave the details in the log instead of exiting.
    qWarning() << "Bonjour Service state is unknown; continuing in best-effort mode";
    QMessageBox::warning(
        this, tr("Bonjour Service Status Unknown"),
        tr("Could not query Bonjour Service status (access denied or service "
           "manager unavailable).\n\nDiscovery may still work. Check the log for "
           "details."));
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
            this, tr("Restart Failed"),
            tr("Could not restart uxplay-windows.\n\nPlease close and open it manually."));
        if (m_serverEnabled) startServer();
        updateStatus();
        return;
    }

    // close GUI of current process after a short delay
    QTimer::singleShot(200, qApp, &QCoreApplication::quit);
}
