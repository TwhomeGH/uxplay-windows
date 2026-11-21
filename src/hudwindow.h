#pragma once

#include <QWidget>

class QPlainTextEdit;

/**
 * Standalone Media Info HUD window. It is intentionally not an overlay on the
 * AirPlay video window: a window that overlaps the D3D11 swapchain can make the
 * sink stop presenting, and a child window can disturb DWM composition.
 */
class HudWindow : public QWidget {
    Q_OBJECT

public:
    explicit HudWindow(QWidget *parent = nullptr);

    void setContent(const QString &text);

private:
    QString plainText(const QString &text) const;

    QPlainTextEdit *m_text = nullptr;
    bool m_positioned = false;
};
