#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Same shape and field names as %APPDATA%\QuickNote\config.yaml, which the Tauri,
// WinForms, Compose and Android clients also read. Keep them compatible.
struct Connection {
    QString name;
    QString type = "markdown";  // markdown | obsidian | logseq | affine
    QString path;               // file-based kinds
    QString sidecarUrl;         // affine only; credentials stay in the sidecar itself
    QString sidecarToken;
    QString sidecarConnector;
};

struct TagRoute {
    QString tag;
    QString connection;
};

struct AppConfig {
    QString toggleNote = "Alt+Space";
    QString saveNote = "Alt+Enter";
    QList<Connection> connections;
    QList<TagRoute> tagRoutes;  // tag -> connection name, in file order
    QString defaultConnection;

    const Connection *find(const QString &name) const;
};

QStringList connectionTypes();

// %APPDATA%\QuickNote (read from the environment on every call).
QString dataDir();
QString configPath();
QString dbPath();
// Absolute paths are used as-is; relative ones live under dataDir().
QString resolveDir(const QString &path);

bool parseConfig(const QByteArray &yaml, AppConfig &out, QString *error);
QByteArray serializeConfig(const AppConfig &config);
// Loads config.yaml, seeding a default one on first run (never overwrites an existing file).
bool loadConfig(AppConfig &out, QString *error);
// Atomic write (temp file + rename); keeps a one-time config.yaml.bak-qt of the previous file.
bool saveConfig(const AppConfig &config, QString *error);

// Empty string when valid, otherwise a short reason.
QString validateConnection(const Connection &c);
QStringList validateConfig(const AppConfig &config);
