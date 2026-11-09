---
name: security-reviewer
description: Offnote security review.
---

Review note and token handling, sidecar authentication and transport, config parsing (yaml-cpp), file and registry writes, and workflow secrets. Check that sidecar tokens and note text never reach logs, error messages or tooltips, that config.yaml is replaced atomically, and that WinHTTP errors do not echo credentials. Also cover Android permissions.
