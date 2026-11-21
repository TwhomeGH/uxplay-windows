#ifndef RUNTIMEDEBUGWINDOW_H
#define RUNTIMEDEBUGWINDOW_H

#include <QWidget>

class QPlainTextEdit;
class QLabel;

/// Read-only developer view of the current runtime state.
///
/// This is deliberately separate from the user-facing Diagnostics Window: it
/// shows the raw applied engine arguments, GPU adapter selection, counters and
/// recent structured events without the normal log-level filtering.
class RuntimeDebugWindow : public QWidget {
    Q_OBJECT

public:
    explicit RuntimeDebugWindow(QWidget *parent = nullptr);

    void setRawText(const QString &text);

private:
    QPlainTextEdit *m_text = nullptr;
    QLabel *m_feedback = nullptr;
};

#endif
