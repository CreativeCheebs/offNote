// Entry point for the dedicated celebration overlay window (confetti.html).
// Lives apart from the note popup so the popup can hide the instant a note
// is saved without cutting the confetti animation off with it.
//
// This window needs its own capability grant (src-tauri/capabilities/confetti.json)
// for listen()/hide()/setIgnoreCursorEvents() to work - Tauri v2's per-window
// permission ACL rejects unlisted IPC calls silently (the call just rejects,
// nothing throws visibly), which cost real time to track down.
import { createConfetti } from "./confetti.js";

const { listen } = window.__TAURI__.event;
const { getCurrentWindow } = window.__TAURI__.window;

const win = getCurrentWindow();
const confetti = createConfetti(document.getElementById("confetti"));

// Full-screen and always-on-top, so make sure it never intercepts clicks
// meant for whatever is underneath it.
win.setIgnoreCursorEvents(true);

listen("celebrate", () => {
  confetti.burst(() => {
    confetti.clear();
    win.hide();
  });
});
