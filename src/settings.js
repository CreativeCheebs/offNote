const { invoke } = window.__TAURI__.core;
const { getCurrentWindow } = window.__TAURI__.window;

const win = getCurrentWindow();

const CONNECTION_TYPES = ["markdown", "obsidian", "logseq", "affine"];

const connectionsEl = document.getElementById("connections");
const tagRoutesEl = document.getElementById("tag-routes");
const defaultConnectionEl = document.getElementById("default-connection");
const toggleShortcutEl = document.getElementById("toggle-shortcut");
const saveShortcutEl = document.getElementById("save-shortcut");
const errorsEl = document.getElementById("errors");
const statusEl = document.getElementById("status");

// In-memory form state, independent of the YAML shape - the ids here are only
// for tracking rows across re-renders and never leave this file.
let connections = [];
let tagRoutes = [];
let nextId = 1;
const uid = () => nextId++;

function fieldsForType(type) {
  return type === "affine" ? ["sidecarUrl", "sidecarToken", "sidecarConnector"] : ["path"];
}

function fieldLabel(field) {
  switch (field) {
    case "path":
      return "Folder path";
    case "sidecarUrl":
      return "Sidecar URL";
    case "sidecarToken":
      return "Sidecar token";
    case "sidecarConnector":
      return "Sidecar connector name";
    default:
      return field;
  }
}

function fieldPlaceholder(type, field) {
  if (field === "path") {
    return type === "logseq" ? "e.g. logseq-vault" : "e.g. notes (relative to %APPDATA%\\QuickNote, or absolute)";
  }
  if (field === "sidecarUrl") return "http://your-sidecar-host:8787";
  if (field === "sidecarToken") return "token printed by install.sh";
  if (field === "sidecarConnector") return "name from the sidecar's connectors.json";
  return "";
}

function renderConnections() {
  connectionsEl.innerHTML = "";
  for (const conn of connections) {
    const card = document.createElement("div");
    card.className = "card";

    const header = document.createElement("div");
    header.className = "card-header";

    const nameInput = document.createElement("input");
    nameInput.type = "text";
    nameInput.placeholder = "connection name";
    nameInput.value = conn.name;
    nameInput.addEventListener("input", () => {
      conn.name = nameInput.value;
      renderRoutingOptions();
    });

    const typeSelect = document.createElement("select");
    for (const t of CONNECTION_TYPES) {
      const opt = document.createElement("option");
      opt.value = t;
      opt.textContent = t;
      if (t === conn.type) opt.selected = true;
      typeSelect.appendChild(opt);
    }
    typeSelect.addEventListener("change", () => {
      conn.type = typeSelect.value;
      renderConnections();
    });

    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.textContent = "Remove";
    removeBtn.className = "remove-btn";
    removeBtn.addEventListener("click", () => {
      connections = connections.filter((c) => c.id !== conn.id);
      renderConnections();
      renderRoutingOptions();
    });

    header.append(nameInput, typeSelect, removeBtn);
    card.appendChild(header);

    const fieldsWrap = document.createElement("div");
    fieldsWrap.className = "card-fields";
    for (const field of fieldsForType(conn.type)) {
      const row = document.createElement("div");
      row.className = "field-row";
      const label = document.createElement("label");
      label.textContent = fieldLabel(field);
      const input = document.createElement("input");
      input.type = field === "sidecarToken" ? "password" : "text";
      input.value = conn[field] || "";
      input.placeholder = fieldPlaceholder(conn.type, field);
      input.addEventListener("input", () => {
        conn[field] = input.value;
      });
      row.append(label, input);
      fieldsWrap.appendChild(row);
    }
    card.appendChild(fieldsWrap);
    connectionsEl.appendChild(card);
  }
}

function renderRoutingOptions() {
  const names = connections.map((c) => c.name.trim()).filter(Boolean);
  const prevDefault = defaultConnectionEl.value;
  defaultConnectionEl.innerHTML = "";
  for (const name of names) {
    const opt = document.createElement("option");
    opt.value = name;
    opt.textContent = name;
    defaultConnectionEl.appendChild(opt);
  }
  if (names.includes(prevDefault)) defaultConnectionEl.value = prevDefault;

  renderTagRoutes(names);
}

function renderTagRoutes(names) {
  const connectionNames = names ?? connections.map((c) => c.name.trim()).filter(Boolean);
  tagRoutesEl.innerHTML = "";
  for (const route of tagRoutes) {
    const row = document.createElement("div");
    row.className = "field-row";

    const tagInput = document.createElement("input");
    tagInput.type = "text";
    tagInput.placeholder = "#tag (without the #)";
    tagInput.value = route.tag;
    tagInput.addEventListener("input", () => (route.tag = tagInput.value));

    const select = document.createElement("select");
    for (const name of connectionNames) {
      const opt = document.createElement("option");
      opt.value = name;
      opt.textContent = name;
      if (name === route.connection) opt.selected = true;
      select.appendChild(opt);
    }
    select.addEventListener("change", () => (route.connection = select.value));

    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.textContent = "Remove";
    removeBtn.className = "remove-btn";
    removeBtn.addEventListener("click", () => {
      tagRoutes = tagRoutes.filter((r) => r.id !== route.id);
      renderTagRoutes();
    });

    row.append(tagInput, select, removeBtn);
    tagRoutesEl.appendChild(row);
  }
}

function addConnection() {
  connections.push({
    id: uid(),
    name: "",
    type: "markdown",
    path: "",
    sidecarUrl: "",
    sidecarToken: "",
    sidecarConnector: "",
  });
  renderConnections();
  renderRoutingOptions();
}

function addTagRoute() {
  const names = connections.map((c) => c.name.trim()).filter(Boolean);
  tagRoutes.push({ id: uid(), tag: "", connection: names[0] || "" });
  renderTagRoutes();
}

// Builds the exact shape the Rust `AppConfig` struct expects (see
// src-tauri/src/lib.rs), matching the same field names as config.yaml.
function toConfigPayload() {
  const conns = connections.map((c) => {
    const base = { name: c.name.trim(), type: c.type };
    if (c.type === "affine") {
      base.sidecar_url = c.sidecarUrl.trim() || null;
      base.sidecar_token = c.sidecarToken.trim() || null;
      base.sidecar_connector = c.sidecarConnector.trim() || null;
    } else {
      base.path = c.path.trim() || null;
    }
    return base;
  });
  const tags = {};
  for (const r of tagRoutes) {
    const tag = r.tag.trim();
    if (tag) tags[tag] = r.connection;
  }
  return {
    shortcuts: {
      toggle_note: toggleShortcutEl.value.trim(),
      save_note: saveShortcutEl.value.trim(),
    },
    connections: conns,
    routing: { tags, default: defaultConnectionEl.value },
  };
}

function fromConfig(config) {
  toggleShortcutEl.value = config.shortcuts?.toggle_note || "Alt+Space";
  saveShortcutEl.value = config.shortcuts?.save_note || "Alt+Enter";
  connections = (config.connections || []).map((c) => ({
    id: uid(),
    name: c.name,
    type: c.type,
    path: c.path || "",
    sidecarUrl: c.sidecar_url || "",
    sidecarToken: c.sidecar_token || "",
    sidecarConnector: c.sidecar_connector || "",
  }));
  renderConnections();
  renderRoutingOptions();
  if (config.routing?.default) defaultConnectionEl.value = config.routing.default;
  tagRoutes = Object.entries(config.routing?.tags || {}).map(([tag, connection]) => ({
    id: uid(),
    tag,
    connection,
  }));
  renderTagRoutes();
}

// Mirrors the checks in the Rust `validate_config` so the user sees the same
// errors here, before a save attempt round-trips to the backend.
function validate(payload) {
  const errors = [];
  const seen = new Set();
  for (const c of payload.connections) {
    if (!c.name) {
      errors.push("Every connection needs a name.");
      continue;
    }
    if (seen.has(c.name)) {
      errors.push(`Duplicate connection name "${c.name}".`);
      continue;
    }
    seen.add(c.name);
    if (c.type === "affine") {
      if (!c.sidecar_url || !c.sidecar_token || !c.sidecar_connector) {
        errors.push(`Connection "${c.name}" needs sidecar URL, token, and connector name.`);
      }
    } else if (!c.path) {
      errors.push(`Connection "${c.name}" needs a folder path.`);
    }
  }
  if (!payload.routing.default) errors.push("Pick a default connection.");
  else if (!seen.has(payload.routing.default)) errors.push('"Default connection" doesn\'t match any connection above.');
  for (const [tag, name] of Object.entries(payload.routing.tags)) {
    if (!seen.has(name)) errors.push(`Tag rule "#${tag}" points to a connection that no longer exists.`);
  }
  if (!payload.shortcuts.toggle_note) errors.push("Toggle-note shortcut can't be empty.");
  if (!payload.shortcuts.save_note) errors.push("Save-note shortcut can't be empty.");
  return errors;
}

async function loadConfig() {
  try {
    const config = await invoke("get_config");
    fromConfig(config);
  } catch (err) {
    statusEl.textContent = typeof err === "string" ? err : "Failed to load config";
    console.error(err);
  }
}

async function save() {
  const payload = toConfigPayload();
  const errors = validate(payload);
  if (errors.length) {
    errorsEl.hidden = false;
    errorsEl.innerHTML = errors.map((e) => `<div>${e}</div>`).join("");
    statusEl.textContent = "";
    return;
  }
  errorsEl.hidden = true;
  errorsEl.innerHTML = "";
  try {
    await invoke("save_config", { config: payload });
    statusEl.textContent = "Saved and applied.";
  } catch (err) {
    statusEl.textContent = typeof err === "string" ? err : "Failed to save config";
    console.error(err);
  }
}

document.getElementById("add-connection").addEventListener("click", addConnection);
document.getElementById("add-tag-route").addEventListener("click", addTagRoute);
document.getElementById("save-btn").addEventListener("click", save);
document.getElementById("cancel-btn").addEventListener("click", () => win.hide());

loadConfig();
