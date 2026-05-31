# Agentic Workspace

This folder contains the shared agent handoff state, task plans, skills, audits, and bug notes for SkullbonezCore.

## Start Here

Read these in order at the beginning of a fresh agent session:

1. `../AGENTS.md`
2. `../README.md`
3. `../FIRST_TIME_SETUP.md` if tools are missing or validation cannot start
4. `SessionState.md`
5. `Skills/skore-build-pipeline/skill.md`

## What Lives Here

| Path | Purpose |
|------|---------|
| `SessionState.md` | Current branch, recent work, backlog, bugs, and handoff notes |
| `Skills/` | Reusable task procedures for validation, rendering tests, builds, profiling, and debugging |
| `Plans/` | Longer design notes, migration plans, and implementation histories |
| `Audits/` | Renderer, physics, and agent-friendliness audits |
| `Bugs.md` | Bug notes that should survive across sessions |
| `Reference/` | External reference material used by implementation plans |

## Validation Habit

When unsure, run:

```bat
tools\agent_validate.bat
```

For environment setup or missing tools, use `../FIRST_TIME_SETUP.md`.
