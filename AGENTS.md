# Offnote project guidance

Read `README.md`, `CLAUDE.md`, and relevant build files before changing code. This repository has several clients for one quick capture workflow; they do not yet share a UI or database.

## Layout

- `android-offnote/app/`: Android app, Room outbox, WorkManager delivery, and lockscreen capture.
- `android-offnote/shared-ui/`: shared Compose Multiplatform Material 3 screens.
- `android-offnote/desktopApp/`: Compose desktop host with SQLite, YAML, routing, and delivery.
- `src/` and `src-tauri/`: Tauri desktop app with SQLite queue, hotkey, tray, routing, and delivery.
- `winforms-quicknote/`: alternative Windows desktop client sharing the desktop config and database.
- `qt-offnote/`: the chosen Windows desktop client (Qt 6 Widgets, C++): widget-free core (config, router, store, delivery) plus capture and Settings windows; `tests/` has `OffnoteQtTests` and a sidecar stub.
- `avalonia-offnote/`: stopped .NET 10 Avalonia prototype reusing WinForms C# code.
- `connectors/affine-sidecar/`: authenticated HTTP bridge to self-hosted AFFiNE.
- `connectors/setup/`: desktop configuration helper; see its limitation in `README.md`.

## Migration direction

Qt 6 Widgets is the selected desktop client (lowest memory, about 50-60 MB). Its C++ routing and delivery is a port of the Tauri client's Rust logic and must keep the same rules. Keep Android notification, foreground service, Room, WorkManager and encrypted config on Android. Preserve the WinForms and Tauri clients until Qt is packaged and has been used in place of them. Avalonia, Compose desktop, GTK, MAUI and WinUI are comparison prototypes; do not extend them unless requested. No iOS target exists yet.

Saving must persist locally before network delivery. Failed delivery remains retryable. Preserve the `config.yaml` format, routing semantics, sidecar authentication, and existing user data.

## Workflow

Repository skills live in `.agents/skills/`; verify their instructions against actual files. On Windows run Gradle from `android-offnote/` with `gradlew.bat`; use `npm` for Tauri/sidecar, `dotnet` for WinForms, and `qt-offnote\build.bat` (MSYS2 MinGW, needs qt6-base and yaml-cpp) for Qt. Test Qt against a scratch `APPDATA`. Report exact checks and platform gaps. Never print credentials or commit local config, databases, signing material, or build outputs. Preserve unrelated working-tree edits.
