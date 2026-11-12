#include "settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString appDataDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString legacyArgumentsPath() {
    return appDataDir() + QStringLiteral("/arguments.txt");
}

bool parseResolution(const QString &value, ResolutionSettings *out) {
    static const QRegularExpression re(
        QStringLiteral("^([0-9]+)[xX]([0-9]+)(?:@([0-9]+))?$"));
    const QRegularExpressionMatch match = re.match(value);
    if (!match.hasMatch()) {
        return false;
    }

    const int width = match.captured(1).toInt();
    const int height = match.captured(2).toInt();
    const int refresh = match.hasCaptured(3) ? match.captured(3).toInt() : 60;

    if (width == 1280 && height == 720) {
        out->mode = QStringLiteral("720p");
    } else if (width == 1920 && height == 1080) {
        out->mode = QStringLiteral("1080p");
    } else if (width == 2560 && height == 1440) {
        out->mode = QStringLiteral("1440p");
    } else if (width == 3840 && height == 2160) {
        out->mode = QStringLiteral("4k");
    } else {
        out->mode = QStringLiteral("custom");
    }
    out->width = width;
    out->height = height;
    out->refresh = refresh;
    return true;
}

} // namespace

QString AppSettings::filePath() {
    return appDataDir() + QStringLiteral("/settings.json");
}

AppSettings AppSettings::load() {
    const QString path = filePath();
    if (QFileInfo::exists(path)) {
        bool ok = false;
        AppSettings settings = fromJson(path, &ok);
        if (ok) {
            if (settings.schemaVersion > 1) {
                qWarning() << "settings.json schema version" << settings.schemaVersion
                           << "is newer than this build; unknown fields will be ignored";
            }
            settings.sanitize();
            qInfo() << "Loaded settings from" << path;
            return settings;
        }

        qWarning() << "Could not parse settings file; backing up to" << path + QStringLiteral(".corrupt");
        QFile::remove(path + QStringLiteral(".corrupt"));
        QFile::copy(path, path + QStringLiteral(".corrupt"));
    }

    AppSettings settings;
    settings.migrateLegacy();
    settings.sanitize();
    if (settings.save()) {
        archiveLegacy();
        cleanLegacySettings();
        qInfo() << "Migrated legacy settings to" << path;
    } else {
        qWarning() << "Could not save migrated settings; arguments.txt was left in place";
    }
    return settings;
}

AppSettings AppSettings::fromJson(const QString &path, bool *ok) {
    AppSettings settings;
    *ok = false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open settings file" << path << ":" << file.errorString();
        return settings;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid settings JSON:" << error.errorString();
        return settings;
    }

    const QJsonObject object = document.object();
    settings.schemaVersion = object.value(QStringLiteral("schema_version")).toInt(1);
    settings.displayName =
        object.value(QStringLiteral("display_name")).toString(settings.displayName);
    settings.bleEnabled =
        object.value(QStringLiteral("ble_enabled")).toBool(settings.bleEnabled);
    settings.forceFullscreen =
        object.value(QStringLiteral("force_fullscreen")).toBool(settings.forceFullscreen);
    settings.renderer =
        object.value(QStringLiteral("renderer")).toString(settings.renderer);

    const QJsonObject video = object.value(QStringLiteral("video")).toObject();
    settings.videoCodec =
        video.value(QStringLiteral("codec")).toString(settings.videoCodec);
    const bool hasVideoCodec = video.contains(QStringLiteral("codec"));
    settings.noHold = object.value(QStringLiteral("no_hold")).toBool(settings.noHold);
    settings.serverEnabled =
        object.value(QStringLiteral("server_enabled")).toBool(settings.serverEnabled);

    const QJsonObject diagnostics =
        object.value(QStringLiteral("diagnostics")).toObject();
    settings.diagnosticsEnabled =
        diagnostics.value(QStringLiteral("enabled"))
            .toBool(settings.diagnosticsEnabled);

    const QJsonObject hud = object.value(QStringLiteral("hud")).toObject();
    settings.hudEnabled =
        hud.value(QStringLiteral("enabled")).toBool(settings.hudEnabled);
    settings.hudCorner =
        hud.value(QStringLiteral("corner")).toString(settings.hudCorner);
    settings.hudMarginX =
        hud.value(QStringLiteral("margin_x")).toInt(settings.hudMarginX);
    settings.hudMarginY =
        hud.value(QStringLiteral("margin_y")).toInt(settings.hudMarginY);

    const QJsonObject obs = object.value(QStringLiteral("obs")).toObject();
    settings.obsMode =
        obs.value(QStringLiteral("mode")).toString(settings.obsMode);

    const QJsonObject resolution = object.value(QStringLiteral("resolution")).toObject();
    settings.resolution.mode =
        resolution.value(QStringLiteral("mode")).toString(settings.resolution.mode);
    settings.resolution.width =
        resolution.value(QStringLiteral("width")).toInt(settings.resolution.width);
    settings.resolution.height =
        resolution.value(QStringLiteral("height")).toInt(settings.resolution.height);
    settings.resolution.nativeWidth =
        resolution.value(QStringLiteral("native_width"))
            .toInt(settings.resolution.nativeWidth);
    settings.resolution.nativeHeight =
        resolution.value(QStringLiteral("native_height"))
            .toInt(settings.resolution.nativeHeight);
    settings.resolution.refresh =
        resolution.value(QStringLiteral("refresh")).toInt(settings.resolution.refresh);

    const QJsonObject videoWindow =
        object.value(QStringLiteral("video_window")).toObject();
    settings.videoWindow.remember =
        videoWindow.value(QStringLiteral("remember"))
            .toBool(settings.videoWindow.remember);
    settings.videoWindow.x =
        videoWindow.value(QStringLiteral("x")).toInt(settings.videoWindow.x);
    settings.videoWindow.y =
        videoWindow.value(QStringLiteral("y")).toInt(settings.videoWindow.y);
    settings.videoWindow.width =
        videoWindow.value(QStringLiteral("width")).toInt(settings.videoWindow.width);
    settings.videoWindow.height =
        videoWindow.value(QStringLiteral("height")).toInt(settings.videoWindow.height);
    settings.videoWindow.hasGeometry =
        videoWindow.value(QStringLiteral("has_geometry"))
            .toBool(settings.videoWindow.width > 0
                    && settings.videoWindow.height > 0);

    const QJsonArray extra = object.value(QStringLiteral("extra_args")).toArray();
    settings.extraArgs.clear();
    for (const QJsonValue &value : extra) {
        if (value.isString()) {
            settings.extraArgs << value.toString();
        }
    }

    if (!hasVideoCodec) {
        // Preserve pre-video.codec behavior: -h265 in extra_args meant HEVC.
        settings.videoCodec =
            settings.extraArgs.contains(QStringLiteral("-h265"))
                ? QStringLiteral("hevc")
                : QStringLiteral("h264");
    }

    *ok = true;
    return settings;
}

bool AppSettings::save() const {
    const QString path = filePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "Could not create settings directory for" << path;
        return false;
    }

    QJsonObject resolutionObject;
    resolutionObject[QStringLiteral("mode")] = resolution.mode;
    resolutionObject[QStringLiteral("width")] = resolution.width;
    resolutionObject[QStringLiteral("height")] = resolution.height;
    resolutionObject[QStringLiteral("native_width")] = resolution.nativeWidth;
    resolutionObject[QStringLiteral("native_height")] = resolution.nativeHeight;
    resolutionObject[QStringLiteral("refresh")] = resolution.refresh;

    QJsonObject obsObject;
    obsObject[QStringLiteral("mode")] = obsMode;

    QJsonObject videoObject;
    videoObject[QStringLiteral("codec")] = videoCodec;

    QJsonObject videoWindowObject;
    videoWindowObject[QStringLiteral("remember")] = videoWindow.remember;
    videoWindowObject[QStringLiteral("has_geometry")] = videoWindow.hasGeometry;
    videoWindowObject[QStringLiteral("x")] = videoWindow.x;
    videoWindowObject[QStringLiteral("y")] = videoWindow.y;
    videoWindowObject[QStringLiteral("width")] = videoWindow.width;
    videoWindowObject[QStringLiteral("height")] = videoWindow.height;

    QJsonObject diagnosticsObject;
    diagnosticsObject[QStringLiteral("enabled")] = diagnosticsEnabled;

    QJsonObject hudObject;
    hudObject[QStringLiteral("enabled")] = hudEnabled;
    hudObject[QStringLiteral("corner")] = hudCorner;
    hudObject[QStringLiteral("margin_x")] = hudMarginX;
    hudObject[QStringLiteral("margin_y")] = hudMarginY;

    QJsonArray extra;
    for (const QString &arg : extraArgs) {
        extra.append(arg);
    }

    QJsonObject object;
    object[QStringLiteral("schema_version")] = 1;
    object[QStringLiteral("display_name")] = displayName;
    object[QStringLiteral("ble_enabled")] = bleEnabled;
    object[QStringLiteral("force_fullscreen")] = forceFullscreen;
    object[QStringLiteral("renderer")] = renderer;
    object[QStringLiteral("resolution")] = resolutionObject;
    object[QStringLiteral("no_hold")] = noHold;
    object[QStringLiteral("server_enabled")] = serverEnabled;
    object[QStringLiteral("obs")] = obsObject;
    object[QStringLiteral("video")] = videoObject;
    object[QStringLiteral("video_window")] = videoWindowObject;
    object[QStringLiteral("diagnostics")] = diagnosticsObject;
    object[QStringLiteral("hud")] = hudObject;
    object[QStringLiteral("extra_args")] = extra;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not write settings file" << path << ":" << file.errorString();
        return false;
    }

    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning() << "Could not commit settings file" << path << ":" << file.errorString();
        return false;
    }

    qDebug() << "Saved settings to" << path;
    return true;
}

QStringList AppSettings::engineArguments(const QString &bleFilePath, bool engineDebug) const {
    QStringList args = stripManagedArgs(extraArgs);

    if (videoCodec == QStringLiteral("hevc")) {
        args << QStringLiteral("-h265");
    }

    QString name = displayName.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("uxplay-windows");
    }
    args << QStringLiteral("-n") << name;

    if (noHold) {
        args << QStringLiteral("-nh");
    }
    if (forceFullscreen) {
        args << QStringLiteral("-fs");
    }

    if (renderer == QStringLiteral("d3d11")) {
        args << QStringLiteral("-vs") << QStringLiteral("d3d11videosink");
    } else if (renderer == QStringLiteral("d3d12")) {
        args << QStringLiteral("-vs") << QStringLiteral("d3d12videosink");
    }

    if (resolution.mode != QStringLiteral("auto")) {
        int width = resolution.width;
        int height = resolution.height;
        if (resolution.mode == QStringLiteral("native")) {
            width = resolution.nativeWidth;
            height = resolution.nativeHeight;
        } else if (resolution.mode == QStringLiteral("720p")) {
            width = 1280;
            height = 720;
        } else if (resolution.mode == QStringLiteral("1080p")) {
            width = 1920;
            height = 1080;
        } else if (resolution.mode == QStringLiteral("1440p")) {
            width = 2560;
            height = 1440;
        } else if (resolution.mode == QStringLiteral("4k")) {
            width = 3840;
            height = 2160;
        }
        width = qBound(1, width, 9999);
        height = qBound(1, height, 9999);
        const int refresh = qBound(1, resolution.refresh, 255);
        args << QStringLiteral("-s")
             << QStringLiteral("%1x%2@%3").arg(width).arg(height).arg(refresh);
    }

    if (bleEnabled && !bleFilePath.isEmpty()) {
        args << QStringLiteral("-ble") << bleFilePath;
    }
    if (engineDebug) {
        args << QStringLiteral("-d");
    }

    return args;
}

QStringList AppSettings::stripManagedArgs(const QStringList &args) {
    QStringList result;
    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == QStringLiteral("-n") || arg == QStringLiteral("-s")
            || arg == QStringLiteral("-vs") || arg == QStringLiteral("-ble")) {
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-'))) {
                ++i;
            }
            continue;
        }
        if (arg == QStringLiteral("-fs") || arg == QStringLiteral("-nh")
            || arg == QStringLiteral("-d") || arg == QStringLiteral("-h265")) {
            continue;
        }
        result << arg;
    }
    return result;
}

void AppSettings::migrateLegacy() {
    QSettings legacy;
    displayName = legacy.value(QStringLiteral("display_name")).toString().trimmed();
    bleEnabled = legacy.value(QStringLiteral("ble_enabled"), true).toBool();
    forceFullscreen = legacy.value(QStringLiteral("force_fs_enabled"), false).toBool();
    renderer = legacy.value(QStringLiteral("renderer_mode"), QStringLiteral("auto")).toString();
    resolution.mode =
        legacy.value(QStringLiteral("resolution_mode"), QStringLiteral("native")).toString();
    resolution.width = legacy.value(QStringLiteral("resolution_width"), 2360).toInt();
    resolution.height = legacy.value(QStringLiteral("resolution_height"), 1640).toInt();
    resolution.refresh = legacy.value(QStringLiteral("resolution_refresh"), 60).toInt();
    serverEnabled = legacy.value(QStringLiteral("server_enabled"), true).toBool();

    const QString legacyPath = legacyArgumentsPath();
    QStringList legacyArgs;
    QFile file(legacyPath);
    const bool legacyFound = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (legacyFound) {
        legacyArgs = QProcess::splitCommand(QString::fromUtf8(file.readAll()).trimmed());
        file.close();
    }

    bool haveName = false;
    bool haveResolution = false;
    bool haveRenderer = false;
    bool haveFullscreen = false;
    bool haveBle = false;
    bool haveH265 = false;
    QString nameFromArgs;
    QString rendererFromArgs;
    ResolutionSettings resolutionFromArgs;
    QStringList extra;

    for (int i = 0; i < legacyArgs.size(); ++i) {
        const QString arg = legacyArgs.at(i);
        auto takeValue = [&](QString *value) {
            if (i + 1 < legacyArgs.size()
                && !legacyArgs.at(i + 1).startsWith(QLatin1Char('-'))) {
                *value = legacyArgs.at(++i);
                return true;
            }
            return false;
        };

        if (arg == QStringLiteral("-n")) {
            QString value;
            if (takeValue(&value)) {
                nameFromArgs = value;
                haveName = true;
            }
        } else if (arg == QStringLiteral("-s")) {
            QString value;
            if (takeValue(&value) && parseResolution(value, &resolutionFromArgs)) {
                haveResolution = true;
            }
        } else if (arg == QStringLiteral("-vs")) {
            QString value;
            if (takeValue(&value)) {
                rendererFromArgs = value;
                haveRenderer = true;
            }
        } else if (arg == QStringLiteral("-fs")) {
            haveFullscreen = true;
        } else if (arg == QStringLiteral("-ble")) {
            QString value;
            if (takeValue(&value)) {
                haveBle = true;
            }
        } else if (arg == QStringLiteral("-nh")) {
            // The old GUI always added -nh; keep that behavior.
        } else if (arg == QStringLiteral("-h265")) {
            haveH265 = true;
        } else if (arg == QStringLiteral("-d")) {
            // Engine debug is controlled by --engine-debug, not persisted.
        } else {
            extra << arg;
        }
    }

    if (displayName.isEmpty() && haveName) {
        displayName = nameFromArgs;
    }
    if (!legacy.contains(QStringLiteral("resolution_mode")) && haveResolution) {
        resolution = resolutionFromArgs;
    }
    if (!legacy.contains(QStringLiteral("renderer_mode")) && haveRenderer) {
        if (rendererFromArgs == QStringLiteral("d3d11videosink")) {
            renderer = QStringLiteral("d3d11");
        } else if (rendererFromArgs == QStringLiteral("d3d12videosink")) {
            renderer = QStringLiteral("d3d12");
        }
    }
    if (!legacy.contains(QStringLiteral("force_fs_enabled")) && haveFullscreen) {
        forceFullscreen = true;
    }
    if (!legacy.contains(QStringLiteral("ble_enabled")) && haveBle) {
        bleEnabled = true;
    }

    videoCodec = haveH265 ? QStringLiteral("hevc") : QStringLiteral("h264");
    if (!legacyFound) {
        videoCodec = QStringLiteral("hevc");
    }

    noHold = true;
    extraArgs = legacyFound ? extra
                            : QStringList{QStringLiteral("-h265"),
                                          QStringLiteral("-fps"),
                                          QStringLiteral("60")};
}

void AppSettings::sanitize() {
    displayName = displayName.trimmed();
    if (displayName.isEmpty()) {
        displayName = QStringLiteral("uxplay-windows");
    }

    if (renderer != QStringLiteral("d3d11") && renderer != QStringLiteral("d3d12")) {
        renderer = QStringLiteral("auto");
    }

    if (videoCodec != QStringLiteral("hevc")
        && videoCodec != QStringLiteral("h264")) {
        videoCodec = QStringLiteral("hevc");
    }

    if (obsMode == QStringLiteral("camera")) {
        qWarning() << "obs.mode=camera is not implemented yet; using off";
        obsMode = QStringLiteral("off");
    }
    if (obsMode != QStringLiteral("off") && obsMode != QStringLiteral("window")) {
        obsMode = QStringLiteral("off");
    }

    if (resolution.mode == QStringLiteral("auto")) {
        resolution.mode = QStringLiteral("native");
    }
    if (resolution.mode != QStringLiteral("native")
        && resolution.mode != QStringLiteral("720p")
        && resolution.mode != QStringLiteral("1080p")
        && resolution.mode != QStringLiteral("1440p")
        && resolution.mode != QStringLiteral("4k")
        && resolution.mode != QStringLiteral("custom")) {
        resolution.mode = QStringLiteral("native");
    }

    if (resolution.mode == QStringLiteral("720p")) {
        resolution.width = 1280;
        resolution.height = 720;
    } else if (resolution.mode == QStringLiteral("1080p")) {
        resolution.width = 1920;
        resolution.height = 1080;
    } else if (resolution.mode == QStringLiteral("1440p")) {
        resolution.width = 2560;
        resolution.height = 1440;
    } else if (resolution.mode == QStringLiteral("4k")) {
        resolution.width = 3840;
        resolution.height = 2160;
    }
    resolution.width = qBound(1, resolution.width, 9999);
    resolution.height = qBound(1, resolution.height, 9999);
    resolution.nativeWidth = qBound(1, resolution.nativeWidth, 9999);
    resolution.nativeHeight = qBound(1, resolution.nativeHeight, 9999);
    resolution.refresh = qBound(1, resolution.refresh, 255);

    extraArgs = stripManagedArgs(extraArgs);

    videoWindow.width = qBound(0, videoWindow.width, 10000);
    videoWindow.height = qBound(0, videoWindow.height, 10000);
    if (videoWindow.width <= 0 || videoWindow.height <= 0) {
        videoWindow.hasGeometry = false;
    }

    if (hudCorner != QStringLiteral("top-left")
        && hudCorner != QStringLiteral("top-right")
        && hudCorner != QStringLiteral("bottom-left")
        && hudCorner != QStringLiteral("bottom-right")) {
        hudCorner = QStringLiteral("top-left");
    }
    hudMarginX = qBound(0, hudMarginX, 1000);
    hudMarginY = qBound(0, hudMarginY, 1000);
}

void AppSettings::archiveLegacy() {
    const QString legacyPath = legacyArgumentsPath();
    if (!QFileInfo::exists(legacyPath)) {
        return;
    }

    const QString backupPath = appDataDir() + QStringLiteral("/arguments.txt.migrated");
    QFile::remove(backupPath);
    if (QFile::rename(legacyPath, backupPath)) {
        qInfo() << "Archived legacy arguments to" << backupPath;
    } else {
        qWarning() << "Could not archive legacy arguments file" << legacyPath;
    }
}

void AppSettings::cleanLegacySettings() {
    QSettings legacy;
    const QStringList keys = {
        QStringLiteral("display_name"),
        QStringLiteral("ble_enabled"),
        QStringLiteral("force_fs_enabled"),
        QStringLiteral("renderer_mode"),
        QStringLiteral("resolution_mode"),
        QStringLiteral("resolution_width"),
        QStringLiteral("resolution_height"),
        QStringLiteral("resolution_refresh"),
        QStringLiteral("server_enabled"),
    };
    for (const QString &key : keys) {
        legacy.remove(key);
    }
    legacy.sync();
}
