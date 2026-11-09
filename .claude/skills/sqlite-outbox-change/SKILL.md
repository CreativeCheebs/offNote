---
name: sqlite-outbox-change
description: Change the shared SQLite outbox or config schema safely.
---

# Sqlite Outbox Change

The notes table in quicknote.db and config.yaml are shared by the Qt, Tauri, WinForms and Compose clients (Android uses Room separately). Inspect store.cpp, src-tauri/src/store.rs and winforms-quicknote/NoteStore.cs before changing persistence. Preserve queued and failed notes with forward migrations and tests, keep the DDL byte-compatible, and never test against the real database.
