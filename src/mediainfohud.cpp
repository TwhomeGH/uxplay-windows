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
    m_hwnd = hwnd;
    if (!hwnd) {
        hide();
        return;
    }

#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(hwnd);
    if (!IsWindow(window) || !IsWindowVisible(window)
        || IsIconic(window)) {
        hide();
        return;
    }

    RECT client = {};
    if (!GetClientRect(window, &client)) {
        hide();
        return;
    }

    POINT origin = {0, 0};
    if (!ClientToScreen(window, &origin)) {
        hide();
        return;
    }

    const int clientWidth = client.right - client.left;
    const int x = m_corner == QStringLiteral("top-right")
                      ? origin.x + clientWidth - width() - 8
                      : origin.x + 8;
    const int y = origin.y + 8;
    move(x, y);
    show();
    raise();
#else
    Q_UNUSED(hwnd);
    hide();
#endif
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
