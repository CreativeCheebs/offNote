#pragma once
#include <QList>
#include <QString>

struct sqlite3;

struct NoteRow {
    qint64 id = 0;
    QString createdAt;
    QString data;
    QString tags;
    QString targets;  // comma-separated connection names still to deliver
    QString error;
    QString source;
    bool processed = false;
};

// The durable note outbox: the same notes table (and WAL + busy timeout settings) as the
// Tauri and WinForms clients, because they all open one %APPDATA%\QuickNote\quicknote.db.
// One Store per thread; a Store is not thread-safe.
class Store {
public:
    Store() = default;
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool open(const QString &path, QString *error);
    bool insert(const QString &createdAt, const QString &data, const QString &tags, const QString &targets,
                const QString &source, QString *error);
    QList<NoteRow> pending();               // processed = 0
    QList<NoteRow> recent(int limit);       // newest first
    int pendingCount();
    bool markProcessed(qint64 id, const QString &processedAt);
    bool markError(qint64 id, const QString &error);
    bool updateTargets(qint64 id, const QString &targets);

private:
    QList<NoteRow> query(const char *sql, int limit);
    sqlite3 *db = nullptr;
};
