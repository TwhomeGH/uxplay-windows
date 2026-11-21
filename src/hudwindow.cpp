#include "hudwindow.h"

#include <QFont>
#include <QGuiApplication>
#include <QDebug>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScreen>
#include <QVBoxLayout>

HudWindow::HudWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(tr("Media Info HUD"));
    setWindowFlags(Qt::Tool | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet(QStringLiteral("QWidget { background:#11161c; color:#e6f0ff; }"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background:#11161c; color:#e6f0ff; "
                       "selection-background-color:#2f6f9f; }"));
    QFont font(QStringLiteral("Microsoft JhengHei UI"), 10);
    m_text->setFont(font);
    m_text->setMinimumWidth(300);
    m_text->setMinimumHeight(140);
    layout->addWidget(m_text);
    resize(420, 220);
    hide();
}

QString HudWindow::plainText(const QString &text) const {
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return value;
}

void HudWindow::setContent(const QString &text) {
    const QString value = plainText(text);
    if (value.isEmpty()) {
        if (isVisible()) {
            qInfo() << "HUD window hidden: no HUD fields selected";
        }
        hide();
        return;
    }
    m_text->setPlainText(value);
    if (!isVisible()) {
        if (!m_positioned) {
            if (QScreen *screen = QGuiApplication::primaryScreen()) {
                const QRect available = screen->availableGeometry();
                move(available.right() - width() - 40,
                     available.top() + 80);
            }
            m_positioned = true;
        }
        show();
        raise();
        qInfo() << "HUD window shown at" << pos();
    }
}
