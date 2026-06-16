# SkullbonezCore Copilot Instructions

`AGENTS.md` is the source of truth for repository rules. Follow its Agent
Startup Contract before editing.

SkullbonezCore is a Windows x64 C++17 graphics and physics engine with a DX12
production renderer. Runtime OpenGL and DX11 paths are retired; do not add new
GL/DX11 runtime dependencies.

## Process

- Validation scripts are formal pre-commit/PR gates, not after-every-edit
  checks.
- Documentation-only changes require no repository validation script.
- Use the narrowest validation gate from `AGENTS.md` for code, shader, scene,
  physics, baseline, or tooling changes.
- Kill launched processes by PID only. Do not suggest `taskkill /IM` or
  process-name kills.
- Do not force-push, rebase, rewrite history, or discard unrelated worktree
  changes.
- Do not commit or push directly on `main` unless the user explicitly requested
  that operation.
- Implementing work from `Agentic/Plans` defaults to the orchestrator workflow.
  Reading, drafting, or maintaining plan docs can stay ordinary documentation
  work.

## Review Focus

Lead with findings. Prioritize behavioral bugs, regressions, missing tests,
validation gaps, baseline mistakes, physics determinism risk, DX12 validation
errors, screenshot-diff reliability, and hot-path allocations. Include file and
line references when possible. Say clearly when no issues are found and mention
remaining validation risk.

## Scoped Guidance

Additional path-scoped guidance lives under `.github/instructions/` for docs,
tools, shaders, baselines, DX12, physics, and orchestrator work. Those files add
local reminders; `AGENTS.md` remains authoritative when instructions overlap.
