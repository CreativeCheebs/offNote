// Manual test/debug CLI for the AFFiNE sync logic - NOT used by any Offnote
// client. Every client (Android, Tauri, WinForms) talks to server.js over
// HTTP instead; this just lets you exercise append-core.js directly from a
// terminal when debugging the sync protocol itself.
//
// Reads a single JSON job from STDIN so credentials never land in the
// process command line or shell history:
//
//   { "base": "https://host:3010",   // optional, overrides AFFINE_BASE / default
//     "email": "...", "password": "...",
//     "workspaceId": "...", "pageId": "...",
//     "texts": ["line one", "line two"] }
//
// Exits 0 on a verified round-trip, non-zero otherwise. Diagnostics go to
// stderr; stdout stays clean for the caller.
import { appendNote } from './append-core.js';

const log = (...a) => console.error('[affine]', ...a);

function readStdin() {
  return new Promise((resolve, reject) => {
    let buf = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (d) => (buf += d));
    process.stdin.on('end', () => resolve(buf));
    process.stdin.on('error', reject);
  });
}

async function main() {
  const raw = await readStdin();
  let job;
  try {
    job = JSON.parse(raw);
  } catch (e) {
    throw new Error(`invalid job JSON on stdin: ${e.message}`);
  }
  await appendNote(job);
}

main()
  .then(() => process.exit(0))
  .catch((e) => {
    log('ERROR:', e.message);
    process.exit(1);
  });
