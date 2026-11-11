#include "cleanoutputwindow.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QShowEvent>
#include <QToolTip>
#include <QWindow>

#ifdef _WIN32
#include <windows.h>
#endif

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
    setMouseTracking(true);

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
    QAction *move = m_menu->addAction("Move window");
    connect(move, &QAction::triggered, this, &CleanOutputWindow::beginSystemMove);
    QAction *resize = m_menu->addAction("Resize window");
    connect(resize, &QAction::triggered, this, [this]() {
        beginSystemResize(Qt::BottomEdge | Qt::RightEdge);
    });
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

Qt::Edges CleanOutputWindow::edgesAt(const QPoint &pos) const {
    constexpr int margin = 8;
    Qt::Edges edges;
    if (pos.x() <= margin) {
        edges |= Qt::LeftEdge;
    }
    if (pos.x() >= width() - margin) {
        edges |= Qt::RightEdge;
    }
    if (pos.y() <= margin) {
        edges |= Qt::TopEdge;
    }
    if (pos.y() >= height() - margin) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void CleanOutputWindow::beginSystemMove() {
    if (QWindow *handle = windowHandle()) {
        if (handle->startSystemMove()) {
            return;
        }
    }
#ifdef _WIN32
    ReleaseCapture();
    SendMessageW(reinterpret_cast<HWND>(winId()), WM_NCLBUTTONDOWN, HTCAPTION,
                 0);
#endif
}

void CleanOutputWindow::beginSystemResize(Qt::Edges edges) {
    if (!edges) {
        return;
    }
    if (QWindow *handle = windowHandle()) {
        if (handle->startSystemResize(edges)) {
            return;
        }
    }
#ifdef _WIN32
    WPARAM hit = HTBOTTOMRIGHT;
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge);
    const bool bottom = edges.testFlag(Qt::BottomEdge);
    if (left && top) {
        hit = HTTOPLEFT;
    } else if (right && top) {
        hit = HTTOPRIGHT;
    } else if (left && bottom) {
        hit = HTBOTTOMLEFT;
    } else if (right && bottom) {
        hit = HTBOTTOMRIGHT;
    } else if (left) {
        hit = HTLEFT;
    } else if (right) {
        hit = HTRIGHT;
    } else if (top) {
        hit = HTTOP;
    } else if (bottom) {
        hit = HTBOTTOM;
    }
    ReleaseCapture();
    SendMessageW(reinterpret_cast<HWND>(winId()), WM_NCLBUTTONDOWN, hit, 0);
#endif
}

void CleanOutputWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const Qt::Edges edges = edgesAt(event->position().toPoint());
        if (edges) {
            beginSystemResize(edges);
        } else {
            beginSystemMove();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CleanOutputWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        const Qt::Edges edges = edgesAt(event->position().toPoint());
        if (edges == (Qt::LeftEdge | Qt::TopEdge) ||
            edges == (Qt::RightEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges == (Qt::RightEdge | Qt::TopEdge) ||
                   edges == (Qt::LeftEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void CleanOutputWindow::mouseReleaseEvent(QMouseEvent *event) {
    unsetCursor();
    QWidget::mouseReleaseEvent(event);
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
