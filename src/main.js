const { invoke } = window.__TAURI__.core;
const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;

const noteEl = document.getElementById("note");
const backdropEl = document.getElementById("backdrop");
const highlightsEl = document.getElementById("highlights");
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

const escapeHtml = (s) =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

// Rebuild the backdrop, wrapping every #tag in a <span class="tag">. A tag is a
// `#` at the start or after whitespace, immediately followed by non-space,
// non-`#` characters (mirrors extract_tags in the Rust backend so what's colored
// is exactly what routes). The regex is applied line by line so we can escape
// the surrounding text safely.
function renderHighlights() {
  const text = noteEl.value;
  const tagRe = /(^|\s)(#[^\s#]+)/g;
  let html = "";
  let last = 0;
  let m;
  while ((m = tagRe.exec(text)) !== null) {
    const tagStart = m.index + m[1].length;
    // Trim trailing punctuation (e.g. "#work.") so the colored run matches
    // exactly what extract_tags routes on in the Rust backend.
    let tag = m[2];
    tag = tag.replace(/[^\w/-]+$/u, "");
    if (tag.length <= 1) continue; // bare "#" or punctuation-only: not a tag
    html += escapeHtml(text.slice(last, tagStart));
    html += `<span class="tag">${escapeHtml(tag)}</span>`;
    last = tagStart + tag.length;
  }
  html += escapeHtml(text.slice(last));
  // Trailing newline needs a placeholder or the backdrop loses the last blank line.
  highlightsEl.innerHTML = html + "\n";
}

function syncScroll() {
  backdropEl.scrollTop = noteEl.scrollTop;
  backdropEl.scrollLeft = noteEl.scrollLeft;
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
    const result = await invoke("save_note", { text });
    const delivered = result?.delivered ?? [];
    const pending = result?.pending ?? [];
    // The note is always in SQLite by the time this resolves; `pending` only
    // means a connector was unreachable, so it's queued, not lost.
    if (pending.length) {
      statusEl.textContent = `Saved - delivery pending: ${pending.join(", ")}`;
    } else if (delivered.length) {
      statusEl.textContent = `Saved -> ${delivered.join(", ")}`;
    } else {
      statusEl.textContent = "Saved";
    }
  } catch (err) {
    // Only reached if the durable write itself failed.
    statusEl.textContent = typeof err === "string" ? err : "Error saving note";
    console.error(err);
    return;
  }
  noteEl.value = "";
  renderHighlights();
  setTimeout(() => {
    statusEl.textContent = "";
  }, 1200);
  await win.hide();
}

noteEl.addEventListener("input", () => {
  renderHighlights();
  syncScroll();
});
noteEl.addEventListener("scroll", syncScroll);

noteEl.addEventListener("keydown", (e) => {
  if (matchesCombo(e, saveCombo)) {
    e.preventDefault();
    saveAndClose();
    return;
  }
  if (e.key === "Escape") {
    e.preventDefault();
    noteEl.value = "";
    renderHighlights();
    win.hide();
  }
});

listen("note-shown", () => {
  noteEl.value = "";
  renderHighlights();
  statusEl.textContent = "";
  noteEl.focus();
});

loadConfig();
renderHighlights();
window.addEventListener("focus", () => noteEl.focus());
