#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

/// Small click-through overlay drawn over the AirPlay video window.
class MediaInfoHud : public QWidget {
    Q_OBJECT

public:
    explicit MediaInfoHud(QWidget *parent = nullptr);

    void setLines(const QStringList &lines);
    void setCorner(const QString &corner);
    QString corner() const { return m_corner; }
    void updateForWindow(quintptr hwnd);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyClickThrough();
    void resizeToContent();

    QStringList m_lines;
    QString m_corner = QStringLiteral("top-left");
    quintptr m_hwnd = 0;
};
