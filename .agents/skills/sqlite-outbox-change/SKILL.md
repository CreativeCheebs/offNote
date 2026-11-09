---
name: sqlite-outbox-change
description: Change the shared SQLite outbox or config schema safely.
---

# Sqlite Outbox Change

The notes table in quicknote.db and config.yaml are shared by the Qt client (Android uses Room separately). Inspect store.cpp, the Android Room schema before changing persistence. Preserve queued and failed notes with forward migrations and tests, keep the DDL byte-compatible, and never test against the real database.
