#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class CleanOutputWindow;
class QNetworkAccessManager;
class QTimer;

typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstBus GstBus;
typedef struct _GObject GObject;
typedef struct _GParamSpec GParamSpec;

/**
 * Owns the experimental USB screen path:
 *
 *   pymobiledevice3 serve-web  ->  stream_to_tcp.py  ->  GStreamer
 *
 * The Python sidecar supplies the RSD tunnel and HEVC access units; the
 * GStreamer pipeline decodes them into a frameless CleanOutputWindow that
 * OBS Window Capture can use. AirPlay is a completely separate backend.
 */
class UsbObsController : public QObject {
    Q_OBJECT

public:
    explicit UsbObsController(QObject *parent = nullptr);
    ~UsbObsController() override;

    void start();
    void stop();
    bool isRunning() const { return m_active; }
    CleanOutputWindow *outputWindow() const { return m_window; }

signals:
    void started();
    void stopped();
    void outputClosed();
    void errorOccurred(const QString &message);
    void logMessage(const QString &line);

private:
    void startSidecar();
    void startBridge();
    void startPipeline();
    void fetchCodec();
    void handleProcessOutput(QProcess *process);
    void handleBusMessage(GstMessage *message);
    void handleParserCaps();
    void applyDisplay(int width, int height, int rotation);
    void applyCrop(int codedWidth, int codedHeight);
    void applyRotation(int rotation);
    void fail(const QString &message);
    void cleanup();
    void appendLog(const QString &line);

    static int busCallback(GstBus *bus, GstMessage *message, void *data);
    static void parserCapsCallback(GObject *object, GParamSpec *pspec,
                                   void *data);

    bool choosePython();
    QString findBridgeScript() const;
    int findFreePort() const;

    QProcess *m_sidecar = nullptr;
    QProcess *m_bridge = nullptr;
    CleanOutputWindow *m_window = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_startTimer = nullptr;

    GstElement *m_pipeline = nullptr;
    GstElement *m_parser = nullptr;
    GstElement *m_rotation = nullptr;
    GstElement *m_crop = nullptr;
    GstElement *m_sink = nullptr;
    unsigned int m_busWatch = 0;

    QString m_python;
    QStringList m_pythonArgs;
    QString m_bridgeScript;

    int m_httpPort = 0;
    int m_tcpPort = 0;
    int m_displayWidth = 0;
    int m_displayHeight = 0;
    int m_displayRotation = 0;

    bool m_active = false;
    bool m_stopping = false;
    bool m_failing = false;
    bool m_started = false;
    bool m_codecPending = false;
    bool m_bridgeReady = false;
};
