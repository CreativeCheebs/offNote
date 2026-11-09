# Offnote engineering rules

Offnote captures notes on desktop and Android, writes each note to a durable local outbox, then routes by `#tag` to markdown or an AFFiNE sidecar. Read `README.md` and the affected module before editing.

## Architecture

- Qt 6 Widgets (`qt-offnote/`) is the only desktop client. Keep its `config.yaml` format and `quicknote.db` schema stable so existing user data keeps working; the Android app uses the same config shape.
- Keep Android platform APIs in Android code: Room, WorkManager, notifications, foreground service, encrypted preferences, and lockscreen activity.
- Keep desktop platform APIs (Win32 hotkey, WinHTTP, registry) in `qt-offnote/`.
- Do not replace Room or desktop SQLite without a tested migration preserving queued and failed notes.
- Sidecar credentials stay server-side. Clients store only sidecar URL, token, and connector name. Never log tokens or note contents in diagnostics.

## Quality

- Preserve fast capture, blank-note handling, keyboard submission, status and retry visibility, accessibility labels, light/dark themes, and error feedback.
- Test pure logic at the common/JVM layer where possible. Run affected Android Gradle tasks, desktop checks, and connector checks only when relevant. Inspect scripts before invoking anything that contacts a live sidecar.
- Android builds require an SDK; device behavior requires a device or emulator. Do not claim an iOS, desktop visual, or device check without running it.
- Review diffs for unrelated edits, generated files, secrets, and behavior or data migration gaps. Update `README.md` when setup or behavior changes.
