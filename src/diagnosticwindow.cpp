#include "diagnosticwindow.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString dash(const QString &value) {
    return value.isEmpty() ? QStringLiteral("-") : value;
}

QString number(double value, int decimals = 1) {
    return value < 0.0 ? QStringLiteral("-")
                       : QString::number(value, 'f', decimals);
}

QString row(const QString &label, const QString &value) {
    return label + QStringLiteral(": ") + value;
}
}  // namespace

DiagnosticsWindow::DiagnosticsWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(tr("Diagnostics - uxplay-windows"));
    setWindowFlag(Qt::Window, true);
    resize(760, 620);
    setMinimumSize(560, 420);

    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    auto makePage = [this](QTabWidget *tabs, const QString &title) {
        auto *page = new QPlainTextEdit(tabs);
        page->setReadOnly(true);
        page->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        page->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        tabs->addTab(page, title);
        return page;
    };

    m_overview = makePage(m_tabs, tr("Overview"));
    m_video = makePage(m_tabs, tr("Video"));
    m_audio = makePage(m_tabs, tr("Audio"));
    m_network = makePage(m_tabs, tr("Network"));
    m_events = makePage(m_tabs, tr("Events"));
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QHBoxLayout();
    m_copySummary = new QPushButton(tr("Copy summary"), this);
    m_copyAll = new QPushButton(tr("Copy all"), this);
    m_save = new QPushButton(tr("Save diagnostics"), this);
    m_close = new QPushButton(tr("Close"), this);
    buttons->addWidget(m_copySummary);
    buttons->addWidget(m_copyAll);
    buttons->addStretch();
    buttons->addWidget(m_save);
    buttons->addWidget(m_close);
    layout->addLayout(buttons);

    connect(m_copySummary, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(summaryText());
    });
    connect(m_copyAll, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(allText());
    });
    connect(m_save, &QPushButton::clicked, this,
            &DiagnosticsWindow::saveDiagnosticsRequested);
    connect(m_close, &QPushButton::clicked, this, &QWidget::hide);

    updateTexts();
}

void DiagnosticsWindow::setSnapshot(const DiagnosticsSnapshot &snapshot) {
    m_snapshot = snapshot;
    updateTexts();
}

void DiagnosticsWindow::showAndRaise() {
    setWindowState(Qt::WindowNoState);
    showNormal();
    show();
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
    // Qt can create the native window after the first show on Windows. Restore
    // once more after the event loop has processed the creation; otherwise the
    // window can remain at the minimized coordinates (-32000, -32000).
    QTimer::singleShot(0, this, [this]() {
        if (isMinimized()) {
            showNormal();
        }
        raise();
        activateWindow();
    });
    updateTexts();
}

QString DiagnosticsWindow::summaryText() const {
    const QString client = m_snapshot.clientName.isEmpty()
                               ? QStringLiteral("-")
                               : m_snapshot.clientName + QStringLiteral(" (")
                                     + dash(m_snapshot.clientModel)
                                     + QStringLiteral(")");
    QStringList lines;
    lines << tr("uxplay-windows diagnostics");
    lines << row(tr("Status"), dash(m_snapshot.status));
    lines << row(tr("Client"), client);
    lines << row(tr("Video"),
                 QStringLiteral("%1  %2  %3 fps")
                     .arg(dash(m_snapshot.videoCodec),
                          dash(m_snapshot.outputSize), number(m_snapshot.fps)));
    lines << row(tr("Audio"),
                 QStringLiteral("%1  %2")
                     .arg(dash(m_snapshot.audioCodec),
                          dash(m_snapshot.audioFormat)));
    lines << row(tr("Last error"), dash(m_snapshot.lastError));
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString DiagnosticsWindow::allText() const {
    auto section = [](const QString &title) {
        return QStringLiteral("\n--- ") + title + QStringLiteral(" ---\n");
    };
    return summaryText() + section(tr("Overview")) + m_overview->toPlainText()
           + section(tr("Video")) + m_video->toPlainText()
           + section(tr("Audio")) + m_audio->toPlainText()
           + section(tr("Network")) + m_network->toPlainText()
           + section(tr("Events")) + m_events->toPlainText();
}

void DiagnosticsWindow::updateTexts() {
    if (!m_overview) {
        return;
    }

    const QString client = m_snapshot.clientName.isEmpty()
                               ? QStringLiteral("-")
                               : m_snapshot.clientName
                                     + QStringLiteral(" (")
                                     + dash(m_snapshot.clientModel)
                                     + QStringLiteral(")");
    QStringList overview;
    overview << row(tr("Time"), dash(m_snapshot.timestamp));
    overview << row(tr("Status"), dash(m_snapshot.status));
    overview << row(tr("Client"), client);
    overview << row(tr("Device ID"), dash(m_snapshot.clientDeviceId));
    overview << row(tr("Connections"), QString::number(m_snapshot.connectionCount));
    overview << row(tr("Session uptime"), dash(m_snapshot.sessionUptime));
    overview << row(tr("App version"), dash(m_snapshot.appVersion));
    overview << row(tr("OBS mode"), dash(m_snapshot.obsMode));
    overview << row(tr("USB"), dash(m_snapshot.usbStatus));
    overview << row(tr("Last error"), dash(m_snapshot.lastError));
    overview << QString();
    overview << row(tr("Settings"), dash(m_snapshot.settingsPath));
    overview << row(tr("Log"), dash(m_snapshot.logPath));
    overview << row(tr("Diagnostics"), dash(m_snapshot.diagnosticsPath));
    overview << QString();
    overview << tr("Engine arguments");
    overview << dash(m_snapshot.engineArgs);
    m_overview->setPlainText(overview.join(QLatin1Char('\n')));

    QStringList video;
    video << row(tr("Codec"), dash(m_snapshot.videoCodec));
    video << row(tr("Output"), dash(m_snapshot.outputSize));
    video << row(tr("Requested"), dash(m_snapshot.requestedSize));
    video << row(tr("Decoder"), dash(m_snapshot.decoder));
    video << row(tr("Renderer"), dash(m_snapshot.renderer));
    video << row(tr("FPS"), number(m_snapshot.fps));
    video << row(tr("Frame interval"),
                 QStringLiteral("%1 ms").arg(number(m_snapshot.frameIntervalMs)));
    video << row(tr("Last frame age"),
                 QStringLiteral("%1 ms").arg(number(m_snapshot.frameAgeMs)));
    video << row(tr("Presented frames"), QString::number(m_snapshot.frames));
    video << row(tr("Input buffers"), QString::number(m_snapshot.inputFrames));
    video << row(tr("Input age"),
                 QStringLiteral("%1 ms").arg(number(m_snapshot.inputAgeMs)));
    m_video->setPlainText(video.join(QLatin1Char('\n')));

    QStringList audio;
    audio << row(tr("Codec"), dash(m_snapshot.audioCodec));
    audio << row(tr("Format"), dash(m_snapshot.audioFormat));
    audio << row(tr("Reported latency"), dash(m_snapshot.audioLatency));
    audio << QString();
    audio << row(tr("Packets received"),
                 QString::number(m_snapshot.audioPacketsReceived));
    audio << row(tr("Packets delivered"),
                 QString::number(m_snapshot.audioPacketsDecoded));
    audio << row(tr("Resend requests"),
                 QString::number(m_snapshot.audioResendRequests));
    audio << row(tr("Empty resend responses"),
                 QString::number(m_snapshot.audioEmptyResends));
    audio << row(tr("Bytes received"),
                 QString::number(m_snapshot.audioBytes));
    audio << QString();
    audio << tr("A rising empty-resend count means the client asked for audio "
                "that it no longer had available; this can sound like a short "
                "audio drop.");
    m_audio->setPlainText(audio.join(QLatin1Char('\n')));

    QStringList network;
    network << row(tr("Client"), client);
    network << row(tr("Device ID"), dash(m_snapshot.clientDeviceId));
    network << row(tr("Connections"), QString::number(m_snapshot.connectionCount));
    network << QString();
    network << tr("RTP and connection details are recorded in the diagnostics "
                 "bundle. Use Save diagnostics when the issue reproduces.");
    m_network->setPlainText(network.join(QLatin1Char('\n')));

    m_events->setPlainText(
        m_snapshot.recentEvents.isEmpty()
            ? tr("No recorded events yet.")
            : m_snapshot.recentEvents.join(QLatin1Char('\n')));
}
