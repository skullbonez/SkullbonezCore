# Engine Cleanup Restart Handoff - 2026-07-09

## Stop Point

Paused after finishing, validating, committing, and pushing Plan 02 Step 1.4.
This is a restart pause point only; the overall engine cleanup goal remains
active and incomplete.

Branch:

- `nightrunner-8th-july`

Latest pushed implementation commit before this handoff:

- `7a4ab2bd cleanup(02): test solver broadphase predicate`

The next unchecked Plan 02 item is:

- `2.1` Move tornado capture/eject arrays and methods out of `PhysicsWorld`
  into a `TornadoGameplay` system. Gate: `tools\validate_physics.bat`.

## What Landed Since The Prior Handoff

The older restart handoff stopped after `05c74196`. This continuation completed
the remaining Plan 02 Phase 1 solver-driver cleanup:

- `2dd38817 cleanup(02): extract terrain detection stage`
  - Added `TerrainDetectionStageContext`, `TerrainDetectionStage`, and
    `PhysicsWorld::DetectTerrainAt`.
  - Removed the `detectTerrainAt` lambda while preserving fixed, sleeping,
    time, and terrain-query guards.
- `e31b8bee cleanup(02): extract terrain candidate commit stage`
  - Added `TerrainCandidateCommitContext` and
    `PhysicsWorld::CommitTerrainCandidate`.
  - Removed the `commitTerrainCandidate` lambda while preserving terrain
    manifold construction, diagnostics, sleep support, visual contact marking,
    and remaining-time writeback.
- `827f86b0 cleanup(02): extract remaining integration stage`
  - Added `IntegrateRemainingSolverBody` and
    `IntegrateRemainingStageContext`.
  - Removed the `integrateRemainingAt` lambda and recorded
    `LAMBDA_MATCH_COUNT=0` for `RunSolverPhysics`.
- `3b3f3620 cleanup(02): shrink solver driver stages`
  - Added `PhysicsWorld::BuildSolverBroadphaseCandidatePairs`.
  - Added `PhysicsWorld::RunSleepIslandStage`.
  - Reduced `RunSolverPhysics` to a 253-line driver with no lambdas.
- `7a4ab2bd cleanup(02): test solver broadphase predicate`
  - Moved the pure broadphase candidate filter into
    `SkullbonezSource/Physics/SolverBroadphaseStage.h`.
  - Kept `PhysicsWorld::BuildSolverBroadphaseCandidatePairs` calling that
    predicate through the spatial-grid callback path.
  - Added `SkullbonezTests/TestSolverBroadphaseStage.cpp` with direct coverage
    for static overlap, static separation, swept approach, null-filter pass
    through, out-of-range rejection, and invalid-radius conservative acceptance.

Plan ledger updated:

- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`

Plan 02 Phase 1 is now checked complete. Phase 2 remains open.

## Validation Evidence

Current continuation gates:

- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_detect_terrain_at_validate_physics_20260709_1106.log`
  - Runtime: 39.1s shell runtime.
  - Result: Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_commit_terrain_candidate_validate_physics_20260709_1109.log`
  - Runtime: 39.0s shell runtime.
  - Result: Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_integrate_remaining_at_validate_physics_20260709_1114.log`
  - Runtime: 27.6s shell runtime.
  - Result: Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_run_solver_driver_shrink_validate_physics_20260709_1118.log`
  - Runtime: 39.2s shell runtime.
  - Result: Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_tests.bat`
  - First attempt log:
    `TestOutput\agent_logs\plan02_solver_broadphase_stage_validate_tests_20260709_1130.log`
  - Result: build succeeded with 0 warnings and 0 errors, then the new test
    crashed from stack-allocating fixed-capacity physics lists. The fixture was
    corrected to static storage.
- `tools\validate_tests.bat`
  - Passing log:
    `TestOutput\agent_logs\plan02_solver_broadphase_stage_validate_tests_20260709_1131.log`
  - Runtime: 4.7s shell runtime.
  - Result: Profile build 0 warnings and 0 errors; 61/61 test cases and
    1539/1539 assertions passed; final `VALIDATE_TESTS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_solver_broadphase_stage_validate_physics_20260709_1132.log`
  - Runtime: 25.9s shell runtime.
  - Result: Debug/Profile builds 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.

No SkullScope trace workflow was used in these slices.

## Comment Audit

Comment audit skill and guide were loaded:

- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Reference/comment-style-guide.md`

Touched source-bearing files audited in this continuation:

- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `SkullbonezSource/Physics/SolverBroadphaseStage.h`
- `SkullbonezTests/TestSolverBroadphaseStage.cpp`

Checked count: 4.
Deferred count: 0.
Unchecked files: none.

For Step 1.4 specifically, checked count was 3:

- `PhysicsWorld.cpp`
- `SolverBroadphaseStage.h`
- `TestSolverBroadphaseStage.cpp`

## Current Open Work

Plan 02 remains in progress:

- Phase 0 complete.
- Phase 1 complete.
- Phase 2 open: evict tornado gameplay and analytic buoyancy from
  `PhysicsWorld`.
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
5. Confirm the branch is at or after implementation commit `7a4ab2bd`.
6. Continue Plan 02 Step 2.1 unless the user gives a different instruction.
7. Do not continue Plan 11 until the human RenderGraph decision is made.
8. Do not continue Plan 13 FAC-005, Plan 03, or Plan 07 without the required
   human decision/sign-off.

## Rubber-Duck Accounting

No rubber-duck pass was run for these incremental slices. The orchestrator skill
calls for a single independent rubber-duck review at the end of a whole cleanup
plan or major checkpoint, not one review per helper extraction.

## Goal Pause Note

Codex goal state has no pause status, only active, complete, or blocked. The
overall goal is still active and incomplete; this document is the manual pause
point for restart or handoff.

## Timing

Current continuation began at:

- `2026-07-09T11:05:39+10:00`

This handoff was drafted at:

- `2026-07-09T11:34:11+10:00`

Substantial validation sub-runs in this continuation:

- 39.1s for `plan02_detect_terrain_at_validate_physics_20260709_1106.log`
- 39.0s for `plan02_commit_terrain_candidate_validate_physics_20260709_1109.log`
- 27.6s for `plan02_integrate_remaining_at_validate_physics_20260709_1114.log`
- 39.2s for `plan02_run_solver_driver_shrink_validate_physics_20260709_1118.log`
- 6.0s for the failed
  `plan02_solver_broadphase_stage_validate_tests_20260709_1130.log`
- 4.7s for `plan02_solver_broadphase_stage_validate_tests_20260709_1131.log`
- 25.9s for `plan02_solver_broadphase_stage_validate_physics_20260709_1132.log`
