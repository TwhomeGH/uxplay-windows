#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include "settings.h"

class QTimer;

/// Keeps a bounded in-memory record of recent events and process metrics so a
/// user can export a diagnostic bundle after an anomaly without reproducing it.
class DiagnosticsRecorder : public QObject {
    Q_OBJECT

public:
    explicit DiagnosticsRecorder(QObject *parent = nullptr);

    void setSettings(const AppSettings &settings);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setStatus(const QString &status);
    void event(const QString &category, const QString &message,
               const QJsonObject &data = QJsonObject());
    QString exportBundle(QString *error = nullptr);
    QString lastBundlePath() const { return m_lastBundlePath; }

private slots:
    void sample();

private:
    struct EventRecord {
        QDateTime time;
        QString category;
        QString message;
        QJsonObject data;
    };

    struct MetricRecord {
        QDateTime time;
        double cpuPercent;
        qint64 workingSet;
        qint64 privateBytes;
        int handles;
        QString status;
    };

    AppSettings m_settings;
    bool m_enabled = false;
    QString m_status;
    QString m_lastBundlePath;
    QVector<EventRecord> m_events;
    QVector<MetricRecord> m_metrics;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastCpu100ns = -1;
    qint64 m_lastSampleMs = 0;
    QDateTime m_startedAt;
};
