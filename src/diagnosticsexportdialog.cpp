#include "diagnosticsexportdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

DiagnosticsExportDialog::DiagnosticsExportDialog(
    const QDateTime &lastConnection, const QDateTime &sessionStart,
    QWidget *parent)
    : QDialog(parent), m_lastConnection(lastConnection),
      m_sessionStart(sessionStart) {
    setWindowTitle(tr("Save Diagnostics"));
    setMinimumWidth(520);

    auto *intro = new QLabel(
        tr("Export one incident window, not the entire history.\n"
           "Data is written locally and is never uploaded. A folder is the "
           "default; ZIP is only for sharing."),
        this);
    intro->setWordWrap(true);

    m_mode = new QComboBox(this);
    m_mode->addItem(tr("Since last AirPlay connection"));
    m_mode->addItem(tr("Last 1 hour"));
    m_mode->addItem(tr("Last 5 hours"));
    m_mode->addItem(tr("Entire app session"));
    m_mode->addItem(tr("Custom range..."));
    if (!m_lastConnection.isValid()) {
        m_mode->setCurrentIndex(LastHour);
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime earliest =
        m_sessionStart.isValid() && m_sessionStart < now ? m_sessionStart
                                                         : now.addSecs(-3600);
    m_from = new QDateTimeEdit(earliest, this);
    m_from->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_from->setCalendarPopup(true);
    m_from->setDateTimeRange(earliest, now);
    m_to = new QDateTimeEdit(now, this);
    m_to->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_to->setCalendarPopup(true);
    m_to->setDateTimeRange(earliest, now);

    m_zip = new QCheckBox(tr("Create .zip for sharing (optional)"), this);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #666;"));

    auto *form = new QFormLayout();
    form->addRow(tr("Range"), m_mode);
    form->addRow(tr("From"), m_from);
    form->addRow(tr("To"), m_to);
    form->addRow(QString(), m_zip);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Save)->setText(tr("Save Diagnostics"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(m_hint);
    layout->addWidget(m_buttons);

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagnosticsExportDialog::updateControls);
    connect(m_from, &QDateTimeEdit::dateTimeChanged, this,
            &DiagnosticsExportDialog::updateControls);
    connect(m_to, &QDateTimeEdit::dateTimeChanged, this,
            &DiagnosticsExportDialog::updateControls);
    connect(m_buttons, &QDialogButtonBox::accepted, this,
            [this]() {
                if (range().isValid()) {
                    accept();
                }
            });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateControls();
}

QDateTime DiagnosticsExportDialog::startForMode() const {
    const QDateTime now = QDateTime::currentDateTime();
    switch (m_mode->currentIndex()) {
    case SinceLastConnection:
        return m_lastConnection.isValid() ? m_lastConnection
                                          : now.addSecs(-3600);
    case LastHour:
        return now.addSecs(-3600);
    case LastFiveHours:
        return now.addSecs(-5 * 3600);
    case EntireSession:
        return m_sessionStart.isValid() ? m_sessionStart : now.addSecs(-3600);
    case Custom:
    default:
        return m_from->dateTime();
    }
}

QDateTime DiagnosticsExportDialog::endForMode() const {
    return m_mode->currentIndex() == Custom ? m_to->dateTime()
                                            : QDateTime::currentDateTime();
}

DiagnosticsExportRange DiagnosticsExportDialog::range() const {
    DiagnosticsExportRange result;
    result.from = startForMode();
    result.to = endForMode();
    const QStringList labels = {
        tr("Since last AirPlay connection"), tr("Last 1 hour"),
        tr("Last 5 hours"), tr("Entire app session"), tr("Custom range...")};
    const int index = qBound(0, m_mode->currentIndex(), labels.size() - 1);
    result.label = labels.at(index);
    return result;
}

bool DiagnosticsExportDialog::createZip() const {
    return m_zip && m_zip->isChecked();
}

void DiagnosticsExportDialog::updateControls() {
    const bool custom = m_mode->currentIndex() == Custom;
    m_from->setEnabled(custom);
    m_to->setEnabled(custom);

    const DiagnosticsExportRange selected = range();
    if (custom && !selected.isValid()) {
        m_hint->setText(tr("From must be earlier than To."));
        if (m_buttons) {
            m_buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        }
        return;
    }
    if (m_buttons) {
        m_buttons->button(QDialogButtonBox::Save)->setEnabled(true);
    }

    if (custom) {
        m_hint->setText(tr("Custom window: %1 → %2")
                            .arg(selected.from.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                 selected.to.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    } else {
        m_hint->setText(tr("%1: %2 → %3")
                            .arg(selected.label,
                                 selected.from.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                                 selected.to.toString(
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
}
