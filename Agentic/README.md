# Agentic Workspace

This folder contains agent handoff state, task-specific skills, long-running plans, audits, and reference material.

## Fresh Agent Start

Follow the Agent Startup Contract in `../AGENTS.md`. Load a skill from
`Skills/` only when the current task calls for it.

## Contents

| Path | Purpose |
|------|---------|
| `SessionState.md` | Current branch, active work, blockers, and next validation. Keep this short. |
| `Skills/` | Concise task procedures and helper scripts. |
| `Plans/` | `MASTER-PLAN.md` (inventory + percent-complete for every remaining plan) and `TODO/` (consolidated active plans). Completed plans are deleted (git history is the archive); do not recreate `Done/`/`Failed/`/`Rejected/`/`To_Eval/`/`In_Progress/`. |
| `Audits/` | Renderer, physics, and process audits. Load on demand. |
| `Bugs.md` | Persistent bug notes. |
| `Reference/` | Runtime, physics, and external reference material. |

## Comment Quality

- `Reference/comment-style-guide.md` defines the repository comment standard.
- For full or subsystem comment remediation, create a scoped checklist plan
  from `git ls-files` per `../AGENTS.md`; the old repository-wide remediation
  plan was retired in the 2026-07-09 plan consolidation. Note
  `../engine-cleanup-plans/15-review-gaps.md` item 15.4: existing learning
  headers have decayed into copy-paste boilerplate — do not add more
  boilerplate headers while that cleanup is pending.
- `Reference/render-backend-portability-contract.md` defines the future
  Vulkan/Metal portability seam now that DX12 is the only active renderer.
- `Skills/comment-style-audit/skill.md` is the repeatable pass for checking
  touched files, or the full repository when explicitly requested.

## Hot Paths

- `../AGENTS.md` is the source of truth for hot-path data and inheritance rules:
  physics, collision, audio classification, render submission, and similar
  per-frame code should stay on compact arrays/value records and explicit
  side-effect buffers.
- New inheritance is banned unless an owning plan proves a stable
  runtime-polymorphic boundary is necessary and records the validation or perf
  evidence.
- Approved source-inheritance evidence belongs in the owning plan and review
  record; do not add a base class without recording why value composition is
  insufficient and what validation or perf evidence backs the boundary.

## Pre-Commit/PR Validation

Validation scripts are pre-commit/PR gates, not normal iteration steps. Choose
the narrowest validation for the fix. When the PR-bound scope is truly unsure,
run:

```bat
tools\agent_validate.bat
```
