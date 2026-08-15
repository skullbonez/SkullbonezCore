# Session State

Date: 2026-08-15
Branch: `claude/codebase-overview-jatmcg`
Status: Two active plans registered; no implementation started

Determinism Envelope Tier-2 Hardening (9 tasks) and Causal Event Inspection
(9 tasks) are registered under `Agentic/Plans/TODO/`, 0 complete.
`MASTER-PLAN.md` carries the binding order.

The mandatory CPU CI lane was repaired this session; see the CI note below
before assuming a red lane is your change.

## Next Work

`TIER2_DETERMINISM` T0. Establish the pre-change envelope and determine whether
the statically linked UCRT dispatches on processor features for `sinf`, `cosf`,
and `acosf`. This needs a Windows build and either a disassembly of the linked
routines or the same binary executed on two different microarchitectures. No
source edit.

`CAUSAL_INSPECT` C0. Fix the seek contract: confirm every cause row kind
addresses a frame the scrubber can restore, and define the refusal behavior for a
row whose frame has aged out of the recorder ring. C2 then picks the transport
semantics, which decides what "fast forward to the causal moment" means for the
whole feature; the plan recommends restoring to a short lead-in and running
forward while the camera arrives.

`TIER2_DETERMINISM` T1, T2, T5 through T8 and all of `CAUSAL_INSPECT` are
unblocked and change no physics output.

## Blockers

- `TIER2_DETERMINISM` T3 and T4 change physics bits by construction. The plan
  grants no baseline-refresh authority; each needs its own owner approval of that
  exact transition, reviewed as behavior.
- T0's CRT dispatch question cannot be answered from a Linux session. It needs
  Windows tooling.

## CI Note

`.github/workflows/mandatory-cpu-validation.yml` now installs the pinned
clang-format 21.1.8 from the PyPI wheel instead of the Chocolatey community feed.
The feed dropped that version on 2026-08-15, `choco` still exited 0 after
upgrading 0/1 packages, and only the workflow's explicit version assertion caught
that the runner's pre-installed 20.1.8 had been left in place. Do not repin
downward: `.clang-format:43` sets `BinPackLongBracedList`, which clang-format 20
rejects outright, and all 651 tracked C++ sources format differently under 20.

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
