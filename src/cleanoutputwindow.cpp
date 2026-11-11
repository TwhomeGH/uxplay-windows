#include "cleanoutputwindow.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QShowEvent>
#include <QToolTip>

CleanOutputWindow::CleanOutputWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint |
                          Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(obsSourceName());
    setAutoFillBackground(true);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    setMinimumSize(320, 200);
    setFocusPolicy(Qt::NoFocus);

    m_menu = new QMenu(this);
    QAction *alwaysOnTop = m_menu->addAction("Always on top");
    alwaysOnTop->setCheckable(true);
    alwaysOnTop->setChecked(m_alwaysOnTop);
    connect(alwaysOnTop, &QAction::toggled, this,
            &CleanOutputWindow::setAlwaysOnTop);

    QAction *copyName = m_menu->addAction("Copy OBS source name");
    connect(copyName, &QAction::triggered, this, [this]() {
        QGuiApplication::clipboard()->setText(obsSourceName());
        QToolTip::showText(QCursor::pos(),
                           QStringLiteral("Copied: %1").arg(obsSourceName()),
                           this);
    });

    m_menu->addSeparator();
    QAction *reset = m_menu->addAction("Reset size");
    connect(reset, &QAction::triggered, this, &CleanOutputWindow::resetSize);
    QAction *closeAction = m_menu->addAction("Close");
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
}

void CleanOutputWindow::setVideoAspectRatio(double ratio) {
    if (ratio <= 0.0) {
        return;
    }
    m_aspectRatio = ratio;
    if (isVisible() && !m_sized) {
        resetSize();
    }
}

QSize CleanOutputWindow::preferredSize() const {
    QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QSize available =
        scr ? scr->availableGeometry().size() : QSize(1280, 720);

    int width = qMin(1280, qMax(320, int(available.width() * 0.7)));
    int height = qMax(200, int(width / m_aspectRatio));
    if (height > int(available.height() * 0.8)) {
        height = qMax(200, int(available.height() * 0.8));
        width = qMax(320, int(height * m_aspectRatio));
    }
    return QSize(width, height);
}

QString CleanOutputWindow::obsSourceName() const {
    return QStringLiteral("uxplay-windows USB Clean Output");
}

void CleanOutputWindow::showEvent(QShowEvent *event) {
    if (!m_sized) {
        resetSize();
        m_sized = true;
    }
    QWidget::showEvent(event);
}

void CleanOutputWindow::resetSize() {
    const QSize size = preferredSize();
    resize(size);
    QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (scr) {
        const QRect available = scr->availableGeometry();
        move(available.center() - rect().center());
    }
}

void CleanOutputWindow::setAlwaysOnTop(bool enabled) {
    if (m_alwaysOnTop == enabled) {
        return;
    }
    m_alwaysOnTop = enabled;
    const bool wasVisible = isVisible();
    if (wasVisible) {
        hide();
    }
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        show();
    }
}

void CleanOutputWindow::contextMenuEvent(QContextMenuEvent *event) {
    if (m_menu) {
        m_menu->exec(event->globalPos());
    }
}

void CleanOutputWindow::closeEvent(QCloseEvent *event) {
    emit closed();
    event->accept();
}
