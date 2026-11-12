#include "mediainfohud.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

MediaInfoHud::MediaInfoHud(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(9);
    setFont(font);
    hide();
}

void MediaInfoHud::setLines(const QStringList &lines) {
    if (m_lines == lines) {
        return;
    }
    m_lines = lines;
    resizeToContent();
    if (isVisible() && m_hwnd) {
        updateForWindow(m_hwnd);
    }
}

void MediaInfoHud::setCorner(const QString &corner) {
    if (corner != QStringLiteral("top-right")) {
        m_corner = QStringLiteral("top-left");
    } else {
        m_corner = corner;
    }
}

void MediaInfoHud::resizeToContent() {
    const QFontMetrics metrics(font());
    int width = 0;
    int height = 0;
    for (const QString &line : m_lines) {
        width = qMax(width, metrics.horizontalAdvance(line));
        height += metrics.height();
    }
    setFixedSize(width + 20, height + 16);
}

void MediaInfoHud::updateForWindow(quintptr hwnd) {
    if (!hwnd) {
        detach();
        return;
    }

    if (m_hwnd != hwnd) {
        detach();
        m_hwnd = hwnd;
    }

#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(hwnd);
    HWND hud = reinterpret_cast<HWND>(winId());
    if (!IsWindow(window) || !IsWindow(hud)
        || !IsWindowVisible(window) || IsIconic(window)) {
        hide();
        return;
    }

    RECT client = {};
    if (!GetClientRect(window, &client)) {
        hide();
        return;
    }

    /* Make the overlay a native child so Windows moves it with the video
     * window instead of relying on the polling tracker. */
    if (!m_child || GetParent(hud) != window) {
        SetParent(hud, window);
        if (GetParent(hud) == window) {
            LONG_PTR style = GetWindowLongPtrW(hud, GWL_STYLE);
            style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
            style |= WS_CHILD | WS_VISIBLE;
            SetWindowLongPtrW(hud, GWL_STYLE, style);
            m_child = true;
        } else {
            m_child = false;
        }
    }

    const int clientWidth = client.right - client.left;
    const int x = m_corner == QStringLiteral("top-right")
                      ? clientWidth - width() - 8
                      : 8;
    const int y = 8;

    if (m_child) {
        SetWindowPos(hud, HWND_TOP, x, y, width(), height(),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        show();
        applyClickThrough();
        return;
    }

    /* Fallback to a top-level window if the native child attach failed. */
    POINT origin = {0, 0};
    if (!ClientToScreen(window, &origin)) {
        hide();
        return;
    }
    const int screenX = m_corner == QStringLiteral("top-right")
                            ? origin.x + clientWidth - width() - 8
                            : origin.x + 8;
    const int screenY = origin.y + 8;
    SetWindowPos(hud, HWND_TOPMOST, screenX, screenY, width(), height(),
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    show();
    applyClickThrough();
#else
    Q_UNUSED(hwnd);
    hide();
#endif
}

void MediaInfoHud::detach() {
#ifdef Q_OS_WIN
    HWND hud = reinterpret_cast<HWND>(winId());
    if (IsWindow(hud) && m_child) {
        SetParent(hud, nullptr);
        LONG_PTR style = GetWindowLongPtrW(hud, GWL_STYLE);
        style &= ~WS_CHILD;
        style |= WS_POPUP;
        SetWindowLongPtrW(hud, GWL_STYLE, style);
        SetWindowPos(hud, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                         | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
    m_child = false;
    m_hwnd = 0;
    hide();
}

void MediaInfoHud::applyClickThrough() {
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(winId());
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(window, GWL_EXSTYLE, style);
#endif
}

void MediaInfoHud::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    applyClickThrough();
}

void MediaInfoHud::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 175));
    painter.drawRoundedRect(rect(), 6, 6);

    painter.setPen(QColor(245, 245, 245));
    const QFontMetrics metrics(font());
    int y = 8 + metrics.ascent();
    for (const QString &line : m_lines) {
        painter.drawText(10, y, line);
        y += metrics.height();
    }
}
