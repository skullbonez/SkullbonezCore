---
applyTo: "tools/**"
---

# Tools Guidance

Tool changes require `tools\validate_fast.bat` at the PR/commit gate, plus the
changed script's own focused self-check when available.

Renderer validation tooling must fail fast after build failure. Preserve
PID-scoped process handling and avoid process-name kills.
