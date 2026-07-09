# Engine Cleanup Handoff - Plan 02 Tornado Gameplay Slice

## Stop Point

Paused after finishing, validating, committing, and preparing handoff for Plan
02 Step 2.1. This is a restart pause point only; the overall engine cleanup
goal remains active and incomplete.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `c7ef56ee cleanup(02): move tornado gameplay out of physics world`

The next unchecked Plan 02 item is:

- `2.2` Move analytic buoyancy (`RefreshUnderwaterSubmersionForBall`) into a
  buoyancy system. Gate: `tools\validate_physics.bat`.

## What Landed

This continuation completed Plan 02 Step 2.1:

- Added `SkullbonezSource/Physics/TornadoGameplay.h/.cpp`.
- Moved tornado field/system config, capture timers, eject cooldowns,
  fixed-tree release scratch, debug vector rendering, replay timer state, and
  tornado memory accounting out of `PhysicsWorld`.
- Replaced the old direct `PhysicsWorld` tornado vectors and methods with one
  `TornadoGameplay` member and a narrow `ApplyTornadoGameplay` call before
  broadphase.
- Kept wake propagation in `PhysicsWorld` through the existing `WakeModel`
  path after `TornadoGameplay` emits deterministic body-index wake output.
- Wired `TornadoGameplay.cpp` into `SKULLBONEZ_CORE` and `SKULLBONEZ_TESTS`.
- Fixed project metadata discovered by the required gates:
  `SolverBroadphaseStage.h` is now listed in the production project/filter, and
  `tools/validate_project_filters.py` recognizes both `SolverBroadphaseStage`
  and `TornadoGameplay` as physics source prefixes.

Plan ledger updated:

- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`

## Validation Evidence

Current continuation gates:

- `tools\validate_build.bat Profile`
  - First attempt failed on an `EngineConfig` forward-declaration mismatch in
    `TornadoGameplay.h`; fixed by matching the existing `class EngineConfig`.
  - Passing log:
    `Agentic\Reports\validate_build_profile_tornado_gameplay_2026-07-09-rerun.log`
  - Runtime: 14.90s shell runtime.
  - Result: 0 warnings, 0 errors.
- `tools\validate_tests.bat`
  - First attempt failed because the project-filter validator did not yet know
    the new `TornadoGameplay` physics prefix; fixed in
    `tools/validate_project_filters.py`.
  - Final passing log:
    `Agentic\Reports\validate_tests_tornado_gameplay_2026-07-09-final.log`
  - Runtime: 1.92s shell runtime.
  - Result: project filters 0 errors, Profile test build 0 warnings/errors,
    61/61 doctest cases and 1539/1539 assertions passed.
- `tools\validate_fast.bat`
  - Earlier attempts exposed a previously committed
    `SolverBroadphaseStage.h` header-format/project-metadata gap; fixed by
    applying the targeted header format pipeline and adding it to the
    production project/filter.
  - Passing log:
    `Agentic\Reports\validate_fast_tornado_gameplay_2026-07-09-rerun4.log`
  - Runtime: 57.34s shell runtime.
  - Result: formatting, project filters, staged file sizes, runtime
    boundaries, and Profile/Debug builds all passed.
- `tools\validate_project_filters.bat`
  - Log:
    `Agentic\Reports\validate_project_filters_tornado_gameplay_2026-07-09.log`
  - Runtime: 1.12s shell runtime.
  - Result: project filters 0 errors.
- `tools\validate_physics.bat`
  - Log:
    `Agentic\Reports\validate_physics_tornado_gameplay_2026-07-09.log`
  - Runtime: 14.37s shell runtime.
  - Result: Debug/Profile builds 0 warnings/errors; final
    `VALIDATE_PHYSICS: ALL PASSED`; `physics_regression_solver.csv` matched
    the committed baseline byte-exact (20001 lines).

No SkullScope trace workflow was used in this slice.

## Comment Audit

Comment audit skill and guide were loaded:

- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Reference/comment-style-guide.md`

Touched source-bearing files audited in this continuation:

- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/TornadoGameplay.cpp`
- `SkullbonezSource/Physics/TornadoGameplay.h`
- `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- `tools/validate_project_filters.py`

Checked count: 6.
Deferred count: 0.
Unchecked files: none.

## Current Open Work

Plan 02 remains in progress:

- Phase 0 complete.
- Phase 1 complete.
- Phase 2 partially complete: tornado gameplay extraction done; analytic
  buoyancy extraction remains.
- Phase 3 open: table-drive replay snapshot capture/restore.

Open cleanup items that remain in `engine-cleanup-plans/00-EXECUTION-GUIDE.md`:

- Plan 13 FAC-005 remains open on a human-owned public physics API planning
  decision.
- Plan 11 RenderGraph decision remains human-gated.
- Plan 02 rest remains open for solver decomposition work.
- Plan 04 remains open for error-handling reconciliation.
- Plan 03 remains sign-off gated.
- Plan 07 remains decision gated.

## Resume Checklist

1. Re-run the repo startup contract from `AGENTS.md`.
2. Read `engine-cleanup-plans/00-EXECUTION-GUIDE.md`.
3. Read `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`.
4. Check `git status --short --branch`.
5. Confirm the branch is at or after implementation commit `c7ef56ee`.
6. Continue Plan 02 Step 2.2 unless the user gives a different instruction.
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

Current continuation began at:

- `2026-07-09T11:37:07.4828805+10:00`

This handoff was drafted at:

- `2026-07-09T12:00:07.0878996+10:00`

Substantial validation sub-runs in this continuation:

- 15.38s failed focused Profile build before the `EngineConfig` declaration
  fix.
- 14.90s for passing
  `validate_build_profile_tornado_gameplay_2026-07-09-rerun.log`.
- 0.24s failed `validate_tests` project-filter prefix attempt.
- 2.91s for passing intermediate `validate_tests` rerun.
- 5.21s failed `validate_fast` formatting attempt.
- 9.31s failed `validate_fast` header-format attempt.
- 10.40s failed `validate_fast` missing production project metadata attempt.
- 10.42s failed `validate_fast` prefix allowlist attempt.
- 57.34s for passing
  `validate_fast_tornado_gameplay_2026-07-09-rerun4.log`.
- 1.12s for
  `validate_project_filters_tornado_gameplay_2026-07-09.log`.
- 14.37s for `validate_physics_tornado_gameplay_2026-07-09.log`.
- 1.92s for final `validate_tests_tornado_gameplay_2026-07-09-final.log`.
