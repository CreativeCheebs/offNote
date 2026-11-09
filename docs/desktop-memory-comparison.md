# Desktop UI memory comparison

Measured on this Windows machine while evaluating Offnote desktop frameworks. Qt was chosen and every other desktop client has since been removed from the tree (last present in commit 84e8caa).
Values are approximate snapshots of **working set** (physical memory resident
for a process). Working sets include shareable pages, so summing processes is
useful for comparing app footprints but is not an exact measure of unique RAM.
Private memory is listed where recorded. No benchmark used an identical full
feature set; see the scope notes below.

| Client | Working set | Private memory | What was measured |
|---|---:|---:|---|
| Qt 6 Widgets | **about 49 MB** idle, about 55-60 MB after using Settings or delivering a note | 42-52 MB | Current client with routing, delivery and Settings (see the Qt paragraph below). The earlier capture-only prototype measured 44.7 MB. |
| WinForms | about 52–76 MB initially; about 80–100 MB in later use | — | Existing full Windows client. The latter range is the user's observation. |
| .NET 10 Avalonia | about 77 MB visible, 88 MB after typing, 18 MB hidden | about 21 / 31 / 29 MB | Frameless capture prototype with tray icon and hotkey, Release build, with software rendering and the GC/runtime settings in `avalonia-offnote/README.md`. Delivery to a sidecar was not measured. Before these changes: about 173 MB working set, about 109 MB private. |
| .NET 10 MAUI Windows | about 230 MB | about 145 MB | UI-only capture benchmark, Release build; no note saving. |
| GTK 3 | **254.8 MB** | 231.5 MB | Capture prototype with window open after 20 seconds. |
| Compose Multiplatform desktop | about 336 MB | — | Desktop app process; a separate Gradle daemon used about 1.24 GB during the development run. The daemon is development overhead, not a packaged app process. |
| Tauri 2, popup hidden | **488.2 MB total** | about 249.3 MB total | Existing September 2026 Release executable: `offnote.exe` plus its seven WebView2 descendants after about 35 seconds. Main process alone: 43.4 MB. |
| Tauri 2, popup open | **507.9 MB total** | 263.7 MB total | Same executable and process tree, five seconds after sending `Alt+Space`. Main process alone: 44.9 MB. |

## How the Tauri figure was obtained

The measured binary was `src-tauri/target/release/offnote.exe`, built on
September 27, 2026. Rust/Cargo was unavailable in the current shell, so this
is the existing Release artifact rather than a fresh build of the current
working tree. The app started outside the filesystem sandbox because WebView2
failed to initialize inside it. Process parent IDs were read through Windows
CIM; only `offnote.exe` and its descendant `msedgewebview2.exe` processes were
summed. Other Edge and WebView2 processes already running on the machine were
excluded. A transient `MoNotificationUx.exe` child was also excluded because
it was not part of Tauri or WebView2. The popup-open reading followed the
configured global `Alt+Space` hotkey.

## What each prototype covers

- Qt now has the full note path: tray, configurable global hotkey, config
  parsing, `#tag` routing, markdown/Obsidian/Logseq/AFFiNE delivery with
  background retry, an outbox view and a Settings window. GTK still has only a
  tray, global hotkey, and durable save to the shared SQLite outbox.
- Avalonia reuses the C# note logic, but is still a migration prototype.
- MAUI was a visual benchmark without persistence.
- WinForms, Compose desktop, and Tauri have broader app behavior, so their
  memory readings include more than the small Qt and GTK capture prototypes.

The current result favors **Qt Widgets** for a small resident Windows app, and
Qt is the chosen desktop direction. After adding config parsing, routing,
delivery and the Settings window, the Qt app measured about **49 MB working set
(42.5 MB private)** with the capture window open or hidden in the tray, about
**60 MB** with the Settings window open, and it did not fall all the way back
afterwards: about 55 MB after closing Settings and about 57 MB after delivering a
note over HTTP (scratch data folder, local sidecar stub; one run each, not
averaged). So the figure is roughly 50-60 MB, not a fixed 50. Packaging is still to be done
before replacing the existing desktop client. Run `launch-qt.bat` to start it.
