#include "runtimedebugwindow.h"

#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

RuntimeDebugWindow::RuntimeDebugWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(tr("Runtime Debug"));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    resize(980, 680);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font(QStringLiteral("Consolas"), 9);
    m_text->setFont(font);
    layout->addWidget(m_text);

    auto *buttons = new QHBoxLayout();
    auto *copyButton = new QPushButton(tr("Copy all"), this);
    auto *saveButton = new QPushButton(tr("Save..."), this);
    m_feedback = new QLabel(this);
    m_feedback->setStyleSheet(QStringLiteral("color:#5fbf7f;"));
    buttons->addWidget(copyButton);
    buttons->addWidget(saveButton);
    buttons->addWidget(m_feedback);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(copyButton, &QPushButton::clicked, this, [this, copyButton]() {
        const QString text = m_text->toPlainText();
        QGuiApplication::clipboard()->setText(text);
        copyButton->setText(tr("Copied"));
        if (m_feedback) {
            m_feedback->setText(tr("Copied %1 characters")
                                    .arg(text.size()));
        }
        QTimer::singleShot(1500, this, [this, copyButton]() {
            copyButton->setText(tr("Copy all"));
            if (m_feedback) {
                m_feedback->clear();
            }
        });
    });
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Save runtime debug"), QStringLiteral("runtime-debug.txt"),
            QStringLiteral("Text files (*.txt);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(m_text->toPlainText().toUtf8());
        }
    });
}

void RuntimeDebugWindow::setRawText(const QString &text) {
    if (m_text == nullptr) {
        return;
    }
    const bool atBottom = m_text->verticalScrollBar()->value() >=
                          m_text->verticalScrollBar()->maximum() - 4;
    m_text->setPlainText(text);
    if (atBottom) {
        m_text->verticalScrollBar()->setValue(
            m_text->verticalScrollBar()->maximum());
    }
}
