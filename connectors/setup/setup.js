#!/usr/bin/env node
// Interactive CLI to check QuickNote's config.yaml for problems and help fill
// in the fiddly bits (AFFiNE workspace id / page id / journal mode, file
// paths, tag routing) without hand-editing YAML.
//
// Usage:
//   node connectors/setup/setup.js [--config <path>]
//
// With no --config, operates on the shared live config both apps read/write:
//   %APPDATA%\QuickNote\config.yaml

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createInterface } from "node:readline/promises";
import { parseDocument } from "yaml";
import { io } from "socket.io-client";
import * as Y from "yjs";

// Self-hosted AFFiNE servers commonly sit behind a self-signed / Tailscale
// cert; the bundled connector (connectors/affine/lib.js) accepts the same
// trade-off, so this tool matches it rather than failing every real setup.
process.env.NODE_TLS_REJECT_UNAUTHORIZED = "0";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, "..", "..");
const CONNECTION_TYPES = ["markdown", "obsidian", "logseq", "affine"];

const rl = createInterface({ input: process.stdin, output: process.stdout });

// A hand-rolled question queue instead of rl.question(): the built-in
// question() only arms its line-callback once you call it, so any input
// lines that arrive before that call (e.g. several answers typed ahead, or
// piped/pasted input) get silently dropped as bare 'line' events. Queuing
// every line ourselves means nothing is ever lost, no matter the timing.
const pendingLines = [];
const pendingWaiters = [];
rl.on("line", (line) => {
  if (pendingWaiters.length) pendingWaiters.shift()(line);
  else pendingLines.push(line);
});
function ask(query) {
  if (query) process.stdout.write(query);
  return new Promise((resolve) => {
    if (pendingLines.length) resolve(pendingLines.shift());
    else pendingWaiters.push(resolve);
  });
}

async function askPassword(question) {
  // Raw-mode character-by-character masking is unreliable across Windows
  // terminals (Git Bash/MSYS and some Windows Terminal configs don't
  // suppress local echo the way the trick assumes, which was corrupting
  // typed passwords rather than just showing them) - echo instead. This is
  // a local setup tool, not a shared login prompt.
  return ask(question);
}

function liveConfigPath() {
  const appData = process.env.APPDATA || path.join(os.homedir(), "AppData", "Roaming");
  return path.join(appData, "QuickNote", "config.yaml");
}

function templatePath() {
  // Either seed template is fine; they're required to stay identical.
  return path.join(REPO_ROOT, "src-tauri", "config.yaml");
}

// --- Minimal AFFiNE client (deliberately separate from connectors/affine/lib.js,
// whose BASE is a module-level constant baked in from AFFINE_BASE at import
// time -- this tool needs to talk to a different server per wizard run). ---

async function affineSignIn(base, email, password) {
  const res = await fetch(`${base}/api/auth/sign-in`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ email, password }),
  });
  if (!res.ok) throw new Error(`sign-in failed: ${res.status} ${await res.text()}`);
  const setCookies = res.headers.getSetCookie?.() ?? [];
  const cookie = setCookies.map((c) => c.split(";")[0]).join("; ");
  const csrf = (cookie.match(/affine_csrf_token=([^;]+)/) || [])[1];
  const user = await res.json();
  return { base, cookie, csrf, user };
}

async function affineGql(session, query, variables) {
  const res = await fetch(`${session.base}/graphql`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Cookie: session.cookie,
      "x-affine-csrf-token": session.csrf,
    },
    body: JSON.stringify({ query, variables }),
  });
  const json = await res.json();
  if (json.errors) throw new Error("GraphQL error: " + JSON.stringify(json.errors));
  return json.data;
}

function affineConnect(session) {
  return new Promise((resolve, reject) => {
    const socket = io(session.base, {
      transports: ["websocket"],
      extraHeaders: { Cookie: session.cookie },
      rejectUnauthorized: false,
    });
    socket.on("connect", () => resolve(socket));
    socket.on("connect_error", reject);
    setTimeout(() => reject(new Error("socket connect timeout")), 15000);
  });
}

function affineEmit(socket, event, payload) {
  return new Promise((resolve, reject) => {
    socket.timeout(15000).emit(event, payload, (err, ack) => {
      if (err) return reject(err);
      resolve(ack);
    });
  });
}

async function affineLoadRoot(socket, workspaceId) {
  await affineEmit(socket, "space:join", {
    spaceType: "workspace",
    spaceId: workspaceId,
    clientVersion: "0.27.4",
  });
  const ack = await affineEmit(socket, "space:load-doc", {
    spaceType: "workspace",
    spaceId: workspaceId,
    docId: workspaceId,
  });
  if (ack.error) throw new Error("load root doc failed: " + JSON.stringify(ack.error));
  const doc = new Y.Doc();
  if (ack.data?.missing) Y.applyUpdate(doc, Buffer.from(ack.data.missing, "base64"));
  return doc.getMap("meta");
}

async function affineListPages(session, workspaceId) {
  const socket = await affineConnect(session);
  try {
    const meta = await affineLoadRoot(socket, workspaceId);
    const pages = meta.get("pages");
    return pages ? pages.toArray().map((p) => (p.toJSON ? p.toJSON() : p)) : [];
  } finally {
    socket.close();
  }
}

// Workspace name isn't a GraphQL field - it only lives in each workspace's
// own root Yjs doc (meta.name), so getting a friendly name means joining and
// loading each workspace in turn over the one socket connection.
async function affineWorkspaceNames(session, workspaces) {
  const socket = await affineConnect(session);
  const named = [];
  try {
    for (const ws of workspaces) {
      let name = null;
      try {
        const meta = await affineLoadRoot(socket, ws.id);
        name = meta.get("name") || null;
      } catch {
        // Unreadable root doc (e.g. bare/uninitialized workspace) - fall back to id-only display.
      }
      named.push({ ...ws, name });
    }
  } finally {
    socket.close();
  }
  return named;
}

// --- Config loading -------------------------------------------------------

function loadConfig(configPath) {
  if (!fs.existsSync(configPath)) return null;
  const raw = fs.readFileSync(configPath, "utf8");
  return parseDocument(raw, { keepSourceTokens: true });
}

async function ensureConfig(configPath) {
  let doc = loadConfig(configPath);
  if (doc) return doc;

  console.log(`No config found at ${configPath}.`);
  const seedFrom = templatePath();
  if (fs.existsSync(seedFrom)) {
    const ans = (await ask(`Seed it from the bundled template (${seedFrom})? [Y/n] `)).trim().toLowerCase();
    if (ans !== "n") {
      fs.mkdirSync(path.dirname(configPath), { recursive: true });
      fs.copyFileSync(seedFrom, configPath);
      console.log("Seeded.\n");
      return loadConfig(configPath);
    }
  }
  console.log("Aborting: no config to work with.");
  process.exit(1);
}

// --- Validation -------------------------------------------------------

function validate(doc, configDir) {
  const errors = [];
  const warnings = [];
  const data = doc.toJS() || {};

  const shortcuts = data.shortcuts || {};
  if (!shortcuts.toggle_note) warnings.push("shortcuts.toggle_note is not set");
  if (!shortcuts.save_note) warnings.push("shortcuts.save_note is not set");

  const connections = Array.isArray(data.connections) ? data.connections : [];
  if (connections.length === 0) warnings.push("no connections defined - every note will fail to route");

  const seenNames = new Set();
  for (const [i, conn] of connections.entries()) {
    const label = conn?.name ? `connection "${conn.name}"` : `connection #${i + 1}`;
    if (!conn?.name) {
      errors.push(`${label}: missing 'name'`);
    } else if (seenNames.has(conn.name)) {
      errors.push(`${label}: duplicate name`);
    } else {
      seenNames.add(conn.name);
    }

    if (!conn?.type) {
      errors.push(`${label}: missing 'type'`);
      continue;
    }
    if (!CONNECTION_TYPES.includes(conn.type)) {
      errors.push(`${label}: unknown type '${conn.type}' (expected one of ${CONNECTION_TYPES.join(", ")})`);
      continue;
    }

    if (conn.type === "markdown" || conn.type === "obsidian" || conn.type === "logseq") {
      if (!conn.path) {
        errors.push(`${label}: type '${conn.type}' requires 'path'`);
      } else {
        const resolved = path.isAbsolute(conn.path) ? conn.path : path.join(configDir, conn.path);
        if (fs.existsSync(resolved) && !fs.statSync(resolved).isDirectory()) {
          errors.push(`${label}: path '${resolved}' exists but is not a directory`);
        }
      }
    }

    if (conn.type === "affine") {
      if (!conn.url) {
        errors.push(
          `${label}: affine requires 'url' (its own server address) - without it, the connector ` +
            `silently falls back to a demo/research server, not your instance`,
        );
      }
      if (!conn.email) errors.push(`${label}: affine requires 'email'`);
      if (!conn.password) errors.push(`${label}: affine requires 'password'`);
      if (!conn.workspace_id) errors.push(`${label}: affine requires 'workspace_id'`);
      if (!conn.page_id && !conn.journal) {
        errors.push(`${label}: affine requires either 'page_id' or 'journal: true'`);
      }
      if (conn.page_id && conn.journal) {
        warnings.push(`${label}: both 'page_id' and 'journal' are set - page_id wins, journal is ignored`);
      }
    }
  }

  const routing = data.routing || {};
  if (!routing.default) {
    errors.push("routing.default is required");
  } else if (!seenNames.has(routing.default)) {
    errors.push(`routing.default '${routing.default}' does not match any connection name`);
  }
  for (const [tag, connName] of Object.entries(routing.tags || {})) {
    if (!seenNames.has(connName)) {
      errors.push(`routing.tags.${tag} -> '${connName}' does not match any connection name`);
    }
  }

  return { errors, warnings, connectionNames: [...seenNames] };
}

function printReport({ errors, warnings }) {
  console.log("");
  if (errors.length === 0 && warnings.length === 0) {
    console.log("Validation: OK - no problems found.");
  } else {
    if (errors.length) {
      console.log(`Validation: ${errors.length} error(s)`);
      for (const e of errors) console.log(`  x ${e}`);
    }
    if (warnings.length) {
      console.log(`Validation: ${warnings.length} warning(s)`);
      for (const w of warnings) console.log(`  ! ${w}`);
    }
  }
  console.log("");
}

// --- Wizards -------------------------------------------------------

async function pickFromList(items, formatItem, allowManual, manualLabel) {
  items.forEach((item, i) => console.log(`  ${i + 1}) ${formatItem(item, i)}`));
  if (allowManual) console.log(`  0) ${manualLabel}`);
  const ans = (await ask("Select: ")).trim();
  const n = Number(ans);
  if (allowManual && n === 0) return null;
  if (Number.isInteger(n) && n >= 1 && n <= items.length) return items[n - 1];
  console.log("Invalid selection, try again.");
  return pickFromList(items, formatItem, allowManual, manualLabel);
}

async function addAffineConnection() {
  console.log("\n-- AFFiNE connection --");
  let urlIn = "";
  while (!urlIn) {
    urlIn = (await ask("Server URL of your self-hosted AFFiNE instance (e.g. https://your-host:3010): ")).trim();
    urlIn = urlIn.replace(/\/+$/, ""); // a trailing slash turns "url/api/..." into "url//api/..." -> 404
    if (!urlIn) console.log("A server URL is required - there is no usable default.");
  }
  const base = urlIn;
  const email = (await ask("Email: ")).trim();
  const password = await askPassword("Password: ");

  let session;
  try {
    console.log("Signing in...");
    session = await affineSignIn(base, email, password);
    console.log(`OK - signed in as ${session.user.email}`);
  } catch (e) {
    console.log(`Sign-in failed: ${e.message}`);
    const retry = (await ask("Save this connection anyway with unverified credentials? [y/N] ")).trim().toLowerCase();
    if (retry !== "y") return null;
    const workspaceId = (await ask("Workspace ID: ")).trim();
    return finishAffineConnection({ base: urlIn, email, password, workspaceId });
  }

  console.log("Fetching workspaces...");
  let workspaceId = null;
  try {
    const data = await affineGql(session, `query { workspaces { id initialized } }`);
    if (data.workspaces.length === 0) {
      console.log("No workspaces found on this account.");
    } else {
      const named = await affineWorkspaceNames(session, data.workspaces);
      const picked = await pickFromList(
        named,
        (w) => `${w.name || "(unnamed)"}  [${w.id}]  (initialized: ${w.initialized})`,
        true,
        "Enter a workspace ID manually",
      );
      workspaceId = picked ? picked.id : (await ask("Workspace ID: ")).trim();
    }
  } catch (e) {
    console.log(`Could not list workspaces: ${e.message}`);
  }
  if (!workspaceId) workspaceId = (await ask("Workspace ID: ")).trim();

  return finishAffineConnection({ base: urlIn, email, password, workspaceId, session });
}

async function finishAffineConnection({ base, email, password, workspaceId, session }) {
  console.log("\nTarget for notes:");
  const mode = await pickFromList(
    ["Journal (auto-created daily page)", "Pin to one specific page"],
    (x) => x,
    false,
  );
  const conn = { email, password, workspace_id: workspaceId };
  if (base) conn.url = base;

  if (mode.startsWith("Journal")) {
    conn.journal = true;
  } else {
    let pageId = null;
    if (session && workspaceId) {
      try {
        console.log("Fetching pages...");
        const pages = await affineListPages(session, workspaceId);
        if (pages.length) {
          const picked = await pickFromList(
            pages,
            (p) => `${p.title || "(untitled)"}  [${p.id}]`,
            true,
            "Enter a page ID manually",
          );
          pageId = picked ? picked.id : null;
        } else {
          console.log("No pages found in that workspace yet.");
        }
      } catch (e) {
        console.log(`Could not list pages: ${e.message}`);
      }
    }
    if (!pageId) pageId = (await ask("Page ID: ")).trim();
    conn.page_id = pageId;
  }

  const name = (await ask("\nName this connection (used in routing): ")).trim();
  return { name, type: "affine", ...conn };
}

async function addFileConnection(type) {
  console.log(`\n-- ${type} connection --`);
  const name = (await ask("Name this connection (used in routing): ")).trim();
  const p = (await ask("Path (absolute, or relative to %APPDATA%\\QuickNote): ")).trim();
  return { name, type, path: p };
}

async function addConnection() {
  console.log("\nConnection type:");
  const type = await pickFromList(CONNECTION_TYPES, (t) => t, false);
  if (type === "affine") return addAffineConnection();
  return addFileConnection(type);
}

async function testConnection(conn, configDir) {
  console.log(`\nTesting "${conn.name}" (${conn.type})...`);
  if (conn.type === "affine") {
    if (!conn.url || !conn.email || !conn.password || !conn.workspace_id) {
      console.log("  Missing url/email/password/workspace_id - fix validation errors first.");
      return;
    }
    const base = conn.url.replace(/\/+$/, "");
    try {
      const session = await affineSignIn(base, conn.email, conn.password);
      console.log(`  OK - signed in as ${session.user.email}`);
      const socket = await affineConnect(session);
      await affineEmit(socket, "space:join", {
        spaceType: "workspace",
        spaceId: conn.workspace_id,
        clientVersion: "0.27.4",
      });
      const ack = await affineEmit(socket, "space:load-doc", {
        spaceType: "workspace",
        spaceId: conn.workspace_id,
        docId: conn.workspace_id,
      });
      socket.close();
      if (ack.error) {
        console.log(`  Workspace join/load failed: ${JSON.stringify(ack.error)}`);
      } else {
        console.log(`  OK - workspace ${conn.workspace_id} is reachable`);
      }
    } catch (e) {
      console.log(`  Failed: ${e.message}`);
    }
    return;
  }

  // File-based: confirm the directory exists or can be created, and is writable.
  const resolved = path.isAbsolute(conn.path) ? conn.path : path.join(configDir, conn.path || "");
  try {
    fs.mkdirSync(resolved, { recursive: true });
    fs.accessSync(resolved, fs.constants.W_OK);
    console.log(`  OK - ${resolved} exists and is writable`);
  } catch (e) {
    console.log(`  Failed: ${e.message}`);
  }
}

async function editRouting(doc, connectionNames) {
  console.log("\n-- Routing --");
  console.log(`Known connections: ${connectionNames.join(", ") || "(none)"}`);
  const current = doc.getIn(["routing"], true)?.toJSON?.() ?? {};
  console.log(`Current default: ${current.default ?? "(unset)"}`);
  console.log(`Current tags: ${JSON.stringify(current.tags ?? {})}`);

  console.log("\n1) Set default connection");
  console.log("2) Map a tag to a connection");
  console.log("0) Back");
  const choice = (await ask("Select: ")).trim();
  if (choice === "1") {
    const name = (await ask(`Default connection (${connectionNames.join(", ")}): `)).trim();
    if (!connectionNames.includes(name)) {
      console.log("Not a known connection name - not changed.");
    } else {
      doc.setIn(["routing", "default"], name);
      console.log("Updated.");
    }
  } else if (choice === "2") {
    const tag = (await ask("Tag (without '#'): ")).trim();
    const name = (await ask(`Connection (${connectionNames.join(", ")}): `)).trim();
    if (!connectionNames.includes(name)) {
      console.log("Not a known connection name - not changed.");
    } else {
      doc.setIn(["routing", "tags", tag], name);
      console.log("Updated.");
    }
  }
}

// --- Main -------------------------------------------------------

async function main() {
  const args = process.argv.slice(2);
  const configFlagIdx = args.indexOf("--config");
  const configPath = configFlagIdx !== -1 ? args[configFlagIdx + 1] : liveConfigPath();
  const configDir = path.dirname(configPath);

  console.log("=== QuickNote Setup ===");
  console.log(`Config: ${configPath}\n`);

  const doc = await ensureConfig(configPath);
  let dirty = false;
  let report = validate(doc, configDir);
  printReport(report);

  let running = true;
  while (running) {
    console.log("1) Re-run validation");
    console.log("2) List connections");
    console.log("3) Add a connection");
    console.log("4) Test a connection");
    console.log("5) Edit routing");
    console.log("6) Remove a connection");
    console.log(`7) Save${dirty ? " (unsaved changes)" : ""}`);
    console.log("0) Exit");
    const choice = (await ask("\n> ")).trim();

    if (choice === "1") {
      report = validate(doc, configDir);
      printReport(report);
    } else if (choice === "2") {
      const conns = doc.get("connections", true)?.toJSON?.() ?? [];
      if (!conns.length) console.log("(no connections)");
      for (const c of conns) console.log(`  - ${c.name} [${c.type}]`);
    } else if (choice === "3") {
      const conn = await addConnection();
      if (conn) {
        doc.get("connections", true).add(doc.createNode(conn));
        dirty = true;
        report = validate(doc, configDir);
        printReport(report);
      }
    } else if (choice === "4") {
      const conns = doc.get("connections", true)?.toJSON?.() ?? [];
      if (!conns.length) {
        console.log("(no connections)");
      } else {
        const picked = await pickFromList(conns, (c) => `${c.name} [${c.type}]`, false);
        await testConnection(picked, configDir);
      }
    } else if (choice === "5") {
      await editRouting(doc, report.connectionNames);
      dirty = true;
    } else if (choice === "6") {
      const seq = doc.get("connections", true);
      const conns = seq?.toJSON?.() ?? [];
      if (!conns.length) {
        console.log("(no connections)");
      } else {
        const picked = await pickFromList(conns, (c) => `${c.name} [${c.type}]`, false);
        const idx = conns.findIndex((c) => c.name === picked.name);
        seq.delete(idx);
        dirty = true;
        report = validate(doc, configDir);
        printReport(report);
      }
    } else if (choice === "7") {
      if (fs.existsSync(configPath)) {
        fs.copyFileSync(configPath, `${configPath}.bak`);
      }
      fs.mkdirSync(configDir, { recursive: true });
      fs.writeFileSync(configPath, doc.toString());
      dirty = false;
      console.log(`Saved. Previous version backed up to ${configPath}.bak`);
    } else if (choice === "0") {
      if (dirty) {
        const ans = (await ask("Unsaved changes - exit anyway? [y/N] ")).trim().toLowerCase();
        if (ans !== "y") continue;
      }
      running = false;
    } else {
      console.log("Unknown choice.");
    }
  }

  rl.close();
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
