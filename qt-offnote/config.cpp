#include "config.h"

#include "shortcut.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <yaml-cpp/yaml.h>

namespace {

const char kSeedConfig[] =
    "# QuickNote configuration (live)\n"
    "#\n"
    "# Shared by the Offnote desktop and Android clients. Edit it in Offnote's\n"
    "# Settings window, or by hand and use the tray's \"Reload config\".\n"
    "shortcuts:\n"
    "  toggle_note: \"Alt+Space\"\n"
    "  save_note: \"Alt+Enter\"\n"
    "\n"
    "connections:\n"
    "  - name: personal\n"
    "    type: markdown\n"
    "    path: \"notes\"\n"
    "\n"
    "routing:\n"
    "  tags: {}\n"
    "  default: personal\n";

QString scalar(const YAML::Node &n) {
    return (n && n.IsScalar()) ? QString::fromStdString(n.Scalar()) : QString();
}

void emitValue(YAML::Emitter &out, const QString &value) {
    out << YAML::DoubleQuoted << value.toStdString();
}

void emitField(YAML::Emitter &out, const char *key, const QString &value) {
    out << YAML::Key << key << YAML::Value;
    emitValue(out, value);
}

}  // namespace

const Connection *AppConfig::find(const QString &name) const {
    for (const Connection &c : connections)
        if (c.name == name) return &c;
    return nullptr;
}

QStringList connectionTypes() { return {"markdown", "obsidian", "logseq", "affine"}; }

QString dataDir() {
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/QuickNote";
}

QString configPath() { return dataDir() + "/config.yaml"; }
QString dbPath() { return dataDir() + "/quicknote.db"; }

QString resolveDir(const QString &path) {
    return QDir::isAbsolutePath(path) ? path : dataDir() + "/" + path;
}

bool parseConfig(const QByteArray &yaml, AppConfig &out, QString *error) {
    try {
        const YAML::Node root = YAML::Load(std::string(yaml.constData(), static_cast<size_t>(yaml.size())));
        AppConfig cfg;
        if (root.IsNull() || !root.IsDefined()) { out = cfg; return true; }
        if (!root.IsMap()) throw YAML::Exception(YAML::Mark::null_mark(), "top level must be a mapping");

        const YAML::Node shortcuts = root["shortcuts"];
        if (shortcuts.IsDefined() && shortcuts.IsMap()) {
            const QString toggle = scalar(shortcuts["toggle_note"]);
            const QString save = scalar(shortcuts["save_note"]);
            if (!toggle.isEmpty()) cfg.toggleNote = toggle;
            if (!save.isEmpty()) cfg.saveNote = save;
        }

        const YAML::Node connections = root["connections"];
        if (connections.IsDefined() && connections.IsSequence()) {
            for (const YAML::Node &item : connections) {
                if (!item.IsMap()) continue;
                Connection c;
                c.name = scalar(item["name"]);
                c.type = scalar(item["type"]);
                c.path = scalar(item["path"]);
                c.sidecarUrl = scalar(item["sidecar_url"]);
                c.sidecarToken = scalar(item["sidecar_token"]);
                c.sidecarConnector = scalar(item["sidecar_connector"]);
                cfg.connections.append(c);
            }
        }

        const YAML::Node routing = root["routing"];
        if (routing.IsDefined() && routing.IsMap()) {
            cfg.defaultConnection = scalar(routing["default"]);
            const YAML::Node tags = routing["tags"];
            if (tags.IsDefined() && tags.IsMap()) {
                for (const auto &entry : tags)
                    cfg.tagRoutes.append({scalar(entry.first), scalar(entry.second)});
            }
        }
        out = cfg;
        return true;
    } catch (const YAML::Exception &e) {
        // e.what() names a line/column but never echoes values, so tokens stay out of messages.
        if (error) *error = QStringLiteral("could not parse config.yaml: ") + QString::fromUtf8(e.what());
        return false;
    } catch (const std::exception &) {
        if (error) *error = QStringLiteral("could not parse config.yaml");
        return false;
    }
}

QByteArray serializeConfig(const AppConfig &config) {
    YAML::Emitter out;
    out << YAML::Comment("QuickNote configuration (live) - written by the Offnote Settings window.");
    out << YAML::BeginMap;

    out << YAML::Key << "shortcuts" << YAML::Value << YAML::BeginMap;
    emitField(out, "toggle_note", config.toggleNote);
    emitField(out, "save_note", config.saveNote);
    out << YAML::EndMap;

    out << YAML::Key << "connections" << YAML::Value << YAML::BeginSeq;
    for (const Connection &c : config.connections) {
        out << YAML::BeginMap;
        emitField(out, "name", c.name);
        emitField(out, "type", c.type);
        if (c.type == "affine") {
            if (!c.sidecarUrl.isEmpty()) emitField(out, "sidecar_url", c.sidecarUrl);
            if (!c.sidecarToken.isEmpty()) emitField(out, "sidecar_token", c.sidecarToken);
            if (!c.sidecarConnector.isEmpty()) emitField(out, "sidecar_connector", c.sidecarConnector);
        } else if (!c.path.isEmpty()) {
            emitField(out, "path", c.path);
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "routing" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "tags" << YAML::Value;
    if (config.tagRoutes.isEmpty()) {
        out << YAML::Flow << YAML::BeginMap << YAML::EndMap;  // "{}": a map, never null
    } else {
        out << YAML::BeginMap;
        for (const TagRoute &r : config.tagRoutes) {
            out << YAML::Key;
            emitValue(out, r.tag);
            out << YAML::Value;
            emitValue(out, r.connection);
        }
        out << YAML::EndMap;
    }
    emitField(out, "default", config.defaultConnection);
    out << YAML::EndMap;

    out << YAML::EndMap;
    return QByteArray(out.c_str()) + "\n";
}

bool loadConfig(AppConfig &out, QString *error) {
    const QString path = configPath();
    if (!QFile::exists(path)) {
        QDir().mkpath(dataDir());
        QSaveFile seed(path);
        if (!seed.open(QIODevice::WriteOnly) || seed.write(kSeedConfig) < 0 || !seed.commit()) {
            if (error) *error = QStringLiteral("could not create config.yaml in ") + dataDir();
            return false;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("could not read config.yaml: ") + file.errorString();
        return false;
    }
    if (file.size() > 1024 * 1024) {
        if (error) *error = QStringLiteral("config.yaml is larger than 1 MB");
        return false;
    }
    return parseConfig(file.readAll(), out, error);
}

bool saveConfig(const AppConfig &config, QString *error) {
    const QString path = configPath();
    QDir().mkpath(dataDir());
    const QString backup = path + ".bak-qt";
    if (QFile::exists(path) && !QFile::exists(backup)) QFile::copy(path, backup);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("could not write config.yaml: ") + file.errorString();
        return false;
    }
    file.write(serializeConfig(config));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("could not replace config.yaml: ") + file.errorString();
        return false;
    }
    return true;
}

QString validateConnection(const Connection &c) {
    if (c.name.trimmed().isEmpty()) return "needs a name";
    if (c.name.contains(',')) return "name cannot contain a comma";
    if (c.type == "markdown" || c.type == "obsidian" || c.type == "logseq") {
        if (c.path.trimmed().isEmpty()) return "needs a folder path";
    } else if (c.type == "affine") {
        if (c.sidecarUrl.trimmed().isEmpty() || c.sidecarToken.trimmed().isEmpty() || c.sidecarConnector.trimmed().isEmpty())
            return "needs a sidecar URL, token and connector name";
        const QString url = c.sidecarUrl.trimmed().toLower();
        if (!url.startsWith("http://") && !url.startsWith("https://")) return "needs a sidecar URL starting with http:// or https://";
        // The token goes into an HTTP header: visible ASCII only, so it can never inject a header.
        for (const QChar ch : c.sidecarToken)
            if (ch.unicode() < 0x21 || ch.unicode() > 0x7E) return "has a token with spaces, line breaks or non-ASCII characters";
    } else {
        return QStringLiteral("has unknown type '%1'").arg(c.type);
    }
    return QString();
}

QStringList validateConfig(const AppConfig &config) {
    QStringList errors;
    QSet<QString> names;
    for (const Connection &c : config.connections) {
        const QString why = validateConnection(c);
        const QString label = c.name.trimmed().isEmpty() ? QStringLiteral("(unnamed)") : c.name;
        if (!why.isEmpty()) {
            errors << QStringLiteral("Connection \"%1\" %2.").arg(label, why);
            continue;
        }
        if (names.contains(c.name)) errors << QStringLiteral("Duplicate connection name \"%1\".").arg(c.name);
        names.insert(c.name);
    }
    if (config.defaultConnection.trimmed().isEmpty()) errors << "Pick a default connection.";
    else if (!names.contains(config.defaultConnection)) errors << "The default connection does not match any connection.";

    QSet<QString> tags;
    for (const TagRoute &r : config.tagRoutes) {
        const QString tag = r.tag.trimmed();
        if (tag.isEmpty()) continue;
        if (!names.contains(r.connection)) errors << QStringLiteral("Tag rule \"#%1\" points to a connection that does not exist.").arg(tag);
        if (tags.contains(tag)) errors << QStringLiteral("Tag \"#%1\" is listed twice.").arg(tag);
        tags.insert(tag);
    }

    const Shortcut toggle = parseShortcut(config.toggleNote);
    if (!toggle.valid()) errors << "The open-note shortcut is not a valid key combination.";
    else if (toggle.mods == 0) errors << "The open-note shortcut needs a modifier such as Alt or Ctrl.";
    const Shortcut save = parseShortcut(config.saveNote);
    if (!save.valid()) errors << "The save shortcut is not a valid key combination.";
    else if (save.mods == 0) errors << "The save shortcut needs a modifier such as Alt or Ctrl, or it would block typing.";
    return errors;
}
