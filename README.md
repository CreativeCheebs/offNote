# Offnote

A quick-capture note app for jotting things down with as little friction as
possible - a global hotkey popup on desktop, a lockscreen notification on
Android - with a small save-celebration animation as positive reinforcement
(built with ADHD users in mind). Every note is written to a durable local
queue first, then routed by `#tag` to one or more destinations: a markdown
file, or a self-hosted [AFFiNE](https://affine.pro/) workspace.

There are three client apps sharing one delivery model, plus a small server
component for AFFiNE:

| Component | What it is |
|---|---|
| `src/` + `src-tauri/` | Desktop app (Windows/Linux/macOS via Tauri) |
| `winforms-quicknote/` | Windows-only alternative desktop app (WinForms/.NET) |
| `android-offnote/` | Android app (Kotlin) |
| `connectors/affine-sidecar/` | Small server-side HTTP service that talks to AFFiNE |
| `connectors/setup/` | Interactive CLI to help build a desktop `config.yaml` |

## Why a sidecar for AFFiNE?

AFFiNE's sync protocol is a live socket.io connection speaking Yjs's CRDT
binary format - not a simple REST call. Rather than reimplement that in
every client (including embedding a JS/Node runtime in the Android APK),
`connectors/affine-sidecar` runs the real sync logic once, server-side, next
to your AFFiNE instance. Every client (desktop or phone) just makes a plain
authenticated HTTP POST naming which of the sidecar's configured connectors
to use.

The practical effect: **no client ever stores your AFFiNE email, password, or
workspace ID.** Only the sidecar does, in a file that's excluded from git by
default.

## Quick start

### 1. Deploy the AFFiNE sidecar (only needed if you use AFFiNE)

On a Linux host reachable by your desktop and/or phone (typically the same
machine running self-hosted AFFiNE, or reachable over Tailscale/your LAN):

```bash
# copy this one directory to the server (scp, git clone, rsync, whatever)
cd connectors/affine-sidecar
./install.sh
```

This installs dependencies, prompts you to fill in `connectors.json` with
your real AFFiNE credentials (created from `connectors.example.json` on
first run), sets up a systemd `--user` service so it survives reboots and
SSH disconnects, and prints the addresses your clients can reach it at plus
the auth token they'll need. See `connectors/affine-sidecar/README.md` for
full details, the HTTP API, and how to safely re-run it after an update.

If you only want the markdown connector, you can skip this step entirely.

### 2. Desktop app (Tauri)

```bash
npm install
npm run tauri dev      # development
npm run tauri build    # release build
```

First run copies `src-tauri/config.yaml` (a template) to
`%APPDATA%\QuickNote\config.yaml` (Windows) - edit that copy to add
connections and routing, then use the tray's "Reload Config".

### 2b. Desktop app (WinForms, Windows only)

```bash
cd winforms-quicknote
dotnet build
dotnet run
```

Shares the same `%APPDATA%\QuickNote\config.yaml` and note database as the
Tauri app - handy if you want to compare the two or prefer WinForms' native feel.

### 3. Android app

Requires the Android SDK. From `android-offnote/`:

```bash
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Open the app once to grant the notification permission, then go to
**Settings** and paste in your connections/routing config (YAML, same shape
as the desktop `config.yaml` - see below). A persistent notification then
lets you capture a note from the lockscreen (tapping it may still require
unlocking on a secured phone - that's an Android OS policy, not something an
app can bypass).

If your AFFiNE sidecar serves plain HTTP (no TLS) and isn't reachable via a
`.ts.net`/public hostname, Android's cleartext-traffic block means you'll
need a domain exception - see the comment in
`android-offnote/app/src/main/res/xml/network_security_config.xml` for how to
add your own host in a gitignored override file, so it never ends up
committed.

## Configuring connections and routing

Every client uses the same config shape (YAML on desktop and Android, same
field names). Example:

```yaml
connections:
  - name: personal
    type: markdown
    path: "notes"          # relative to %APPDATA%\QuickNote, or absolute

  - name: affine_braindump
    type: affine
    sidecar_url: "http://your-sidecar-host:8787"
    sidecar_token: "<token printed by install.sh>"
    sidecar_connector: "affine_braindump"   # a key in the sidecar's connectors.json

routing:
  tags: {}                  # e.g. work: personal
  default: affine_braindump
```

A note's `#tags` are matched against `routing.tags` to pick a destination;
untagged notes (or tags with no mapping) go to `routing.default`. A note is
always written to the local durable queue first and retried in the
background until delivery succeeds - nothing is lost if a connection is
temporarily unreachable.

## Interactive setup CLI

```bash
npm run setup
```

Walks through validating an existing `config.yaml` and adding connections
interactively. **Known limitation:** it currently talks to AFFiNE directly
and writes the older direct-credential connection shape, predating the
sidecar - if you're using the sidecar, prefer editing `config.yaml` by hand
per the example above.

## Repo layout notes

- `connectors/affine-sidecar/connectors.json` and `.env` hold real
  credentials/tokens and are gitignored - copy the `.example` files to get started.
- `android-offnote/app/src/debug/res/xml/network_security_config.xml` is
  gitignored for the same reason (it may need to name a private host/IP).
- Nothing else in the repo should ever contain real credentials - if you find
  something, please open an issue.
