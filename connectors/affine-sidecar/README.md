# AFFiNE sidecar

A self-contained HTTP service that talks to AFFiNE over its sync API
(socket.io + Yjs CRDT). This is the **single place AFFiNE credentials
live** - no Offnote client (Android, Tauri, WinForms) stores an AFFiNE
email/password/workspace_id anymore; they only know this service's URL, a
bearer token, and a connector *name*.

Everything the sidecar needs is in this one directory - `lib.js` and
`append-core.js` hold the actual sync-protocol logic, `server.js` is the HTTP
layer, `install.sh` sets it all up. Copy (or `git pull`) just this folder onto
the same host as your AFFiNE server.

## Install / update

```
./install.sh
```

Safe to re-run any number of times - re-running to pick up a code update
won't touch your credentials, port, or token unless you explicitly ask it to:

- `npm install` is idempotent
- `connectors.json` is only ever created from the template on first run,
  never overwritten after
- the token in `.env` is kept as-is unless you pass `--new-token`
- the systemd unit is regenerated and reloaded, not duplicated
- the service is restarted (brief, sub-second downtime), never left half-updated

`loginctl enable-linger` is applied so the service survives you logging out
of SSH, and it comes back after a reboot.

Options:

```
./install.sh --port 9000      # change the listening port
./install.sh --new-token      # rotate the sidecar token (update your clients after)
```

At the end of every run it prints the token plus every address this host is
reachable at (Tailscale IP, LAN IPs) - copy whichever one your client can
actually route to into that connection's `sidecar_url`.

## Configure

The first `./install.sh` run creates `connectors.json` from
`connectors.example.json` (gitignored - this file holds real credentials).
Edit it with one entry per named connector:

```json
{
  "affine_braindump": {
    "type": "affine",
    "email": "you@example.com",
    "password": "your-affine-password",
    "workspaceId": "your-workspace-id",
    "journal": true
  }
}
```

Use `"pageId": "..."` instead of `"journal": true` to target a fixed page.
`connectors.json` is re-read on every request, so edits apply without a
restart - only changing the port or rotating the token needs `./install.sh`
again.

## What address do I put in a client's `sidecar_url`?

Whatever address that specific device can route to this host over:

- Same LAN as the server → the LAN IP `install.sh` prints (e.g. `192.168.0.8`)
- Elsewhere, over Tailscale → the Tailscale IP or `.ts.net` hostname (both
  print at the end of `install.sh`, when `tailscale` is on the server's PATH)

There's no single right answer here since it depends on your network - this
is not something an install script can decide for you, only surface the
options.

Since the sidecar serves plain HTTP, Android additionally requires a
cleartext-traffic exception naming whichever host/IP you use (see
`android-offnote/app/src/debug/res/xml/network_security_config.xml`, which is
gitignored precisely so a private host never ends up in the public repo).

## API

`POST /append`, header `Authorization: Bearer <AFFINE_SIDECAR_TOKEN>`, JSON body:

```json
{
  "connector": "affine_braindump",
  "texts": ["note text"]
}
```

Returns `{ "ok": true, "pageId": "..." }` on a verified round-trip, or a
4xx/5xx with `{ "ok": false, "error": "..." }`.

`GET /health` returns `{ "ok": true }` with no auth, for a liveness check.
