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

// Builds the exact shape the Rust `AppConfig`/`Connection` structs expect (see
// src-tauri/src/lib.rs), matching the same field names as config.yaml. Shared
// by the full-config Save payload and the per-card Test button.
function connectionPayload(c) {
  const base = { name: c.name.trim(), type: c.type };
  if (c.type === "affine") {
    base.sidecar_url = c.sidecarUrl.trim() || null;
    base.sidecar_token = c.sidecarToken.trim() || null;
    base.sidecar_connector = c.sidecarConnector.trim() || null;
  } else {
    base.path = c.path.trim() || null;
  }
  return base;
}

// Mirrors `validate_connection` in the Rust backend so the settings window
// shows the same complaint before a save or a test round-trips there. Applied
// live (on every keystroke) as well as before Save/Test, per "check and
// validate the fields if there's any issue" - errors show inline on the card
// itself, not just in a summary at save time.
function connectionErrors(c) {
  if (!c.name) return ["needs a name"];
  if (c.type === "affine") {
    if (!c.sidecar_url || !c.sidecar_token || !c.sidecar_connector) {
      return ["needs sidecar URL, token, and connector name"];
    }
  } else if (!c.path) {
    return ["needs a folder path"];
  }
  return [];
}

async function testConnection(conn, messageEl, btn) {
  const payload = connectionPayload(conn);
  const errors = connectionErrors(payload);
  if (errors.length) {
    messageEl.textContent = `Can't test - ${errors.join(", ")}.`;
    messageEl.classList.add("error");
    messageEl.classList.remove("success");
    return;
  }
  btn.disabled = true;
  messageEl.textContent = "Testing...";
  messageEl.classList.remove("error", "success");
  try {
    await invoke("test_connection", { connection: payload });
    messageEl.textContent = 'Sent "current time testing" - check the destination.';
    messageEl.classList.add("success");
    messageEl.classList.remove("error");
  } catch (err) {
    messageEl.textContent = typeof err === "string" ? err : "Test failed";
    messageEl.classList.add("error");
    messageEl.classList.remove("success");
  } finally {
    btn.disabled = false;
  }
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

    const typeSelect = document.createElement("select");
    for (const t of CONNECTION_TYPES) {
      const opt = document.createElement("option");
      opt.value = t;
      opt.textContent = t;
      if (t === conn.type) opt.selected = true;
      typeSelect.appendChild(opt);
    }

    const testBtn = document.createElement("button");
    testBtn.type = "button";
    testBtn.textContent = "Test";
    testBtn.className = "test-btn";

    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.textContent = "Remove";
    removeBtn.className = "remove-btn";

    header.append(nameInput, typeSelect, testBtn, removeBtn);
    card.appendChild(header);

    const fieldsWrap = document.createElement("div");
    fieldsWrap.className = "card-fields";
    const fieldInputs = {};
    for (const field of fieldsForType(conn.type)) {
      const row = document.createElement("div");
      row.className = "field-row";
      const label = document.createElement("label");
      label.textContent = fieldLabel(field);
      const input = document.createElement("input");
      input.type = field === "sidecarToken" ? "password" : "text";
      input.value = conn[field] || "";
      input.placeholder = fieldPlaceholder(conn.type, field);
      fieldInputs[field] = input;
      row.append(label, input);
      fieldsWrap.appendChild(row);
    }
    card.appendChild(fieldsWrap);

    const footer = document.createElement("div");
    footer.className = "card-footer";
    const messageEl = document.createElement("span");
    messageEl.className = "card-message";
    footer.appendChild(messageEl);
    card.appendChild(footer);

    // Live validation: recomputed on every keystroke in this card, so a
    // problem shows up immediately rather than only when Save/Test is clicked.
    function refreshValidity() {
      const errors = connectionErrors(connectionPayload(conn));
      messageEl.classList.remove("success");
      if (errors.length) {
        messageEl.textContent = `This connection ${errors.join(", ")}.`;
        messageEl.classList.add("error");
      } else {
        messageEl.textContent = "";
        messageEl.classList.remove("error");
      }
      nameInput.classList.toggle("invalid", !conn.name.trim());
      for (const [field, input] of Object.entries(fieldInputs)) {
        input.classList.toggle("invalid", !conn[field] || !conn[field].trim());
      }
    }

    nameInput.addEventListener("input", () => {
      conn.name = nameInput.value;
      renderRoutingOptions();
      refreshValidity();
    });
    typeSelect.addEventListener("change", () => {
      conn.type = typeSelect.value;
      renderConnections();
      renderRoutingOptions();
    });
    for (const [field, input] of Object.entries(fieldInputs)) {
      input.addEventListener("input", () => {
        conn[field] = input.value;
        refreshValidity();
      });
    }
    removeBtn.addEventListener("click", () => {
      connections = connections.filter((c) => c.id !== conn.id);
      renderConnections();
      renderRoutingOptions();
    });
    testBtn.addEventListener("click", () => testConnection(conn, messageEl, testBtn));

    refreshValidity();
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

function toConfigPayload() {
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
    connections: connections.map(connectionPayload),
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

// Aggregate, cross-cutting checks (duplicates, routing) on top of the
// per-connection checks in `connectionErrors`.
function validate(payload) {
  const errors = [];
  const seen = new Set();
  for (const c of payload.connections) {
    const cErrors = connectionErrors(c);
    if (cErrors.length) {
      errors.push(`Connection "${c.name || "(unnamed)"}" ${cErrors.join(", ")}.`);
      continue;
    }
    if (seen.has(c.name)) {
      errors.push(`Duplicate connection name "${c.name}".`);
      continue;
    }
    seen.add(c.name);
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
