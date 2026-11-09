# Offnote desktop client (Qt 6 Widgets)

The chosen desktop client for Windows. It sits in the system tray, opens a small
capture window from a global hotkey (`Alt+Space` by default), saves every note to
the shared durable SQLite outbox first, then routes it by `#tag` and delivers it in
the background. Start it with `launch-qt.bat` at the repository root.

It reads and writes `%APPDATA%\QuickNote\config.yaml` and `quicknote.db`, the same files the
earlier (now removed) Tauri and WinForms clients used, so existing notes and
config carry over and notes still queued by those clients are delivered by this one.

## Capture window

Dark, frameless, no Save button. Drag it by its background, press `Ctrl+Enter`
(or `Alt+Enter`, or the save shortcut from Settings) to save, `Esc` to hide. A
saved note flashes the box green and says "Saved to local outbox", then the window
fades out. The footer hint follows the configured hotkey.

Keep the text of the UI to ASCII (plus the middle dot already in the footer). A
single missing glyph, such as a check mark, makes Windows load fallback fonts and
pushed the process from about 50 MB to about 390 MB in an earlier experiment.

## Tray menu

New note, Outbox (shows how many notes are waiting), Settings..., Retry pending now,
Reload config, Quit. The tooltip also shows the number of waiting notes.

## Routing and delivery

Ported from the Tauri client (`src-tauri/src/lib.rs` in git history, commit 84e8caa), same rules:

- A tag is a `#` at the start of the text or after whitespace, followed by non-space
  characters; trailing punctuation is trimmed. `# heading` is not a tag.
- A note goes to every connection its tags map to (`routing.tags`), or to
  `routing.default` when no tag matches.
- Connection types: `markdown`, `obsidian` (daily file `YYYY-MM-DD.md`), `logseq`
  (`journals/YYYY_MM_DD.md`) and `affine` (authenticated `POST {sidecar_url}/append`
  to the sidecar; credentials stay in the sidecar).
- Delivery runs on a worker thread right after each save and every 30 seconds. A
  note's remaining targets shrink as destinations succeed, so a retry never
  re-sends to a destination that already worked. HTTP uses WinHTTP, so there is no
  extra Qt module in memory.
- A note queued with no targets (for example while `config.yaml` was unreadable, or
  by an earlier build of this client) is routed from the current config at retry
  time. The Tauri client would mark such a note delivered without sending it.

## Settings window

Tray, Settings...:

- **Connections**: add, rename, remove; per-type fields (folder with Browse, or
  sidecar URL, masked token and connector name); live validation; "Send test note"
  delivers a test line straight to that connection without queueing it.
- **Routing**: default connection, tag rules, and a "Try it" box that shows where a
  sample note would go.
- **Shortcuts**: click a box and press the keys. The open shortcut is re-registered
  on Save and the previous one is kept if another program already owns the new one.
- **General**: start with Windows (a per-user `Run` entry named `OffnoteQt`), data
  folder paths.
- **Outbox**: recent notes with Delivered / Queued / Retrying status and the last
  error as a tooltip; Retry pending now.

Sidecar URLs must be http:// or https://; tokens must be visible ASCII (no spaces or line breaks). Delivery never follows an https-to-http redirect and error messages carry only the HTTP status, never the response body. Plain http sends the token unencrypted, so use https or a private network such as Tailscale.

Save validates, then writes `config.yaml` atomically and keeps a one-time
`config.yaml.bak-qt` of the previous file. Comments and keys this client does not know are not preserved,
as with the Tauri settings window. The file it writes was checked to parse with the
WinForms client's YamlDotNet and the Tauri client's serde_yaml.

If `config.yaml` cannot be parsed, Settings refuses to open (saving would replace the
file), notes are still saved, and the capture window says so.

`OffnoteQt.exe --settings` opens the Settings window at launch.

## Build and test

Install MSYS2 packages `mingw-w64-x86_64-qt6-base` and `mingw-w64-x86_64-yaml-cpp`
(sqlite3 is already needed), then run `build.bat`. `launch-qt.bat` rebuilds
incrementally before starting; quit the running copy first or the link step fails.

`OffnoteQtTests.exe` (built next to the app) checks tag extraction, routing, shortcut
parsing, config round-trips, the markdown and Logseq file formats, and retry
behaviour. It runs against a scratch `%APPDATA%`, never the real one. To also check
the AFFiNE request, run `python tests/sidecar_stub.py 8799 out.json` and set
`OFFNOTE_TEST_SIDECAR=http://127.0.0.1:8799` before running the tests.

The app uses Qt, yaml-cpp and compiler DLLs from `C:\msys64\mingw64\bin`, so it is
not yet a redistributable package (`windeployqt` or a static build is still to do).

## Running more than one client

Run only one desktop client at a time. Two clients both register `Alt+Space`, and
both would try to deliver the same pending notes. If the Tauri app is set to start
with Windows, disable that before relying on this client.
