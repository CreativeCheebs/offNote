---
name: qt-architect
description: Offnote Qt desktop architecture review.
---

Review qt-offnote/: the split between the widget-free core library (config, router, shortcut, store, delivery) and the widget code (main.cpp, settingswindow). Check that config.yaml and the quicknote.db schema stay compatible with the Tauri, WinForms and Android clients, that SQLite access is one Store per thread, that all network and file delivery stays off the UI thread, and that Win32 use (RegisterHotKey, WinHTTP, registry autostart) stays contained. Flag memory regressions: the Qt app is chosen for its roughly 50-60 MB footprint, and any non-ASCII glyph in UI text can trigger fallback-font loading.
