#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <sqlite3.h>

namespace {

// Byte-compatible with src-tauri/src/store.rs and winforms-quicknote/NoteStore.cs.
const char kSchema[] =
    "CREATE TABLE IF NOT EXISTS notes ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "created_at TEXT NOT NULL,"
    "data TEXT NOT NULL,"
    "tags TEXT,"
    "targets TEXT,"
    "processed INTEGER NOT NULL DEFAULT 0,"
    "processed_at TEXT,"
    "error TEXT,"
    "source TEXT);";

QString text(sqlite3_stmt *stmt, int col) {
    const unsigned char *t = sqlite3_column_text(stmt, col);
    return t ? QString::fromUtf8(reinterpret_cast<const char *>(t)) : QString();
}

bool run(sqlite3 *db, const char *sql, const QList<QString> &args) {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    QList<QByteArray> bytes;
    for (const QString &a : args) bytes.append(a.toUtf8());
    for (int i = 0; i < bytes.size(); ++i) sqlite3_bind_text(stmt, i + 1, bytes[i].constData(), bytes[i].size(), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

}  // namespace

Store::~Store() {
    if (db) sqlite3_close(db);
}

bool Store::open(const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    int rc = sqlite3_open(QFile::encodeName(path), &db);
    if (rc == SQLITE_OK) {
        sqlite3_busy_timeout(db, 5000);
        rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    }
    if (rc == SQLITE_OK) rc = sqlite3_exec(db, kSchema, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = db ? QString::fromUtf8(sqlite3_errmsg(db)) : QStringLiteral("could not open the note database");
        if (db) sqlite3_close(db);
        db = nullptr;
        return false;
    }
    return true;
}

bool Store::insert(const QString &createdAt, const QString &data, const QString &tags, const QString &targets,
                   const QString &source, QString *error) {
    const bool ok = run(db,
                        "INSERT INTO notes (created_at,data,tags,targets,processed,source) VALUES (?1,?2,?3,?4,0,?5)",
                        {createdAt, data, tags, targets, source});
    if (!ok && error) *error = QString::fromUtf8(sqlite3_errmsg(db));
    return ok;
}

QList<NoteRow> Store::query(const char *sql, int limit) {
    QList<NoteRow> rows;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return rows;
    if (limit > 0) sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        NoteRow r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.createdAt = text(stmt, 1);
        r.data = text(stmt, 2);
        r.tags = text(stmt, 3);
        r.targets = text(stmt, 4);
        r.processed = sqlite3_column_int(stmt, 5) != 0;
        r.error = text(stmt, 6);
        r.source = text(stmt, 7);
        rows.append(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

QList<NoteRow> Store::pending() {
    return query("SELECT id,created_at,data,tags,targets,processed,error,source FROM notes WHERE processed = 0 ORDER BY id", 0);
}

QList<NoteRow> Store::recent(int limit) {
    return query("SELECT id,created_at,data,tags,targets,processed,error,source FROM notes ORDER BY id DESC LIMIT ?1", limit);
}

int Store::pendingCount() {
    sqlite3_stmt *stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM notes WHERE processed = 0", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool Store::markProcessed(qint64 id, const QString &processedAt) {
    return run(db, "UPDATE notes SET processed = 1, processed_at = ?1, error = NULL WHERE id = ?2",
               {processedAt, QString::number(id)});
}

bool Store::markError(qint64 id, const QString &error) {
    return run(db, "UPDATE notes SET error = ?1 WHERE id = ?2", {error, QString::number(id)});
}

bool Store::updateTargets(qint64 id, const QString &targets) {
    return run(db, "UPDATE notes SET targets = ?1 WHERE id = ?2", {targets, QString::number(id)});
}
