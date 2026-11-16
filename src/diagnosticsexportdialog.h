#pragma once

#include <QDialog>
#include <QDateTime>

#include "diagnostics.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDialogButtonBox;
class QLabel;

/// Lets the user choose the incident window for a diagnostics export.
///
/// The dialog intentionally has no "everything forever" default: the useful
/// unit for support is one AirPlay session or a bounded recent window.
class DiagnosticsExportDialog : public QDialog {
    Q_OBJECT

public:
    DiagnosticsExportDialog(const QDateTime &lastConnection,
                            const QDateTime &sessionStart,
                            QWidget *parent = nullptr);

    DiagnosticsExportRange range() const;
    bool createZip() const;

private slots:
    void updateControls();

private:
    enum Mode {
        SinceLastConnection = 0,
        LastHour,
        LastFiveHours,
        EntireSession,
        Custom,
    };

    QDateTime startForMode() const;
    QDateTime endForMode() const;

    QComboBox *m_mode = nullptr;
    QDateTimeEdit *m_from = nullptr;
    QDateTimeEdit *m_to = nullptr;
    QLabel *m_hint = nullptr;
    QCheckBox *m_zip = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QDateTime m_lastConnection;
    QDateTime m_sessionStart;
};
