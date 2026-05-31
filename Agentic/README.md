# Agentic Workspace

This folder contains agent handoff state, task-specific skills, long-running plans, audits, and reference material.

## Fresh Agent Start

Read only:
1. `../AGENTS.md`
2. `../README.md`
3. `SessionState.md`

If tools are missing or validation cannot start, read `../FIRST_TIME_SETUP.md`.

Load a skill from `Skills/` only when the current task calls for it.

## Contents

| Path | Purpose |
|------|---------|
| `SessionState.md` | Current branch, active work, blockers, and next validation. Keep this short. |
| `SessionHistory.md` | Older session notes that should not be loaded by default. |
| `Skills/` | Concise task procedures and helper scripts. |
| `Plans/` | Design notes and implementation histories. Load on demand. |
| `Audits/` | Renderer, physics, and process audits. Load on demand. |
| `Bugs.md` | Persistent bug notes. |
| `Reference/` | Runtime, physics, and external reference material. |

## Validation Habit

When unsure, run:

```bat
tools\agent_validate.bat
```
