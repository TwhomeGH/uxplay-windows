#include "usbobscontroller.h"

#include "cleanoutputwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>

#include <algorithm>

namespace {
constexpr int kStartupTimeoutMs = 60000;
constexpr int kSidecarStartTimeoutMs = 6000;
constexpr int kBridgeStartTimeoutMs = 6000;
}  // namespace

UsbObsController::UsbObsController(QObject *parent) : QObject(parent) {
    m_network = new QNetworkAccessManager(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(400);
    connect(m_pollTimer, &QTimer::timeout, this,
            &UsbObsController::fetchCodec);

    m_startTimer = new QTimer(this);
    m_startTimer->setSingleShot(true);
    connect(m_startTimer, &QTimer::timeout, this, [this]() {
        if (m_active && !m_started) {
            fail(QStringLiteral("Timed out waiting for the USB screen stream"));
        }
    });
}

UsbObsController::~UsbObsController() {
    stop();
    delete m_window;
    m_window = nullptr;
}

void UsbObsController::start() {
    if (m_active) {
        if (m_window) {
            m_window->show();
            m_window->raise();
        }
        return;
    }

    if (!choosePython()) {
        Q_EMIT errorOccurred(
            QStringLiteral("Python was not found. Set UXPLAY_USB_PYTHON to a "
                           "Python 3 interpreter."));
        return;
    }

    m_bridgeScript = findBridgeScript();
    if (m_bridgeScript.isEmpty()) {
        Q_EMIT errorOccurred(
            QStringLiteral("stream_to_tcp.py was not found next to the "
                           "executable or in the source tree."));
        return;
    }

    m_httpPort = findFreePort();
    m_tcpPort = findFreePort();
    if (!m_httpPort || !m_tcpPort) {
        Q_EMIT errorOccurred(
            QStringLiteral("Could not allocate local ports for the USB "
                           "Clean Output."));
        return;
    }

    if (!m_window) {
        m_window = new CleanOutputWindow();
        connect(m_window, &CleanOutputWindow::closed, this, [this]() {
            stop();
            Q_EMIT outputClosed();
        });
    }

    m_window->setVideoAspectRatio(16.0 / 9.0);
    m_displayWidth = 0;
    m_displayHeight = 0;
    m_displayRotation = 0;
    m_started = false;
    m_codecPending = false;
    m_bridgeReady = false;
    m_stopping = false;
    m_failing = false;
    m_active = true;

    appendLog(QStringLiteral("starting USB Clean Output (http %1, tcp %2)")
                  .arg(m_httpPort)
                  .arg(m_tcpPort));
    if (m_startTimer) {
        m_startTimer->start(kStartupTimeoutMs);
    }
    startSidecar();
}

void UsbObsController::stop() {
    if (!m_active && !m_pipeline && !m_sidecar && !m_bridge) {
        return;
    }
    if (m_stopping) {
        return;
    }

    m_stopping = true;
    cleanup();
    m_active = false;
    m_stopping = false;
    appendLog(QStringLiteral("USB Clean Output stopped"));
    Q_EMIT stopped();
}

void UsbObsController::startSidecar() {
    if (m_sidecar) {
        return;
    }

    m_sidecar = new QProcess(this);
    m_sidecar->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_sidecar, &QProcess::readyReadStandardOutput, this,
            [this]() { handleProcessOutput(m_sidecar); });
    connect(m_sidecar, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (m_stopping || !m_active) {
                    return;
                }
                fail(QStringLiteral("USB sidecar failed to start (%1)")
                         .arg(static_cast<int>(error)));
            });
    connect(m_sidecar,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (m_stopping || !m_active) {
                    return;
                }
                fail(QStringLiteral("USB sidecar exited (code %1)").arg(code));
            });

    QStringList args = m_pythonArgs;
    args << "-m" << "pymobiledevice3" << "developer" << "core-device"
         << "display" << "serve-web" << "--http-port"
         << QString::number(m_httpPort) << "--bind" << "127.0.0.1";

    appendLog(QStringLiteral("sidecar: %1 %2")
                  .arg(m_python, args.join(QLatin1Char(' '))));
    m_sidecar->start(m_python, args);
    if (!m_sidecar->waitForStarted(kSidecarStartTimeoutMs)) {
        fail(QStringLiteral("USB sidecar did not start: %1")
                 .arg(m_sidecar->errorString()));
        return;
    }

    if (m_pollTimer) {
        m_pollTimer->start();
    }
}

void UsbObsController::fetchCodec() {
    if (m_codecPending || m_stopping || !m_active) {
        return;
    }
    m_codecPending = true;

    QNetworkRequest request(
        QUrl(QStringLiteral("http://127.0.0.1:%1/codec").arg(m_httpPort)));
    request.setTransferTimeout(3000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_codecPending = false;
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        reply->deleteLater();

        if (m_stopping || !m_active) {
            return;
        }
        if (error != QNetworkReply::NoError || (status != 0 && status != 200)) {
            appendLog(QStringLiteral("codec wait: %1 (HTTP %2)")
                          .arg(errorString)
                          .arg(status));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            appendLog(QStringLiteral("codec: invalid JSON"));
            return;
        }

        const QJsonObject display =
            document.object().value(QStringLiteral("display")).toObject();
        const int width = display.value(QStringLiteral("width")).toInt();
        const int height = display.value(QStringLiteral("height")).toInt();
        const int rotation =
            display.value(QStringLiteral("rotation")).toInt(0);
        if (width <= 0 || height <= 0) {
            appendLog(QStringLiteral("codec: display is not ready yet"));
            return;
        }

        if (m_pollTimer) {
            m_pollTimer->stop();
        }
        applyDisplay(width, height, rotation);
        startBridge();
    });
}

void UsbObsController::startBridge() {
    if (m_bridge || m_stopping || !m_active) {
        return;
    }

    m_bridge = new QProcess(this);
    m_bridge->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_bridge, &QProcess::readyReadStandardOutput, this,
            [this]() { handleProcessOutput(m_bridge); });
    connect(m_bridge, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (m_stopping || !m_active) {
                    return;
                }
                fail(QStringLiteral("USB bridge failed to start (%1)")
                         .arg(static_cast<int>(error)));
            });
    connect(m_bridge,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (m_stopping || !m_active) {
                    return;
                }
                fail(QStringLiteral("USB bridge exited (code %1)").arg(code));
            });

    QStringList args = m_pythonArgs;
    args << m_bridgeScript << "--http-port" << QString::number(m_httpPort)
         << "--tcp-port" << QString::number(m_tcpPort);

    appendLog(QStringLiteral("bridge: %1 %2")
                  .arg(m_python, args.join(QLatin1Char(' '))));
    m_bridge->start(m_python, args);
    if (!m_bridge->waitForStarted(kBridgeStartTimeoutMs)) {
        fail(QStringLiteral("USB bridge did not start: %1")
                 .arg(m_bridge->errorString()));
    }
}

void UsbObsController::handleProcessOutput(QProcess *process) {
    if (!process) {
        return;
    }
    const QByteArray data = process->readAllStandardOutput();
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        appendLog((process == m_bridge ? QStringLiteral("bridge: ")
                                       : QStringLiteral("sidecar: ")) +
                  line);
        if (process == m_bridge && !m_bridgeReady &&
            line.contains(QStringLiteral("waiting for GStreamer"))) {
            m_bridgeReady = true;
            startPipeline();
        }
    }
}

void UsbObsController::startPipeline() {
    if (m_pipeline || m_stopping || !m_active || !m_window) {
        return;
    }

    gst_init(nullptr, nullptr);

    const QString description =
        QStringLiteral(
            "tcpclientsrc host=127.0.0.1 port=%1 ! "
            "h265parse name=parser ! "
            "avdec_h265 ! "
            "videoflip name=rotate ! "
            "videocrop name=crop right=0 bottom=0 ! "
            "videoconvert ! "
            "d3d11videosink name=sink sync=false force-aspect-ratio=true")
            .arg(m_tcpPort);

    GError *error = nullptr;
    m_pipeline =
        gst_parse_launch(description.toUtf8().constData(), &error);
    if (!m_pipeline || error) {
        const QString message =
            error ? QString::fromUtf8(error->message)
                  : QStringLiteral("Could not create the USB pipeline");
        if (error) {
            g_error_free(error);
        }
        fail(message);
        return;
    }

    m_parser = gst_bin_get_by_name(GST_BIN(m_pipeline), "parser");
    m_rotation = gst_bin_get_by_name(GST_BIN(m_pipeline), "rotate");
    m_crop = gst_bin_get_by_name(GST_BIN(m_pipeline), "crop");
    m_sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    if (!m_parser || !m_rotation || !m_crop || !m_sink) {
        fail(QStringLiteral("USB pipeline is missing a required element"));
        return;
    }
    if (!GST_IS_VIDEO_OVERLAY(m_sink)) {
        fail(QStringLiteral("d3d11videosink does not support VideoOverlay"));
        return;
    }

    applyRotation(m_displayRotation);

    GstPad *parserPad = gst_element_get_static_pad(m_parser, "src");
    if (parserPad) {
        g_signal_connect(parserPad, "notify::caps",
                         G_CALLBACK(&UsbObsController::parserCapsCallback),
                         this);
        gst_object_unref(parserPad);
    }

    m_window->show();
    m_window->raise();
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(m_sink),
                                       (guintptr)m_window->winId());

    GstBus *bus = gst_element_get_bus(m_pipeline);
    if (bus) {
        m_busWatch = gst_bus_add_watch(bus, &UsbObsController::busCallback,
                                       this);
        gst_object_unref(bus);
    }

    const GstStateChangeReturn result =
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (result == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("GStreamer could not start the USB pipeline"));
        return;
    }
    appendLog(QStringLiteral("pipeline started"));
}

void UsbObsController::applyDisplay(int width, int height, int rotation) {
    m_displayWidth = width;
    m_displayHeight = height;
    m_displayRotation = rotation;

    const bool sideways = qAbs(rotation) % 180 == 90;
    const double aspect =
        sideways ? double(height) / double(width)
                 : double(width) / double(height);
    if (m_window) {
        m_window->setVideoAspectRatio(aspect);
        m_window->show();
        m_window->raise();
    }
    appendLog(QStringLiteral("display %1x%2 rotation %3")
                  .arg(width)
                  .arg(height)
                  .arg(rotation));
}

void UsbObsController::applyCrop(int codedWidth, int codedHeight) {
    if (!m_crop || m_displayWidth <= 0 || m_displayHeight <= 0) {
        return;
    }
    const int right = std::max(0, codedWidth - m_displayWidth);
    const int bottom = std::max(0, codedHeight - m_displayHeight);
    g_object_set(m_crop, "right", static_cast<guint>(right), "bottom",
                 static_cast<guint>(bottom), nullptr);
}

void UsbObsController::applyRotation(int rotation) {
    if (!m_rotation) {
        return;
    }
    const int normalized = ((rotation % 360) + 360) % 360;
    // GstVideoFlipMethod: 0 none, 1 clockwise, 2 rotate-180,
    // 3 counterclockwise.
    int method = 0;
    if (normalized == 90) {
        method = 1;
    } else if (normalized == 180) {
        method = 2;
    } else if (normalized == 270) {
        method = 3;
    }
    g_object_set(m_rotation, "method", method, nullptr);
}

void UsbObsController::handleBusMessage(GstMessage *message) {
    if (!message) {
        return;
    }

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        QString text = error ? QString::fromUtf8(error->message)
                             : QStringLiteral("GStreamer error");
        if (debug) {
            text += QStringLiteral(" (%1)").arg(QString::fromUtf8(debug));
        }
        if (error) {
            g_error_free(error);
        }
        g_free(debug);
        QMetaObject::invokeMethod(
            this, [this, text]() { fail(text); }, Qt::QueuedConnection);
        break;
    }
    case GST_MESSAGE_EOS:
        QMetaObject::invokeMethod(
            this, [this]() { fail(QStringLiteral("USB stream ended")); },
            Qt::QueuedConnection);
        break;
    case GST_MESSAGE_WARNING: {
        GError *error = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_warning(message, &error, &debug);
        if (error) {
            appendLog(QStringLiteral("GStreamer warning: %1")
                          .arg(QString::fromUtf8(error->message)));
            g_error_free(error);
        }
        g_free(debug);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
            GstState oldState = GST_STATE_VOID_PENDING;
            GstState newState = GST_STATE_VOID_PENDING;
            GstState pending = GST_STATE_VOID_PENDING;
            gst_message_parse_state_changed(message, &oldState, &newState,
                                            &pending);
            if (newState == GST_STATE_PLAYING) {
                // The bus watch runs on the GStreamer streaming thread.
                // QTimer and the MainWindow signal belong on the UI thread.
                QMetaObject::invokeMethod(
                    this,
                    [this]() {
                        if (m_started || m_stopping || !m_active) {
                            return;
                        }
                        m_started = true;
                        if (m_startTimer) {
                            m_startTimer->stop();
                        }
                        appendLog(QStringLiteral("USB Clean Output is playing"));
                        Q_EMIT started();
                    },
                    Qt::QueuedConnection);
            }
        }
        break;
    default:
        break;
    }
}

void UsbObsController::handleParserCaps() {
    if (!m_parser) {
        return;
    }
    GstPad *pad = gst_element_get_static_pad(m_parser, "src");
    if (!pad) {
        return;
    }
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (caps) {
        const GstStructure *structure = gst_caps_get_structure(caps, 0);
        int width = 0;
        int height = 0;
        if (structure &&
            gst_structure_get_int(structure, "width", &width) &&
            gst_structure_get_int(structure, "height", &height)) {
            applyCrop(width, height);
        }
        gst_caps_unref(caps);
    }
    gst_object_unref(pad);
}

gboolean UsbObsController::busCallback(GstBus *, GstMessage *message,
                                       void *data) {
    if (auto *self = static_cast<UsbObsController *>(data)) {
        self->handleBusMessage(message);
    }
    return TRUE;
}

void UsbObsController::parserCapsCallback(GObject *object, GParamSpec *,
                                          void *data) {
    if (auto *self = static_cast<UsbObsController *>(data)) {
        if (object && GST_IS_PAD(object)) {
            self->handleParserCaps();
        }
    }
}

bool UsbObsController::choosePython() {
    m_python.clear();
    m_pythonArgs.clear();

    const QString override = qEnvironmentVariable("UXPLAY_USB_PYTHON");
    if (!override.isEmpty()) {
        const QStringList parts = QProcess::splitCommand(override);
        if (!parts.isEmpty()) {
            m_python = parts.first();
            m_pythonArgs = parts.mid(1);
        }
    }

    if (m_python.isEmpty()) {
        const QString py = QStandardPaths::findExecutable(
            QStringLiteral("py"));
        if (!py.isEmpty()) {
            m_python = py;
            m_pythonArgs << QStringLiteral("-3.11");
        } else {
            const QString python = QStandardPaths::findExecutable(
                QStringLiteral("python"));
            if (!python.isEmpty()) {
                m_python = python;
            }
        }
    }
    return !m_python.isEmpty();
}

QString UsbObsController::findBridgeScript() const {
    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() +
                      QStringLiteral("/usbcapture/stream_to_tcp.py");
#ifdef USB_BRIDGE_SCRIPT
    candidates << QStringLiteral(USB_BRIDGE_SCRIPT);
#endif
    candidates << QCoreApplication::applicationDirPath() +
                      QStringLiteral(
                          "/../usbcapture/spike/tools/stream_to_tcp.py");

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return {};
}

int UsbObsController::findFreePort() const {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const int port = probe.serverPort();
    probe.close();
    return port;
}

void UsbObsController::appendLog(const QString &line) {
    qInfo().noquote() << "[usb-obs]" << line;
    Q_EMIT logMessage(line);
}

void UsbObsController::fail(const QString &message) {
    if (m_stopping || m_failing) {
        return;
    }
    m_failing = true;
    appendLog(QStringLiteral("error: %1").arg(message));
    cleanup();
    m_active = false;
    m_stopping = false;
    Q_EMIT errorOccurred(message);
    Q_EMIT stopped();
    m_failing = false;
}

void UsbObsController::cleanup() {
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    if (m_startTimer) {
        m_startTimer->stop();
    }
    if (m_busWatch) {
        g_source_remove(m_busWatch);
        m_busWatch = 0;
    }

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_element_get_state(m_pipeline, nullptr, nullptr, 2 * GST_SECOND);
    }

    auto unrefElement = [](GstElement *&element) {
        if (element) {
            gst_object_unref(element);
            element = nullptr;
        }
    };
    unrefElement(m_parser);
    unrefElement(m_rotation);
    unrefElement(m_crop);
    unrefElement(m_sink);
    if (m_pipeline) {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    auto stopProcess = [](QProcess *&process) {
        if (!process) {
            return;
        }
        process->disconnect();
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(1500)) {
                process->kill();
                process->waitForFinished(500);
            }
        }
        process->deleteLater();
        process = nullptr;
    };
    stopProcess(m_bridge);
    stopProcess(m_sidecar);

    if (m_window) {
        m_window->hide();
    }
    m_started = false;
    m_codecPending = false;
    m_bridgeReady = false;
    m_displayWidth = 0;
    m_displayHeight = 0;
    m_displayRotation = 0;
}
