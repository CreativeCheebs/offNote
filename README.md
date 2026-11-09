# Offnote

Jot a thought down in a second, then forget about it. Press a hotkey on your PC
(or tap a lockscreen notification on your phone), type, and save. Offnote keeps
every note safe on your device first, then sends it where you want it: a
markdown file, an Obsidian vault, a Logseq graph, or your own
[AFFiNE](https://affine.pro/) workspace. If the destination is unreachable,
the note waits and is retried automatically. Nothing is lost.

## What you get

- **Windows app**: lives in the system tray, opens with `Alt+Space`, saves with
  `Ctrl+Enter`, hides with `Esc`. Uses roughly 50-60 MB of memory.
- **Android app**: a persistent notification lets you capture a note from the
  lockscreen.
- **Tags decide where a note goes.** Write `#work call the dentist` and the note
  goes to whatever you linked to `work`. Notes with no matching tag go to your
  default destination.

## Install the Windows app

There is no installer yet, so the app is built once on your PC (about five
minutes):

1. Install [MSYS2](https://www.msys2.org/) with the default options.
2. Open the **MSYS2 MINGW64** terminal and run:

   ```
   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-qt6-base mingw-w64-x86_64-yaml-cpp mingw-w64-x86_64-sqlite3
   ```
3. Download this repository and double-click **`launch-qt.bat`**. It builds the
   app and starts it. (Quit Offnote before running it again, so it can rebuild.)

Offnote appears as an icon in the system tray. Press `Alt+Space` to write a note.

**Start with Windows:** right-click the tray icon, **Settings...**, **General**,
tick "Start Offnote when I sign in to Windows", then **Save**.

## Using it

| You want to | Do this |
|---|---|
| Open or hide the note box | `Alt+Space` (changeable in Settings) |
| Save the note | `Ctrl+Enter` or `Alt+Enter` |
| Hide without saving | `Esc` |
| Move the window | Drag it by its background |
| See what was delivered or is waiting | Right-click the tray icon, **Outbox** |
| Retry waiting notes now | Right-click the tray icon, **Retry pending now** |

A green flash and "Saved to local outbox" confirm a save. The tray icon tooltip
shows how many notes are still waiting to be delivered.

Run only one copy of Offnote at a time; two copies would fight over `Alt+Space`.

## Choosing where notes go (Settings)

Right-click the tray icon and choose **Settings...**.

- **Connections**: add the places notes can go. Pick a type:
  - *Markdown folder*: one file per day.
  - *Obsidian vault*: the same, inside your vault.
  - *Logseq graph*: added to today's journal.
  - *AFFiNE (via sidecar)*: your AFFiNE workspace, see below.

  **Send test note** checks that a connection works.
- **Routing**: pick a default connection and link tags to connections. The
  **Try it** box shows where a sample note would go.
- **Shortcuts**: click a box and press the keys you want.
- **General**: start with Windows, and where your data is stored.
- **Outbox**: every recent note and whether it was delivered.

Press **Save** (or `Ctrl+S`) to apply. Your settings and notes live in
`%APPDATA%\QuickNote`.

## Optional: send notes to AFFiNE

AFFiNE needs a small helper service, called the sidecar, running next to your
AFFiNE server. It keeps your AFFiNE login on the server, so neither the PC nor
the phone ever stores your AFFiNE password.

1. On the Linux machine that runs (or can reach) AFFiNE, copy the
   `connectors/affine-sidecar` folder and run `./install.sh`. It asks for your
   AFFiNE details and prints an address and a token.
2. In Offnote **Settings, Connections**, add an *AFFiNE (via sidecar)*
   connection with that address, the token, and the connector name you chose.
3. Press **Send test note** to confirm.

More detail is in `connectors/affine-sidecar/README.md`. Use an `https://`
address or a private network such as Tailscale: a plain `http://` address sends
your token unencrypted.

## Install the Android app

1. Install the Android SDK tooling, then from the `android-offnote` folder run
   `./gradlew assembleDebug`.
2. With your phone connected, run
   `adb install -r app/build/outputs/apk/debug/app-debug.apk`.
3. Open Offnote once and allow notifications. In **Settings**, paste your
   connections and routing (same format as the Windows `config.yaml`).

A persistent notification then lets you capture a note from the lockscreen. On
a phone with a screen lock you may need to unlock first; that is an Android
rule, not something an app can bypass.

## Your data and privacy

- Notes are stored on your device and sent only to the destinations you set up.
- The AFFiNE token is stored in `%APPDATA%\QuickNote\config.yaml` on your PC
  (and in the phone's encrypted storage). Treat that file like a password.
- Offnote never logs your notes or tokens.

## Troubleshooting

- **`Alt+Space` does nothing**: another program already uses it. Choose another
  combination in **Settings, Shortcuts**.
- **A note stays "Retrying" in the Outbox**: hover over the status to see why
  (for example, the sidecar is unreachable). It is retried every 30 seconds.
- **"config.yaml could not be read"**: the file has a typo. Fix it in a text
  editor, or move it aside and Offnote creates a fresh one.
- **The first run fails to build**: re-check step 2 of the install, then run
  `launch-qt.bat` again.
