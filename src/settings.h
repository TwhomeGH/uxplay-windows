#pragma once

#include <QString>
#include <QStringList>

struct ResolutionSettings {
    QString mode = "native";
    int width = 2360;
    int height = 1640;
    int refresh = 60;
};

class AppSettings {
public:
    static AppSettings load();
    static QString filePath();

    bool save() const;
    QStringList engineArguments(const QString &bleFilePath, bool engineDebug) const;

    int schemaVersion = 1;
    QString displayName = "uxplay-windows";
    bool bleEnabled = true;
    bool forceFullscreen = false;
    QString renderer = "auto";
    QString videoCodec = "hevc";
    ResolutionSettings resolution;
    bool noHold = true;
    bool serverEnabled = true;
    bool diagnosticsEnabled = true;
    QString obsMode = "off";
    QStringList extraArgs;

private:
    static AppSettings fromJson(const QString &path, bool *ok);
    static QStringList stripManagedArgs(const QStringList &args);
    static void archiveLegacy();
    static void cleanLegacySettings();

    void migrateLegacy();
    void sanitize();
};
