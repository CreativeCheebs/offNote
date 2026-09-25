// Tiny HTTP sidecar for AFFiNE delivery from every Offnote client (Android,
// and eventually the desktop apps too) - see connectors.example.json for the
// server-side connector config shape.
//
// Runs *next to* the AFFiNE server (e.g. on alphacore.taila9d96c.ts.net,
// reachable over Tailscale), so clients only ever need to make a plain HTTP
// POST naming a connector - the socket.io + Yjs CRDT sync logic
// (connectors/affine) and the AFFiNE credentials both stay server-side. No
// client (phone or desktop) stores AFFiNE email/password/workspace_id
// anymore; it only knows the sidecar's URL, a bearer token, and a connector
// name.
//
// Auth: a single shared bearer token (AFFINE_SIDECAR_TOKEN env var), checked
// on every request. This is a private LAN/Tailscale-only service, not a
// public API - keep it off the public internet.
//
// Usage:
//   AFFINE_SIDECAR_TOKEN=<random-string> PORT=8787 node server.js
import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { appendNote } from './append-core.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 8787);
const TOKEN = process.env.AFFINE_SIDECAR_TOKEN;
if (!TOKEN) {
  console.error('[affine-sidecar] AFFINE_SIDECAR_TOKEN is required');
  process.exit(1);
}

const CONNECTORS_PATH = process.env.CONNECTORS_PATH || path.join(__dirname, 'connectors.json');

// Throws (never exits the process) so a config mistake fails just the one
// request with a 5xx, instead of taking the whole server down.
function loadConnectors() {
  if (!fs.existsSync(CONNECTORS_PATH)) {
    throw new Error(`no connector config at ${CONNECTORS_PATH} (copy connectors.example.json)`);
  }
  return JSON.parse(fs.readFileSync(CONNECTORS_PATH, 'utf8'));
}

// Reloaded per-request (cheap, small file) so editing connectors.json takes
// effect without restarting the process.
function connector(name) {
  const all = loadConnectors();
  return all[name];
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let buf = '';
    req.setEncoding('utf8');
    req.on('data', (d) => {
      buf += d;
      if (buf.length > 1_000_000) req.destroy(new Error('body too large'));
    });
    req.on('end', () => resolve(buf));
    req.on('error', reject);
  });
}

function send(res, status, body) {
  const json = JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(json) });
  res.end(json);
}

const server = http.createServer(async (req, res) => {
  if (req.method === 'GET' && req.url === '/health') {
    return send(res, 200, { ok: true });
  }

  if (req.method !== 'POST' || req.url !== '/append') {
    return send(res, 404, { error: 'not found' });
  }

  const auth = req.headers['authorization'] || '';
  if (auth !== `Bearer ${TOKEN}`) {
    return send(res, 401, { error: 'unauthorized' });
  }

  let body;
  try {
    body = JSON.parse(await readBody(req));
  } catch (e) {
    return send(res, 400, { error: `invalid JSON body: ${e.message}` });
  }

  const { connector: connectorName, texts } = body;
  if (!connectorName) return send(res, 400, { error: 'body requires "connector"' });

  let conn;
  try {
    conn = connector(connectorName);
  } catch (e) {
    return send(res, 500, { error: e.message });
  }
  if (!conn) return send(res, 404, { error: `unknown connector '${connectorName}'` });
  if (conn.type !== 'affine') return send(res, 400, { error: `connector '${connectorName}' is not type "affine"` });

  try {
    const result = await appendNote({ ...conn, texts });
    send(res, 200, { ok: true, ...result });
  } catch (e) {
    console.error(`[affine-sidecar] append via '${connectorName}' failed:`, e.message);
    send(res, 502, { ok: false, error: e.message });
  }
});

server.listen(PORT, () => {
  console.log(`[affine-sidecar] listening on :${PORT}, connectors from ${CONNECTORS_PATH}`);
});

// Defense in depth: a single bad request should never take the whole service
// down (it did once, from an uncaught throw - see git history). Log and keep
// serving other requests rather than crashing the process.
process.on('uncaughtException', (e) => {
  console.error('[affine-sidecar] uncaught exception (still running):', e);
});
process.on('unhandledRejection', (e) => {
  console.error('[affine-sidecar] unhandled rejection (still running):', e);
});
