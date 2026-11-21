#pragma once

#include <string>

#include <QLabel>
#include <QMainWindow>
#include <QDateTime>
#include <QRect>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QProcess>
#include <QCheckBox>
#include <QMessageBox>
#include <QTimer>

#include "settings.h"

class QMenu;
class QAction;
class QToolButton;
class AirPlayWorker;
class UsbObsController;
class UsbDeviceConnector;
class DiagnosticsRecorder;
class DiagnosticsWindow;
class HudWindow;
class HudOverlay;
class RuntimeDebugWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void setServerEnabled(bool enabled);
    void showAndRaise();
    void showDiagnosticsWindow();
    void startUsbObs(bool minimize = false);
    void saveDiagnostics(bool showDialog = true);
    void quit();
    void showRuntimeDebugWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleAutostart();
    void showLicense();
    void openSettingsFile();
    void openListArgsFile();
    void onAirplayStarted();
    void onAirplayStopped();
    void onAirplayError(const QString &message);
    void onClientConnected(const QString &name, const QString &model,
                           const QString &deviceid);
    void onClientDisconnected();
    void onClientRejected(const QString &name, const QString &model,
                          const QString &deviceid);
    void onConnectionCountChanged(int count);
    void toggleServer();
    void toggleUsbObs();
    void onObsModeChanged(int index);
    void onConnectUsbClicked();
    void openLogFile();
    void toggleDiagnostics(bool enabled);
    void toggleHud(bool enabled);
    void toggleHudField(const QString &field, bool enabled);
    void showAllHudFields();
    void toggleVideoAlwaysOnTop(bool enabled);
    void toggleBle(bool checked); // bluetooth
    void toggleForceFullscreen(bool checked);
    void toggleVirtualCamera(bool checked);
    void onRendererChanged(int index);
    void onVideoCodecChanged(int index);
    void onVideoPacingChanged(int index);
    void onAudioPacingChanged(int index);
    void updateVideoCodecHint();
    void onResolutionChanged();
    void updateMediaInfo(const QString &key, const QString &value);
    void applyDisplayName();
    void restartApplicationAfterWake();


private:
    void startServer();
    void stopServer();
    void setupTray();
    void setupUI();
    void updateStatus();
    void updateTrayIcon();
    void clearClientInfo();
    void startBluetoothBeacon(const QString &path);
    void stopBluetoothBeacon();
    void scheduleWakeRestart();
    void updateResolutionControls();
    void resetMediaInfo(const QString &status);
    void ensureUsbObsController();
    void ensureUsbConnector();
    void setObsMode(const QString &mode, bool persist);
    void syncVideoWindow();
    void applyVideoWindowGeometry(const QRect &geometry);
    void fitVideoWindowToStream(int width, int height);
    void saveVideoWindowGeometry();
    void updateHud();
    /// 在 UI 執行緒切換 HUD 顯示模式並保存設定，不重新啟動引擎。
    /// @param mode window 為獨立視窗，overlay 為舊原生疊層。
    void setHudMode(const QString &mode);
    /// 將相同 HUD 內容交給目前模式；空字串隱藏並解除疊層附著。
    /// @param text 目前 HUD 的文字／標記內容。
    void presentHud(const QString &text);
    void updateRuntimeDebugWindow();
    void updateFrameStats();
    void updateOutputLabel();
    void setHudCorner(const QString &corner);
    void setLanguage(const QString &language);
    void rebuildAirplayStream();
    void openDiagnosticsViewer();
    void saveSettingsSoon();
    void markEngineSettingsChanged(const QString &reason);
    void refreshPendingChanges();
    void applyPendingEngineChanges();
    void updateDiagnosticsWindow();
    void raiseVideoWindow();
    QString currentEngineArguments() const;
    void buildHudFieldsMenu(QMenu *menu);
    void populateLogMenu(QMenu *menu);

    QProcess *m_beacon = nullptr;

    AppSettings m_settings;
    enum class BonjourState { Missing, Stopped, Running, Unknown };
    BonjourState queryBonjourState() const;
    bool startBonjourService();
    bool ensureBonjourServiceReady();
    void restartApplication();

    bool isAutostartEnabled() const;
    void setAutostart(bool enabled);

    QCheckBox *m_bleCheckbox = nullptr;
    QCheckBox *m_fullscreenCheckbox = nullptr;
    QLineEdit *m_displayNameEdit = nullptr;
    
    QComboBox *m_rendererCombo = nullptr;
    QComboBox *m_videoCodecCombo = nullptr;
    QComboBox *m_videoPacingCombo = nullptr;
    QComboBox *m_audioPacingCombo = nullptr;
    QLabel *m_videoCodecHint = nullptr;
    QComboBox *m_resolutionCombo = nullptr;
    QComboBox *m_obsModeCombo = nullptr;
    QLabel *m_obsHint = nullptr;
    QCheckBox *m_virtualCameraCheckbox = nullptr;
    QLabel *m_usbStatusLabel = nullptr;
    QPushButton *m_usbConnectBtn = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
    QSpinBox *m_refreshSpin = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_mediaInfoMenu = nullptr;
    QAction *m_autostartAction = nullptr;
    QAction *m_statusAction = nullptr;
    QAction *m_toggleServerAction = nullptr;
    QAction *m_usbObsAction = nullptr;
    QAction *m_diagnosticsRecordAction = nullptr;
    QAction *m_saveDiagnosticsAction = nullptr;
    QAction *m_hudAction = nullptr;
    QAction *m_videoOnTopAction = nullptr;
    QAction *m_languageSystemAction = nullptr;
    QAction *m_languageEnglishAction = nullptr;
    QAction *m_languageChineseAction = nullptr;
    QAction *m_hudTopLeftAction = nullptr;
    QAction *m_hudTopRightAction = nullptr;
    QAction *m_hudBottomLeftAction = nullptr;
    QAction *m_hudBottomRightAction = nullptr;
    QAction *m_clientAction = nullptr;
    QAction *m_connectionsAction = nullptr;
    QAction *m_mediaStatusAction = nullptr;
    QAction *m_videoCodecAction = nullptr;
    QAction *m_videoDecoderAction = nullptr;
    QAction *m_videoRendererAction = nullptr;
    QAction *m_streamSizeAction = nullptr;
    QAction *m_audioCodecAction = nullptr;
    QAction *m_audioFormatAction = nullptr;

    QPushButton *m_autostartBtn = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QPushButton *m_listargsBtn = nullptr;
    QPushButton *m_licenseBtn = nullptr;
    QToolButton *m_openLogBtn = nullptr;
    QPushButton *m_diagnosticsBtn = nullptr;
    QCheckBox *m_diagnosticsCheckbox = nullptr;

    QPushButton *m_rebuildButton = nullptr;
    QCheckBox *m_hudCheckbox = nullptr;
    QComboBox *m_hudModeCombo = nullptr;
    QSpinBox *m_hudXSpin = nullptr;
    QSpinBox *m_hudYSpin = nullptr;
    QPushButton *m_hudFieldsButton = nullptr;
    QMenu *m_hudFieldsMenu = nullptr;
    QList<QAction *> m_hudFieldActions;
    QLabel *m_outputLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QWidget *m_pendingChangesRow = nullptr;
    QLabel *m_pendingChangesLabel = nullptr;
    QPushButton *m_applyChangesButton = nullptr;
    QTimer *m_wakeRestartTimer = nullptr;

    AirPlayWorker *m_worker = nullptr;
    UsbObsController *m_usbObs = nullptr;
    UsbDeviceConnector *m_usbConnector = nullptr;
    DiagnosticsRecorder *m_diagnostics = nullptr;
    DiagnosticsWindow *m_diagnosticsWindow = nullptr;
    HudWindow *m_hudWindow = nullptr;
    HudOverlay *m_hudOverlay = nullptr;
    RuntimeDebugWindow *m_runtimeDebugWindow = nullptr;
    QString m_engineArgsText;
    QDateTime m_sessionStartedAt;
    bool m_usbDeviceReady = false;

    QString m_clientName;
    QString m_clientModel;
    QString m_clientDeviceId;
    int m_connectionCount = 0;

    bool m_running = false;
    bool m_quitting = false;
    bool m_serverEnabled = true;
    bool m_streaming = false;
    bool m_engineChangesPending = false;
    QString m_appliedEngineArguments;
    QString m_appliedVideoPacing;
    QString m_appliedAudioPacing;
    int m_appliedWidth = -1;
    int m_appliedHeight = -1;
    bool m_inputStallActive = false;
    bool m_videoWindowMinimizedNotified = false;
    int m_restartAttempt = 0;
    QString m_lastEngineError;
    QTimer *m_videoTitleTimer = nullptr;
    QTimer *m_statsTimer = nullptr;
    QTimer *m_settingsSaveTimer = nullptr;
    quint64 m_lastFrameCount = 0;
    quint64 m_lastInputFrameCount = 0;
    quint64 m_audioPacketsReceived = 0;
    quint64 m_audioPacketsDecoded = 0;
    quint64 m_audioResendRequests = 0;
    quint64 m_audioEmptyResends = 0;
    quint64 m_audioBytes = 0;
    quint64 m_audioRecoveries = 0;
    quint64 m_audioAgeMs = 0;
    quint64 m_audioLastGapMs = 0;
    quint64 m_audioPipelineRestarts = 0;
    double m_audioOutputPeak = -1.0;
    quint64 m_audioOutputAgeMs = 0;
    quint64 m_audioPushedFrames = 0;
    quint64 m_lastAudioGapSkips = 0;
    quint64 m_lastAudioPacketsReceived = 0;
    quint64 m_lastAudioPacketsDecoded = 0;
    quint64 m_lastAudioResendRequests = 0;
    quint64 m_lastAudioEmptyResends = 0;
    qint64 m_lastAudioStatsLogMs = 0;
    qint64 m_lastStatsMs = 0;
    qint64 m_lastStallRebuildMs = 0;
    int m_stalledFrameSamples = 0;
    int m_stallRebuildCount = 0;
    qint64 m_streamingStartedMs = 0;
    double m_inputFrameAgeMs = -1.0;
    qint64 m_lastFrameRecoveryMs = 0;
    double m_fps = 0.0;
    double m_frameIntervalMs = 0.0;
    double m_frameAgeMs = -1.0;
    int m_lowFpsSamples = 0;
    qint64 m_lastStatsLogMs = 0;
    qint64 m_lastGeometrySaveMs = 0;
    quintptr m_videoWindowHandle = 0;
    QRect m_lastVideoWindowRect;
    bool m_videoWindowApplied = false;
    int m_streamWidth = 0;
    int m_streamHeight = 0;
    enum class AppState { Disabled, Starting, Waiting, Streaming, Error };
    AppState m_appState = AppState::Starting;
};
