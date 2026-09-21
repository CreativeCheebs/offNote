# AFFiNE sidecar

A tiny HTTP wrapper around the existing `connectors/affine` AFFiNE connector
(socket.io + Yjs CRDT sync). This is the **single place AFFiNE credentials
live** - no client (Android, Tauri, WinForms) stores an AFFiNE
email/password/workspace_id anymore; they only know this service's URL, a
bearer token, and a connector *name*.

Deploy this **alongside** `connectors/affine` (it imports `../affine/append-core.js`
by relative path) on the same host as your AFFiNE server, e.g.
`alphacore.taila9d96c.ts.net`, reachable over your existing Tailscale network.

## Configure

Copy `connectors.example.json` to `connectors.json` (gitignored - this file
holds real credentials) and fill in one entry per named connector:

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
`connectors.json` is re-read on every request, so edits apply without a restart.

## Deploy

```
# On the AFFiNE host, copy both directories:
#   connectors/affine/
#   connectors/affine-sidecar/  (including your connectors.json)
cd connectors/affine && npm install
cd ../affine-sidecar && npm install   # only needed if connectors/affine/node_modules isn't present

AFFINE_SIDECAR_TOKEN=<generate a random string> PORT=8787 node server.js
```

Run it under systemd/pm2 so it survives reboots and SSH disconnects - see
`offnote-sidecar.service` in this directory for a systemd user-service unit.

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
