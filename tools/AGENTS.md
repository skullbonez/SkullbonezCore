# Tools Instructions

Follow the root `../AGENTS.md` contract first. This folder contains validation,
diagnostic, formatting, and helper scripts.

## Local Rules

- Tool changes are not documentation-only. At the PR/commit gate, run
  `tools\validate_fast.bat`, then run the changed script's own self-check or
  focused command when it has one.
- Renderer validation helpers must fail fast when Profile build fails.
- Preserve PID-scoped process handling. Do not add process-name kills such as
  `taskkill /IM`.
- Prefer structured parsers and explicit schemas over ad hoc text scraping for
  validation artifacts.
- Keep generated validation output under `TestOutput/validation` or the
  workflow-specific artifact path, not in source directories.
