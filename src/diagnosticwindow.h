#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QTabWidget;

/// Values shown by the standalone diagnostics window. The MainWindow updates
/// this once per second; the window itself never queries the engine.
struct DiagnosticsSnapshot {
    QString timestamp;
    QString status;
    QString clientName;
    QString clientModel;
    QString clientDeviceId;
    int connectionCount = 0;

    QString videoCodec;
    QString outputSize;
    QString requestedSize;
    QString decoder;
    QString renderer;
    double fps = 0.0;
    double frameIntervalMs = 0.0;
    double frameAgeMs = -1.0;
    quint64 frames = 0;
    quint64 inputFrames = 0;
    double inputAgeMs = -1.0;

    QString audioCodec;
    QString audioFormat;
    QString audioLatency;
    quint64 audioPacketsReceived = 0;
    quint64 audioPacketsDecoded = 0;
    quint64 audioResendRequests = 0;
    quint64 audioEmptyResends = 0;
    quint64 audioBytes = 0;

    QString engineArgs;
    QString appVersion;
    QString sessionUptime;
    QString settingsPath;
    QString logPath;
    QString diagnosticsPath;
    QString lastError;
    QString obsMode;
    QString usbStatus;
    QStringList recentEvents;
};

/// Resizable, non-modal diagnostics window. Closing it only hides the window;
/// it never stops AirPlay or the engine.
class DiagnosticsWindow : public QWidget {
    Q_OBJECT

public:
    explicit DiagnosticsWindow(QWidget *parent = nullptr);

    void setSnapshot(const DiagnosticsSnapshot &snapshot);
    void showAndRaise();

signals:
    void saveDiagnosticsRequested();

private:
    void updateTexts();
    QString summaryText() const;
    QString allText() const;

    QTabWidget *m_tabs = nullptr;
    QPlainTextEdit *m_overview = nullptr;
    QPlainTextEdit *m_video = nullptr;
    QPlainTextEdit *m_audio = nullptr;
    QPlainTextEdit *m_network = nullptr;
    QPlainTextEdit *m_events = nullptr;
    QPushButton *m_copySummary = nullptr;
    QPushButton *m_copyAll = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_close = nullptr;
    DiagnosticsSnapshot m_snapshot;
};
