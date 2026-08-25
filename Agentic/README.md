# Agentic Workspace

This folder contains agent handoff state, task-specific skills, live plans,
audits, and reference material.

## Fresh Agent Start

Follow the startup contract in `../AGENTS.md`. Load only the skill, plan, audit,
or reference needed for the current task.

## Contents

| Path | Purpose |
|---|---|
| `SessionState.md` | Short operational state: branch, active objective, blockers, next work |
| `Skills/` | Task procedures and helpers |
| `Plans/MASTER-PLAN.md` | Authoritative inventory, checked-phase counts, priority, decisions, and closure rules |
| `Plans/TODO/` | Every live implementation plan and execution checklist |
| `Plans/WNF/` | Owner-parked “will not do now” plans; agents ignore them unless the owner explicitly restores them to `TODO/` |
| `Audits/` | Renderer, physics, and process audits loaded on demand |
| `Reference/` | Runtime, physics, style, and external reference material |
| `Tests/` | Standalone CPU test projects |

Completed plans/checklists are deleted; git history is the archive. Per-run
investigation and closure evidence belongs in the commit body and the owning
plan, not in a committed report tree. A completed
plan may remain temporarily only when `MASTER-PLAN.md` explicitly retains it
for an unmet aggregate closure gate, and is deleted when that gate passes. Do not
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
- `python ../tools/check_related_paths.py --repo ..` is an advisory stale-
  reference report for review; unresolved paths do not block validation.
- `Skills/comment-style-audit/skill.md` is the touched-file/full-scope audit.

## Hot Paths

`../AGENTS.md` is authoritative: physics, collision, audio classification,
render submission, and similar per-frame code use compact arrays/value records
and explicit side-effect buffers. New runtime inheritance requires an owning
plan proving why value composition is insufficient and naming call frequency
and validation/perf evidence.

Replay is an upper Runtime consumer. The Replay Boundary Rule in `../AGENTS.md`
forbids downward `Runtime/Replay/*` includes and requires every replay reserve
registration or privilege change to stay reconciled with the authoritative
owner/phase/cap/counter inventory.

## Validation

Validation scripts are pre-commit/PR gates, not routine iteration. Ordinary
commits use the cumulative focused gates mapped in `../AGENTS.md`.
`validate_full --plan-completion` is reserved for terminal closure of an entire
implementation plan; it runs the mandatory CPU umbrella before either runtime
lane. That umbrella runs `validate_coverage` and enforces the ratified
subsystem floors. Run `tools\validate_coverage.bat` directly for changes to floors,
exclusions, instrumentation scope, coverage tooling, or tests intended to raise
subsystem coverage, and when explicit final-gate floor confirmation is needed.
Do not duplicate it after `validate_all_cpu_tests`,
`validate_full --plan-completion`, or `agent_validate --plan-completion`;
hosted mandatory CPU CI uses the same umbrella call chain.
