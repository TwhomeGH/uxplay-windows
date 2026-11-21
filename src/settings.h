#pragma once

#include <QString>
#include <QStringList>

struct ResolutionSettings {
    QString mode = "native";
    int width = 2360;
    int height = 1640;
    int nativeWidth = 2360;
    int nativeHeight = 1640;
    int refresh = 60;
};

struct VideoWindowSettings {
    bool remember = true;
    bool alwaysOnTop = false;
    bool hasGeometry = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class AppSettings {
public:
    static AppSettings load();
    static QString filePath();

    /// 將完整 JSON 設定原子寫入目前 AppData 路徑；內容相同時不寫入。
    /// @return 未變更或提交成功為 true；失敗則保留原檔並回傳 false。
    /// @note 呼叫端需序列化儲存要求，不再以直接截斷覆寫作為備援。
    bool save() const;
    QStringList engineArguments(const QString &bleFilePath, bool engineDebug) const;
    int requestedWidth() const;
    int requestedHeight() const;

    int schemaVersion = 1;
    QString displayName = "uxplay-windows";
    QString language = "system";
    bool bleEnabled = true;
    bool forceFullscreen = false;
    QString renderer = "auto";
    QString videoCodec = "h264";
    QString videoPacing = "arrival";
    QString audioPacing = "airplay";
    ResolutionSettings resolution;
    VideoWindowSettings videoWindow;
    bool noHold = true;
    bool serverEnabled = true;
    bool diagnosticsEnabled = true;
    bool virtualCameraEnabled = false;
    int gpuAdapterIndex = -1;
    bool hudEnabled = false;
    /// HUD 顯示模式：window 為獨立視窗，overlay 為影片 owner 的舊原生疊層。
    QString hudMode = "window";
    QString hudCorner = "top-left";
    int hudMarginX = 8;
    int hudMarginY = 72;
    QStringList hudFields = {
        "client", "status", "video", "output",
        "decoder", "renderer", "fps", "audio"};
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
