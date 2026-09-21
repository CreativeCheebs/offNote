// QuickNote/Offnote AFFiNE connector CLI.
//
// Appends one or more paragraphs to a page in a self-hosted AFFiNE workspace.
// Reads a single JSON job from STDIN so credentials never land in the process
// command line or shell history:
//
//   { "base": "https://host:3010",   // optional, overrides AFFINE_BASE / default
//     "email": "...", "password": "...",
//     "workspaceId": "...", "pageId": "...",
//     "texts": ["line one", "line two"] }
//
// Exits 0 on a verified round-trip, non-zero otherwise. Diagnostics go to
// stderr; stdout stays clean for the caller. The actual sync-protocol logic
// lives in append-core.js, shared with the HTTP sidecar (server.js) used by
// the Android app.
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
