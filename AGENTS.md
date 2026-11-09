# Offnote project guidance

Read `README.md`, `CLAUDE.md`, and relevant build files before changing code. This repository has two clients for one quick capture workflow: the Qt desktop app and the Android app. They do not share a UI or database. The earlier Tauri, WinForms, Avalonia, WinUI, GTK and Compose desktop clients were removed; they remain in git history (commit 84e8caa is the last one containing them).

## Layout

- `android-offnote/app/`: Android app, Room outbox, WorkManager delivery, and lockscreen capture.
- `android-offnote/shared-ui/`: shared Compose Multiplatform Material 3 screens.
- `qt-offnote/`: the chosen Windows desktop client (Qt 6 Widgets, C++): widget-free core (config, router, store, delivery) plus capture and Settings windows; `tests/` has `OffnoteQtTests` and a sidecar stub.
- `connectors/affine-sidecar/`: authenticated HTTP bridge to self-hosted AFFiNE.
- `connectors/setup/`: desktop configuration helper; see its limitation in `README.md`.

## Direction

Qt 6 Widgets is the only desktop client (lowest memory, about 50-60 MB). Its C++ routing and delivery is a port of the removed Tauri client's Rust logic. Keep Android notification, foreground service, Room, WorkManager and encrypted config on Android. No iOS target exists yet.

Saving must persist locally before network delivery. Failed delivery remains retryable. Preserve the `config.yaml` format, routing semantics, sidecar authentication, and existing user data.

## Workflow

Repository skills live in `.agents/skills/`; verify their instructions against actual files. On Windows run Gradle from `android-offnote/` with `gradlew.bat`; use `npm` for the sidecar and `qt-offnote\build.bat` (MSYS2 MinGW, needs qt6-base and yaml-cpp) for Qt. Test Qt against a scratch `APPDATA`. Report exact checks and platform gaps. Never print credentials or commit local config, databases, signing material, or build outputs. Preserve unrelated working-tree edits.
