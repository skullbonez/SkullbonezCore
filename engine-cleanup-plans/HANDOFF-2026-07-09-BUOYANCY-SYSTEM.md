# Engine Cleanup Handoff - Plan 02 Buoyancy System Slice

## Stop Point

Paused after finishing, validating, committing, and preparing handoff for Plan
02 Step 2.2. This is a restart pause point only; the overall engine cleanup
goal remains active and incomplete.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `9a738c0e cleanup(02): move buoyancy snapshot out of physics world`

The next unchecked Plan 02 item is:

- `3.1` Replace the field-by-field
  `CaptureReplaySolverSnapshot` / `RestoreReplaySolverSnapshot` mirroring with
  one field list or serializable record. Gate: `tools\validate_physics.bat`
  plus replay scrub regression.

## What Landed

This continuation completed Plan 02 Step 2.2 and closed Phase 2:

- Added `SkullbonezSource/Physics/BuoyancySystem.h/.cpp`.
- Moved analytic sphere-cap submersion snapshots and fully-submerged ball
  classification out of `PhysicsWorld`.
- Replaced `PhysicsWorld`'s old shape-specific buoyancy helpers with
  `BuoyancySystem::RefreshUnderwaterSubmersionForBall` and
  `BuoyancySystem::IsFullySubmergedBall` calls from the existing underwater
  sleep-lock and wake paths.
- Kept the underwater sleep-lock vector/state in `PhysicsWorld` because that
  state is solver sleep policy, not analytic buoyancy ownership.
- Wired `BuoyancySystem.cpp` into `SKULLBONEZ_CORE` and `SKULLBONEZ_TESTS`.
- Added `BuoyancySystem` to the physics-prefix allowlist in
  `tools/validate_project_filters.py`.

Plan ledger updated:

- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`

## Validation Evidence

Current continuation gates:

- `tools\validate_build.bat Profile`
  - Log:
    `Agentic\Reports\validate_build_profile_buoyancy_system_2026-07-09.log`
  - Runtime: 15.36s shell runtime.
  - Result: Profile build passed with 0 warnings and 0 errors.
- `tools\validate_tests.bat`
  - Log:
    `Agentic\Reports\validate_tests_buoyancy_system_2026-07-09.log`
  - Runtime: 1.90s shell runtime.
  - Result: project filters 0 errors, Profile test build 0 warnings/errors,
    61/61 doctest cases and 1539/1539 assertions passed.
- `tools\validate_fast.bat`
  - Log:
    `Agentic\Reports\validate_fast_buoyancy_system_2026-07-09.log`
  - Runtime: 53.42s shell runtime.
  - Result: formatting, project filters, staged file sizes, runtime
    boundaries, and Profile/Debug builds all passed.
- `tools\validate_project_filters.bat`
  - Log:
    `Agentic\Reports\validate_project_filters_buoyancy_system_2026-07-09.log`
  - Runtime: 1.13s shell runtime.
  - Result: project filters 0 errors.
- `tools\validate_physics.bat`
  - Log:
    `Agentic\Reports\validate_physics_buoyancy_system_2026-07-09.log`
  - Runtime: 16.07s shell runtime.
  - Result: Debug/Profile builds 0 warnings/errors; final
    `VALIDATE_PHYSICS: ALL PASSED`; `physics_regression_solver.csv` matched
    the committed baseline byte-exact (20001 lines).

No SkullScope trace workflow was used in this slice.

## Comment Audit

Comment audit skill and guide were loaded:

- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Reference/comment-style-guide.md`

Touched source-bearing files audited in this continuation:

- `SkullbonezSource/Physics/BuoyancySystem.cpp`
- `SkullbonezSource/Physics/BuoyancySystem.h`
- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `tools/validate_project_filters.py`

Checked count: 5.
Deferred count: 0.
Unchecked files: none.

## Current Open Work

Plan 02 remains in progress:

- Phase 0 complete.
- Phase 1 complete.
- Phase 2 complete.
- Phase 3 open: table-drive replay snapshot capture/restore.

Open cleanup items that remain in `engine-cleanup-plans/00-EXECUTION-GUIDE.md`:

- Plan 13 FAC-005 remains open on a human-owned public physics API planning
  decision.
- Plan 11 RenderGraph decision remains human-gated.
- Plan 02 Phase 3 remains open for replay snapshot table-driving.
- Plan 04 remains open for error-handling reconciliation.
- Plan 03 remains sign-off gated.
- Plan 07 remains decision gated.

## Resume Checklist

1. Re-run the repo startup contract from `AGENTS.md`.
2. Read `engine-cleanup-plans/00-EXECUTION-GUIDE.md`.
3. Read `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`.
4. Check `git status --short --branch`.
5. Confirm the branch is at or after implementation commit `9a738c0e`.
6. Continue Plan 02 Step 3.1 unless the user gives a different instruction.
7. Do not continue Plan 11 until the human RenderGraph decision is made.
8. Do not continue Plan 13 FAC-005, Plan 03, or Plan 07 without the required
   human decision/sign-off.

## Rubber-Duck Accounting

No rubber-duck pass was run for this incremental slice. The orchestrator skill
calls for a single independent rubber-duck review at the end of a whole cleanup
plan or major checkpoint, not one review per helper extraction.

## Goal Pause Note

Codex goal state has no pause status, only active, complete, or blocked. The
overall goal is still active and incomplete; this document is the manual pause
point for restart or handoff.

## Timing

This finish-slice handoff was drafted at:

- `2026-07-09T12:13:39.5173029+10:00`

Substantial validation sub-runs in this continuation:

- 15.36s for
  `validate_build_profile_buoyancy_system_2026-07-09.log`.
- 53.42s for `validate_fast_buoyancy_system_2026-07-09.log`.
- 1.13s for `validate_project_filters_buoyancy_system_2026-07-09.log`.
- 16.07s for `validate_physics_buoyancy_system_2026-07-09.log`.
- 1.90s for final `validate_tests_buoyancy_system_2026-07-09.log`.
