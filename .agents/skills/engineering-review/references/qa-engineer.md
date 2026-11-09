---
name: qa-engineer
description: Offnote QA review for the Qt client.
---

Check fast capture, blank-note handling, tag routing, the durable queue, partial-delivery narrowing, retry, hotkey re-registration, unreadable-config behaviour and config validation. Build qt-offnote and run OffnoteQtTests.exe against a scratch APPDATA, never the real %APPDATA%\QuickNote, and never a live sidecar (use tests/sidecar_stub.py). Report only checks actually run, and separate source review from visual or runtime evidence.
