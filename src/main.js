const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;

const noteEl = document.getElementById("note");
const statusEl = document.getElementById("status");
const win = getCurrentWindow();

let saveCombo = { alt: true, ctrl: false, shift: false, meta: false, key: "enter" };

// Parses accelerator strings like "Alt+Enter" or "CmdOrCtrl+Shift+N" from config.yaml
// into a plain object we can match against keydown events.
function parseShortcut(spec) {
  const parts = spec.split("+").map((p) => p.trim().toLowerCase());
  const combo = { alt: false, ctrl: false, shift: false, meta: false, key: "" };
  for (const part of parts) {
    if (part === "alt" || part === "option") combo.alt = true;
    else if (part === "ctrl" || part === "control" || part === "cmdorctrl" || part === "commandorcontrol")
      combo.ctrl = true;
    else if (part === "shift") combo.shift = true;
    else if (part === "cmd" || part === "command" || part === "super" || part === "meta") combo.meta = true;
    else combo.key = part;
  }
  return combo;
}

function matchesCombo(e, combo) {
  const key = e.key.toLowerCase();
  return (
    e.altKey === combo.alt &&
    e.ctrlKey === combo.ctrl &&
    e.shiftKey === combo.shift &&
    e.metaKey === combo.meta &&
    key === combo.key
  );
}

async function loadConfig() {
  try {
    const config = await invoke("get_config");
    saveCombo = parseShortcut(config.shortcuts.save_note);
  } catch (err) {
    console.error("failed to load config", err);
  }
}

async function saveAndClose() {
  const text = noteEl.value;
  if (!text.trim()) {
    await win.hide();
    return;
  }
  try {
    await invoke("save_note", { text });
    statusEl.textContent = "Saved";
  } catch (err) {
    statusEl.textContent = "Error saving note";
    console.error(err);
    return;
  }
  noteEl.value = "";
  setTimeout(() => {
    statusEl.textContent = "";
  }, 800);
  await win.hide();
}

noteEl.addEventListener("keydown", (e) => {
  if (matchesCombo(e, saveCombo)) {
    e.preventDefault();
    saveAndClose();
    return;
  }
  if (e.key === "Escape") {
    e.preventDefault();
    noteEl.value = "";
    win.hide();
  }
});

listen("note-shown", () => {
  noteEl.value = "";
  statusEl.textContent = "";
  noteEl.focus();
});

loadConfig();
window.addEventListener("focus", () => noteEl.focus());
