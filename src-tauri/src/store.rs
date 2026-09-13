// Durable note store backed by SQLite.
//
// Every note is written here *before* delivery to any connector, so nothing is
// lost when a connection is unreachable. Rows are never deleted; a failed
// delivery simply stays at `processed = 0` as the backlog.
//
// IMPORTANT: this schema and the PRAGMAs below are a shared contract with the
// C# app (winforms-quicknote/NoteStore.cs). Both apps open the SAME database
// file (%APPDATA%\QuickNote\quicknote.db), so the DDL must stay byte-compatible
// and both must use WAL + a busy timeout to survive concurrent access.

use std::path::Path;
use std::time::Duration;

use rusqlite::{params, Connection};

pub const SCHEMA: &str = "CREATE TABLE IF NOT EXISTS notes (\
    id           INTEGER PRIMARY KEY AUTOINCREMENT,\
    created_at   TEXT    NOT NULL,\
    data         TEXT    NOT NULL,\
    tags         TEXT,\
    targets      TEXT,\
    processed    INTEGER NOT NULL DEFAULT 0,\
    processed_at TEXT,\
    error        TEXT,\
    source       TEXT\
);";

/// Open (creating if needed) the note database, enabling WAL + busy timeout and
/// ensuring the schema exists.
pub fn open(db_path: &Path) -> Result<Connection, String> {
    if let Some(parent) = db_path.parent() {
        std::fs::create_dir_all(parent)
            .map_err(|e| format!("failed to create db dir {}: {e}", parent.display()))?;
    }
    let conn = Connection::open(db_path)
        .map_err(|e| format!("failed to open db {}: {e}", db_path.display()))?;
    // WAL lets the two tray apps read/write the shared file concurrently; the
    // busy timeout makes a briefly-locked write wait instead of failing.
    conn.busy_timeout(Duration::from_secs(5))
        .map_err(|e| format!("failed to set busy_timeout: {e}"))?;
    conn.pragma_update(None, "journal_mode", "WAL")
        .map_err(|e| format!("failed to enable WAL: {e}"))?;
    conn.execute_batch(SCHEMA)
        .map_err(|e| format!("failed to ensure schema: {e}"))?;
    Ok(conn)
}

/// Insert a new, not-yet-delivered note. Returns its row id.
pub fn insert(
    conn: &Connection,
    created_at: &str,
    data: &str,
    tags: &str,
    targets: &str,
    source: &str,
) -> Result<i64, String> {
    conn.execute(
        "INSERT INTO notes (created_at, data, tags, targets, processed, source) \
         VALUES (?1, ?2, ?3, ?4, 0, ?5)",
        params![created_at, data, tags, targets, source],
    )
    .map_err(|e| format!("failed to insert note: {e}"))?;
    Ok(conn.last_insert_rowid())
}

/// Mark a note delivered to all its targets.
pub fn mark_processed(conn: &Connection, id: i64, processed_at: &str) -> Result<(), String> {
    conn.execute(
        "UPDATE notes SET processed = 1, processed_at = ?1, error = NULL WHERE id = ?2",
        params![processed_at, id],
    )
    .map_err(|e| format!("failed to mark processed: {e}"))?;
    Ok(())
}

/// Record a delivery failure; the note stays at processed = 0 (backlog).
pub fn mark_error(conn: &Connection, id: i64, error: &str) -> Result<(), String> {
    conn.execute(
        "UPDATE notes SET error = ?1 WHERE id = ?2",
        params![error, id],
    )
    .map_err(|e| format!("failed to record error: {e}"))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    // Deterministic path shared with the C# cross-app verification harness.
    fn xapp_db() -> std::path::PathBuf {
        std::env::temp_dir().join("qn_xapp.db")
    }

    fn count_source(conn: &Connection, source: &str) -> i64 {
        conn.query_row(
            "SELECT COUNT(*) FROM notes WHERE source = ?1",
            params![source],
            |r| r.get(0),
        )
        .unwrap()
    }

    #[test]
    fn store_roundtrip_across_connections() {
        let path = std::env::temp_dir().join("qn_roundtrip.db");
        let _ = std::fs::remove_file(&path);

        let conn = open(&path).unwrap();
        let id = insert(&conn, "2026-09-13T00:00:00Z", "hello #work", "work", "personal", "tauri").unwrap();
        mark_processed(&conn, id, "2026-09-13T00:00:01Z").unwrap();
        drop(conn);

        // Reopen with a fresh connection: the row must persist with our fields.
        let conn2 = open(&path).unwrap();
        let (data, tags, processed, source): (String, String, i64, String) = conn2
            .query_row(
                "SELECT data, tags, processed, source FROM notes WHERE id = ?1",
                params![id],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?, r.get(3)?)),
            )
            .unwrap();
        assert_eq!(data, "hello #work");
        assert_eq!(tags, "work");
        assert_eq!(processed, 1);
        assert_eq!(source, "tauri");
    }

    // Step 1 of the cross-app check: Rust creates the shared DB and writes a
    // 'tauri' row. The C# harness then reads it and appends a 'winforms' row;
    // `cross_app_rust_reads_csharp` (run with --ignored, after the harness)
    // confirms Rust reads the C#-written row back.
    #[test]
    fn cross_app_rust_writes() {
        let path = xapp_db();
        let _ = std::fs::remove_file(&path);
        let _ = std::fs::remove_file(path.with_extension("db-wal"));
        let _ = std::fs::remove_file(path.with_extension("db-shm"));
        let conn = open(&path).unwrap();
        let id = insert(&conn, "2026-09-13T00:00:00Z", "row from rust", "", "personal", "tauri").unwrap();
        mark_processed(&conn, id, "2026-09-13T00:00:01Z").unwrap();
        assert_eq!(count_source(&conn, "tauri"), 1);
    }

    #[test]
    #[ignore = "run after the C# cross-app harness has written its row"]
    fn cross_app_rust_reads_csharp() {
        let conn = open(&xapp_db()).unwrap();
        assert_eq!(count_source(&conn, "tauri"), 1, "Rust's own row should still be there");
        assert_eq!(
            count_source(&conn, "winforms"),
            1,
            "Rust should be able to read the row C# wrote to the shared file"
        );
    }
}
