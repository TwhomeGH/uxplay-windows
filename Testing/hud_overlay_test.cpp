// Exercise the real overlay HWND lifecycle without showing a video window.
#include "../src/hudoverlay.h"
#include <QCoreApplication>
#include <QTimer>
#include <cassert>
#include <cstdio>
#include <cwchar>

static BOOL CALLBACK findOverlay(HWND window, LPARAM context) {
    wchar_t name[128];
    GetClassNameW(window, name, 128);
    if (std::wcscmp(name, L"UxPlayNativeHudOverlay") == 0) {
        auto *found = reinterpret_cast<HWND *>(context);
        assert(*found == nullptr); // Only one overlay may remain attached.
        *found = window;
    }
    return TRUE;
}

static HWND overlayWindow() {
    HWND result = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), findOverlay, reinterpret_cast<LPARAM>(&result));
    return result;
}

static HWND createOwner() {
    HWND owner = CreateWindowExW(0, L"STATIC", L"HUD regression owner", WS_OVERLAPPEDWINDOW,
                                 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(owner && !IsWindowVisible(owner));
    return owner;
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    assert(!overlayWindow());
    {
        HudOverlay overlay;
        QTimer *timer = overlay.findChild<QTimer *>();
        assert(timer && !timer->isActive());
        overlay.setText(QStringLiteral("client: test\nfps: 60"));
        for (int cycle = 0; cycle < 50; ++cycle) {
            HWND first = createOwner();
            HWND second = createOwner();
            overlay.attach(first);
            assert(overlayWindow() && timer->isActive());
            assert(GetWindow(overlayWindow(), GW_OWNER) == first);
            assert(!IsWindowVisible(overlayWindow()));
            overlay.attach(first); // Same owner must not create another window.
            overlay.attach(second);
            assert(GetWindow(overlayWindow(), GW_OWNER) == second);
            overlay.setOverlayVisible(false);
            assert(!timer->isActive() && !IsWindowVisible(overlayWindow()));
            overlay.setOverlayVisible(true);
            assert(timer->isActive());
            DestroyWindow(second); // Renderer teardown must invalidate the HUD too.
            assert(!overlayWindow() && !timer->isActive());
            overlay.attach(first);
            assert(overlayWindow());
            overlay.detach();
            overlay.detach();
            assert(!overlayWindow() && !timer->isActive());
            DestroyWindow(first);
        }
        HWND owner = createOwner();
        overlay.attach(owner);
        DestroyWindow(owner);
        overlay.attach(nullptr);
    }
    assert(!overlayWindow());
    puts("HUD overlay lifecycle test passed (50 owner/switch/teardown cycles)");
}
