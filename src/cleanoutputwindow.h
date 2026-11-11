#pragma once

#include <QSize>
#include <QWidget>

class QMenu;

/**
 * Frameless output window intended for OBS Window Capture.
 *
 * It owns no media pipeline; the controller attaches a GStreamer video
 * sink to this window's native handle. Keeping the window free of chrome,
 * controls and device bezels is what makes the captured source usable.
 */
class CleanOutputWindow : public QWidget {
    Q_OBJECT

public:
    explicit CleanOutputWindow(QWidget *parent = nullptr);

    /// Sets width/height of the video after rotation is applied.
    void setVideoAspectRatio(double ratio);
    QSize preferredSize() const;

    QString obsSourceName() const;

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    Qt::Edges edgesAt(const QPoint &pos) const;
    void beginSystemMove();
    void beginSystemResize(Qt::Edges edges);
    void setAlwaysOnTop(bool enabled);
    void resetSize();

    double m_aspectRatio = 16.0 / 9.0;
    bool m_alwaysOnTop = false;
    bool m_sized = false;
    QMenu *m_menu = nullptr;
};
