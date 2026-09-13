using Microsoft.Data.Sqlite;

namespace QuickNote;

/// Durable note store backed by SQLite. Every note is written here *before*
/// delivery to any connector, so nothing is lost when a connection is
/// unreachable; rows are never deleted, a failed delivery just stays at
/// processed = 0 as backlog.
///
/// IMPORTANT: the column set and PRAGMAs mirror the Rust store
/// (src-tauri/src/store.rs). Both apps open the SAME file (AppPaths.DbPath), so
/// the columns must stay compatible and both must use WAL + a busy timeout.
public static class NoteStore
{
    public const string Schema =
        "CREATE TABLE IF NOT EXISTS notes (" +
        "id INTEGER PRIMARY KEY AUTOINCREMENT," +
        "created_at TEXT NOT NULL," +
        "data TEXT NOT NULL," +
        "tags TEXT," +
        "targets TEXT," +
        "processed INTEGER NOT NULL DEFAULT 0," +
        "processed_at TEXT," +
        "error TEXT," +
        "source TEXT" +
        ");";

    public static SqliteConnection Open(string dbPath)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(dbPath)!);
        var conn = new SqliteConnection($"Data Source={dbPath}");
        conn.Open();
        // WAL + busy timeout so the two tray apps can share the file safely.
        Exec(conn, "PRAGMA busy_timeout=5000;");
        Exec(conn, "PRAGMA journal_mode=WAL;");
        Exec(conn, Schema);
        return conn;
    }

    private static void Exec(SqliteConnection conn, string sql)
    {
        using var cmd = conn.CreateCommand();
        cmd.CommandText = sql;
        cmd.ExecuteNonQuery();
    }

    /// Insert a new, not-yet-delivered note. Returns its row id.
    public static long Insert(SqliteConnection conn, string createdAt, string data,
        string tags, string targets, string source)
    {
        using var cmd = conn.CreateCommand();
        cmd.CommandText =
            "INSERT INTO notes (created_at, data, tags, targets, processed, source) " +
            "VALUES ($created, $data, $tags, $targets, 0, $source); " +
            "SELECT last_insert_rowid();";
        cmd.Parameters.AddWithValue("$created", createdAt);
        cmd.Parameters.AddWithValue("$data", data);
        cmd.Parameters.AddWithValue("$tags", tags);
        cmd.Parameters.AddWithValue("$targets", targets);
        cmd.Parameters.AddWithValue("$source", source);
        return (long)cmd.ExecuteScalar()!;
    }

    public static void MarkProcessed(SqliteConnection conn, long id, string processedAt)
    {
        using var cmd = conn.CreateCommand();
        cmd.CommandText = "UPDATE notes SET processed = 1, processed_at = $at, error = NULL WHERE id = $id;";
        cmd.Parameters.AddWithValue("$at", processedAt);
        cmd.Parameters.AddWithValue("$id", id);
        cmd.ExecuteNonQuery();
    }

    public static void MarkError(SqliteConnection conn, long id, string error)
    {
        using var cmd = conn.CreateCommand();
        cmd.CommandText = "UPDATE notes SET error = $err WHERE id = $id;";
        cmd.Parameters.AddWithValue("$err", error);
        cmd.Parameters.AddWithValue("$id", id);
        cmd.ExecuteNonQuery();
    }
}
