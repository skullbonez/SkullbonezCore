# Session State

Date: 2026-08-15
Branch: `claude/codebase-overview-jatmcg`
Status: One active plan registered; no implementation started

Determinism Envelope Tier-2 Hardening is registered at
`Agentic/Plans/TODO/determinism-envelope-tier2-hardening.md` with 9 tasks, 0
complete. The portfolio is no longer empty; `MASTER-PLAN.md` carries the binding
order.

## Next Work

T0. Establish the pre-change envelope and determine whether the statically
linked UCRT dispatches on processor features for `sinf`, `cosf`, and `acosf`.
This needs a Windows build and either a disassembly of the linked routines or the
same binary executed on two different microarchitectures. No source edit.

T1, T2, and T5 through T8 are unblocked and do not change physics output. T3 and
T4 are blocked on an explicit owner baseline decision and must not be started
without it.

## Blockers

- T3 and T4 change physics bits by construction. The plan grants no
  baseline-refresh authority; each needs its own owner approval of that exact
  transition, reviewed as behavior.
- T0's CRT dispatch question cannot be answered from a Linux session. It needs
  Windows tooling.

## Finding That Motivated The Plan

A `SkullbonezSource/Physics` scan reports one `acosf` and no other
implementation-defined transcendental, which understates the exposure. The trig
is one call level down: `Quaternion::RotateAboutAxis`
(`SkullbonezSource/Maths/Quaternion.cpp:94-95`) calls `sinf` and `cosf`, and
`IntegrateBodyRecordPose` at `SkullbonezSource/Physics/PhysicsBodyStore.cpp:1007`
calls it for every rotating body every tick. Any future scan of this class must
cover `Maths` as well as `Physics`, which is why the T2 gate scopes both.

Separately, `SkullbonezSource/Physics/Ragdoll.cpp:253` passes an unclamped dot
product to `acosf`; a normalized dot product routinely exceeds 1.0 by an ULP and
`acos` returns NaN. That is a live correctness defect independent of the
determinism work, and T4 lands the clamp as its own reviewable change.

## Repository Presentation Conventions

Still current; read before your first commit.

- `Agentic/Reports/` is deleted and must not be recreated. Closure and
  investigation evidence belongs in the commit body and the owning plan. Git
  history is the archive. Source `Related:` blocks cite only durable targets —
  source, `tools/`, `Agentic/Reference/`, or a root document.
- The commit progress header carries no percentage. Use
  `<PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>` and keep the whole
  subject under 72 characters. Commits made outside a plan runner, including
  plan registration, use normal subject rules and claim no plan progress.
- `tools/validate_build_all.bat` builds Automation, Debug, and Profile.
  `validate_fast` calls it, because the compiled-symbol reachability scan reads
  three object roots.

Ownership rulings pin exact line numbers. Removing or adding a comment line
above a ruled aggregate shifts its recorded `site` and fails `validate_fast`;
re-derive sites from `inventory_authority_free_aggregates.py --format json`
rather than editing them by hand.
