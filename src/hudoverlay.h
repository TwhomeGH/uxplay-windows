#pragma once

#include <QObject>
#include <QString>
#include <windows.h>

class QTimer;

/**
 * @brief 附著於影片視窗的原生、點擊穿透 HUD；所有方法均由 UI 執行緒呼叫。
 * @details 不複製影片影格，也不插入 GStreamer pipeline。重疊 D3D11 視窗仍可能
 * 影響呈現，故提供獨立視窗模式供切回，不保證所有 renderer 的實機行為。
 */
class HudOverlay : public QObject {
public:
    /// 建立控制物件；尚未附著時不建立 HWND 或啟動定位 timer。
    /// @param parent Qt 擁有者，管理控制物件生命週期。
    explicit HudOverlay(QObject *parent = nullptr);
    /// 解除附著，回收原生視窗、定位 timer 與 GDI 字型。
    ~HudOverlay() override;

    /// 附著至影片 owner 並建立疊層；影片視窗失效時會解除附著。
    /// @param parent 有效的影片 HWND；不接管影片視窗擁有權。
    void attach(HWND parent);
    /// 銷毀疊層 HWND 並停止定位 timer；可以重複呼叫。
    void detach();
    /// 更新顯示文字，不保留呼叫端的外部指標。
    /// @param text HUD 文字或相容的簡單標記，空內容會隱藏視窗。
    void setText(const QString &text);
    /// 設定相對影片 client area 的位置。
    /// @param corner top-left、top-right、bottom-left 或 bottom-right。
    /// @param marginX 水平邊距，單位像素。
    /// @param marginY 垂直邊距，單位像素。
    void setPosition(const QString &corner, int marginX, int marginY);
    /// 設定顯示狀態；隱藏時停止定位 timer。
    /// @param visible 是否允許顯示，目前無有效影片視窗時仍不會顯示。
    void setOverlayVisible(bool visible);

    /// 原生視窗訊息入口，繪製時不處理影片影格。
    /// @param hwnd 疊層 HWND。
    /// @param message Windows 訊息代碼。
    /// @param wparam 訊息參數。
    /// @param lparam 訊息參數。
    /// @return 訊息處理結果，未處理訊息交給 DefWindowProcW。
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam);

private:
    void ensureWindow();
    void updateLayout();
    void paint(HDC dc);
    QString plainText(const QString &text) const;
    HFONT font() const;

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    RECT m_lastRect = {};
    bool m_hasRect = false;
    QString m_text;
    QString m_corner = QStringLiteral("top-left");
    int m_marginX = 8;
    int m_marginY = 72;
    bool m_visible = true;
    mutable HFONT m_font = nullptr;
    QTimer *m_timer = nullptr;
};
