# Agentic Workspace

This folder contains agent handoff state, task-specific skills, live plans,
audits, reports, and reference material.

## Fresh Agent Start

Follow the startup contract in `../AGENTS.md`. Load only the skill, plan, audit,
report, or reference needed for the current task.

## Contents

| Path | Purpose |
|---|---|
| `SessionState.md` | Short operational state: branch, active objective, blockers, next work |
| `Skills/` | Task procedures and helpers |
| `Plans/MASTER-PLAN.md` | Authoritative inventory, checked-phase counts, priority, decisions, and closure rules |
| `Plans/TODO/` | Every live implementation plan and execution checklist |
| `Audits/` | Renderer, physics, and process audits loaded on demand |
| `Bugs.md` | Persistent product bug notes |
| `Reference/` | Runtime, physics, style, and external reference material |
| `Reports/` | Validation/investigation evidence; not plan status authority |

Completed plans/checklists are deleted; git history is the archive. Do not
recreate `Done`, `Failed`, `Rejected`, `To_Eval`, `In_Progress`, or
`awaiting_verification` plan folders.

## Plan Quality

- Use checked phase/file counts, not subjective percentages.
- A plan names owner, dated evidence, goal, phases, dependencies/decisions,
  acceptance, deletion or behavioral proof, and exact validation.
- Every dependency path resolves to a live file.
- A checked phase includes its required evidence; prose claiming completion is
  insufficient.
- `Plans/MASTER-PLAN.md` and `SessionState.md` update with every plan closure.

## Comment Quality

- `Reference/comment-style-guide.md` defines the standard.
- Full/subsystem remediation starts with a `git ls-files` checklist as required
  by `../AGENTS.md`.
- The boilerplate cleanup completed on 2026-07-10. Do not recreate generic
  learning headers; teach file-specific vocabulary, ownership, invariants,
  lifetime, hazards, and validation-sensitive behavior.
- The active stale-reference inventory is
  `Plans/TODO/stale-plan-reference-cleanup-15.6-checklist.md`.
- `Skills/comment-style-audit/skill.md` is the touched-file/full-scope audit.

## Hot Paths

`../AGENTS.md` is authoritative: physics, collision, audio classification,
render submission, and similar per-frame code use compact arrays/value records
and explicit side-effect buffers. New runtime inheritance requires an owning
plan proving why value composition is insufficient and naming call frequency
and validation/perf evidence.

## Validation

Validation scripts are pre-commit/PR gates, not routine iteration. The current
gap and remediation are tracked in
`Plans/TODO/validation-gate-integrity.md`; until it completes, do not assume
`validate_full` runs every standalone CPU test target.
