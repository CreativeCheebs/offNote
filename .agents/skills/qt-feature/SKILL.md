---
name: qt-feature
description: Implement features in the Offnote Qt 6 Widgets desktop client.
---

# Qt Feature

Implement features in qt-offnote/. Keep config, routing, SQLite and delivery in the widget-free core library and UI in main.cpp or settingswindow.cpp. Preserve durable local save before delivery, tag routing, retry, the config.yaml format and the shared quicknote.db schema. Keep UI text ASCII (a missing glyph triggers fallback fonts and large memory use). Build with qt-offnote/build.bat, add cases to tests/core_tests.cpp, run OffnoteQtTests.exe against a scratch APPDATA, and report checks actually run.
