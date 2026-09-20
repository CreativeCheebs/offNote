// Shared helpers for AFFiNE self-hosted API testing (v0.27.4)
import { io } from 'socket.io-client';
import * as Y from 'yjs';

// Base URL comes from config (passed via env by the caller, e.g.
// affine-append.js sets AFFINE_BASE from the connection's `url` field);
// the literal is only the fallback for local testing. This MUST be read
// lazily (a function, not a const computed at import time) - callers set
// process.env.AFFINE_BASE *after* importing this module, and a const would
// have already frozen in the fallback before that assignment ever ran.
function BASE() {
  // Strip a trailing slash - "url/" + "/api/..." would otherwise become a
  // double slash that some servers 404 on.
  return (process.env.AFFINE_BASE || 'https://alphacore.taila9d96c.ts.net:3010').replace(/\/+$/, '');
}
export const CLIENT_VERSION = process.env.AFFINE_CLIENT_VERSION || '0.27.4';

// Allow self-signed / tailscale cert
process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';

export async function signIn(email, password) {
  const res = await fetch(`${BASE()}/api/auth/sign-in`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ email, password }),
  });
  if (!res.ok) throw new Error(`sign-in failed: ${res.status} ${await res.text()}`);
  const setCookies = res.headers.getSetCookie?.() ?? [];
  const cookie = setCookies.map((c) => c.split(';')[0]).join('; ');
  const csrf = (cookie.match(/affine_csrf_token=([^;]+)/) || [])[1];
  const user = await res.json();
  return { cookie, csrf, user };
}

export async function gql(session, query, variables) {
  const res = await fetch(`${BASE()}/graphql`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Cookie: session.cookie,
      'x-affine-csrf-token': session.csrf,
    },
    body: JSON.stringify({ query, variables }),
  });
  const json = await res.json();
  if (json.errors) throw new Error('GraphQL error: ' + JSON.stringify(json.errors));
  return json.data;
}

export function connect(session) {
  return new Promise((resolve, reject) => {
    const socket = io(BASE(), {
      transports: ['websocket'],
      extraHeaders: { Cookie: session.cookie },
      rejectUnauthorized: false,
    });
    socket.on('connect', () => resolve(socket));
    socket.on('connect_error', reject);
    setTimeout(() => reject(new Error('socket connect timeout')), 15000);
  });
}

// Promisified emit with ack
export function emit(socket, event, payload) {
  return new Promise((resolve, reject) => {
    socket.timeout(15000).emit(event, payload, (err, ack) => {
      if (err) return reject(err);
      resolve(ack);
    });
  });
}

export async function joinWorkspace(socket, workspaceId) {
  return emit(socket, 'space:join', {
    spaceType: 'workspace',
    spaceId: workspaceId,
    clientVersion: CLIENT_VERSION,
  });
}

export async function loadDoc(socket, workspaceId, docId) {
  const ack = await emit(socket, 'space:load-doc', {
    spaceType: 'workspace',
    spaceId: workspaceId,
    docId,
  });
  return ack; // { data: { missing, state, timestamp } } or { error }
}

export async function pushDocUpdate(socket, workspaceId, docId, update /* Uint8Array */) {
  const ack = await emit(socket, 'space:push-doc-update', {
    spaceType: 'workspace',
    spaceId: workspaceId,
    docId,
    update: Buffer.from(update).toString('base64'),
  });
  return ack;
}

export { Y };
