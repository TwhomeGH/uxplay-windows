#pragma once

#include <string>

#include <QLabel>
#include <QMainWindow>
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
class AirPlayWorker;
class UsbObsController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void setServerEnabled(bool enabled);
    void startUsbObs(bool minimize = false);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleAutostart();
    void showLicense();
    void openSettingsFile();
    void openListArgsFile();
    void quit();
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
    void openLogFile();
    void toggleBle(bool checked); // bluetooth
    void toggleForceFullscreen(bool checked);
    void onRendererChanged(int index);
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
    QComboBox *m_resolutionCombo = nullptr;
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
    QAction *m_openLogAction = nullptr;
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
    QPushButton *m_openLogBtn = nullptr;
    QPushButton *m_usbObsBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QTimer *m_wakeRestartTimer = nullptr;

    AirPlayWorker *m_worker = nullptr;
    UsbObsController *m_usbObs = nullptr;

    QString m_clientName;
    QString m_clientModel;
    QString m_clientDeviceId;
    int m_connectionCount = 0;

    bool m_running = false;
    bool m_quitting = false;
    bool m_serverEnabled = true;
    bool m_streaming = false;
    int m_restartAttempt = 0;
    QString m_lastEngineError;
    QTimer *m_videoTitleTimer = nullptr;
    enum class AppState { Disabled, Starting, Waiting, Streaming, Error };
    AppState m_appState = AppState::Starting;
};
