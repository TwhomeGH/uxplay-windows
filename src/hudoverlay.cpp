#include "hudoverlay.h"

#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int HUD_PADDING = 10;
constexpr int HUD_ALPHA = 225;

ATOM registerHudClass() {
    static ATOM atom = 0;
    if (atom != 0) {
        return atom;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HudOverlay::windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"UxPlayNativeHudOverlay";
    atom = RegisterClassExW(&wc);
    return atom;
}

}  // namespace

HudOverlay::HudOverlay(QObject *parent) : QObject(parent) {
    registerHudClass();
    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (!IsWindow(m_parent)) {
            detach();
            return;
        }
        updateLayout();
    });
}

HudOverlay::~HudOverlay() {
    detach();
    if (m_font) {
        DeleteObject(m_font);
    }
}

void HudOverlay::attach(HWND parent) {
    if (!IsWindow(parent)) {
        detach();
        return;
    }
    if (parent == m_parent && m_hwnd && IsWindow(m_hwnd)) {
        updateLayout();
        return;
    }
    detach();
    m_parent = parent;
    ensureWindow();
    if (m_hwnd && m_visible && !m_timer->isActive()) m_timer->start();
    updateLayout();
}

void HudOverlay::detach() {
    m_timer->stop();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_parent = nullptr;
    m_hasRect = false;
}

void HudOverlay::setText(const QString &text) {
    if (m_text == text) {
        return;
    }
    m_text = text;
    updateLayout();
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void HudOverlay::setPosition(const QString &corner, int marginX, int marginY) {
    if (m_corner == corner && m_marginX == marginX && m_marginY == marginY) return;
    m_corner = corner;
    m_marginX = marginX;
    m_marginY = marginY;
    updateLayout();
}

void HudOverlay::setOverlayVisible(bool visible) {
    m_visible = visible;
    if (m_hwnd && visible) {
        if (!m_timer->isActive()) m_timer->start();
    } else {
        m_timer->stop();
    }
    updateLayout();
}

void HudOverlay::ensureWindow() {
    if (m_hwnd || !m_parent || !IsWindow(m_parent)) {
        return;
    }
    if (registerHudClass() == 0) {
        return;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
            WS_EX_TOOLWINDOW,
        L"UxPlayNativeHudOverlay", L"", WS_POPUP,
        0, 0, 10, 10, m_parent, nullptr, instance, nullptr);
    if (!m_hwnd) {
        return;
    }
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(m_hwnd, 0, HUD_ALPHA, LWA_ALPHA);
    m_hasRect = false;
    // 使用影片 HWND 作為 popup owner，跟隨其生命週期，不做全域置頂視窗。
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 10, 10,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

QString HudOverlay::plainText(const QString &text) const {
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return value;
}

HFONT HudOverlay::font() const {
    if (!m_font) {
        m_font = CreateFontW(
            -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft JhengHei UI");
    }
    return m_font;
}

void HudOverlay::updateLayout() {
    if (!m_hwnd || !m_parent || !IsWindow(m_hwnd) || !IsWindow(m_parent)) {
        return;
    }
    const QString value = plainText(m_text);
    if (!m_visible || value.isEmpty() || !IsWindowVisible(m_parent) || IsIconic(m_parent)) {
        ShowWindow(m_hwnd, SW_HIDE);
        return;
    }

    HDC dc = GetDC(m_hwnd);
    if (!dc) {
        return;
    }
    HGDIOBJ old_font = SelectObject(dc, font());
    RECT measure = {0, 0, 0, 0};
    const std::wstring wide = value.toStdWString();
    DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), &measure,
              DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
    SelectObject(dc, old_font);
    ReleaseDC(m_hwnd, dc);

    const int width = measure.right + HUD_PADDING * 2;
    const int height = measure.bottom + HUD_PADDING * 2;
    RECT client = {};
    GetClientRect(m_parent, &client);
    const int parent_width = client.right - client.left;
    const int parent_height = client.bottom - client.top;
    const bool right = m_corner.endsWith(QStringLiteral("right"));
    const bool bottom = m_corner.startsWith(QStringLiteral("bottom"));
    const int local_x = right ? std::max(0, parent_width - width - m_marginX)
                              : m_marginX;
    const int local_y = bottom ? std::max(0, parent_height - height - m_marginY)
                               : m_marginY;

    POINT origin = {0, 0};
    ClientToScreen(m_parent, &origin);
    const int x = origin.x + local_x;
    const int y = origin.y + local_y;
    RECT target = {x, y, x + width, y + height};
    const bool same_rect = m_hasRect
                           && target.left == m_lastRect.left
                           && target.top == m_lastRect.top
                           && target.right == m_lastRect.right
                           && target.bottom == m_lastRect.bottom;
    if (!same_rect) {
        SetWindowPos(m_hwnd, HWND_TOP, x, y, width, height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_lastRect = target;
        m_hasRect = true;
        InvalidateRect(m_hwnd, nullptr, TRUE);
    } else if (!IsWindowVisible(m_hwnd)) {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }
}

void HudOverlay::paint(HDC dc) {
    RECT client = {};
    GetClientRect(m_hwnd, &client);
    HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &client, background);
    DeleteObject(background);

    if (!m_visible || m_text.isEmpty()) {
        return;
    }

    HGDIOBJ old_font = SelectObject(dc, font());
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(235, 245, 255));
    RECT text_rect = {HUD_PADDING, HUD_PADDING,
                      client.right - HUD_PADDING, client.bottom - HUD_PADDING};
    const QString value = plainText(m_text);
    const std::wstring wide = value.toStdWString();
    DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), &text_rect,
              DT_LEFT | DT_TOP | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

LRESULT CALLBACK HudOverlay::windowProc(HWND hwnd, UINT message,
                                         WPARAM wparam, LPARAM lparam) {
    HudOverlay *self = reinterpret_cast<HudOverlay *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        if (self) {
            self->paint(dc);
        } else {
            FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        // owner 被 renderer 銷毀時也會走此路徑，避免留下失效或被重用的 HWND。
        if (self) {
            self->m_hwnd = nullptr;
            self->m_parent = nullptr;
            self->m_hasRect = false;
            self->m_timer->stop();
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
