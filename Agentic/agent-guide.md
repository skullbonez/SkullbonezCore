# Agent Guide

This file is intentionally small. `AGENTS.md` is the authoritative rule set; this guide only points agents at the lowest-context starting path.

## Start

1. Read `AGENTS.md`.
2. Read `README.md`.
3. Read `Agentic/SessionState.md`.
4. Load only the skill needed for the current task.

Do not load every skill during initialization. The skill files are reference material, not default context.

## Always Remember

- Do not submit, force-push, rebase, or rewrite git history.
- Do not kill processes by name. Kill only by PID captured from the process you launched.
- Before editing, identify the impacted area and the validation command you plan to run.
- After editing, run the validation script mapped in `AGENTS.md`.
- Ask before committing or pushing unless the user explicitly requested that action.

## Common Skills

| Task | Skill |
|------|-------|
| Build | `Agentic/Skills/skore-build/skill.md` |
| Full verify and commit prep | `Agentic/Skills/skore-build-pipeline/skill.md` |
| Renderer validation | `Agentic/Skills/skore-render-test/skill.md` |
| Crash or hang debugging | `Agentic/Skills/skore-cdb-debug/skill.md` |
| Launching the exe | `Agentic/Skills/skore-launch/skill.md` |
| CPU bottleneck investigation | `Agentic/Skills/skore-cpu-profiler/skill.md` |

## Reference

- Runtime commands and scene directives: `Agentic/Reference/runtime-reference.md`
- Physics overview: `Agentic/Reference/physics-overview.md`
- Current handoff state: `Agentic/SessionState.md`
