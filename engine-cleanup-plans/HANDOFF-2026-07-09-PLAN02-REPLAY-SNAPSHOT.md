# Handoff - 2026-07-09 - Plan 02 Replay Snapshot Table

## Status

Plan 02, Phase 3 Step 3.1 is complete. The replay solver snapshot capture and
restore paths are now table-driven, and the remaining oversized
`PhysicsWorld::RunSleepIslandStage` tail was split into
`PhysicsWorld::ApplySleepIslandTransitions`.

The user requested a pause after this slice for a computer restart. Do not
continue automatically past this checkpoint unless the user resumes the goal.

## Branch / Worktree

- Branch: `nightrunner-8th-july`
- Repo: `C:\SkullbonezCore`
- Planned commit subject: `cleanup(02): table-drive replay solver snapshot`

## Files Changed

- `SkullbonezSource/Physics/PhysicsWorld.cpp`
- `SkullbonezSource/Physics/PhysicsWorld.h`
- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`
- `engine-cleanup-plans/00-EXECUTION-GUIDE.md`
- `Agentic/SessionState.md`
- `engine-cleanup-plans/HANDOFF-2026-07-09-PLAN02-REPLAY-SNAPSHOT.md`

## Implementation Notes

- Added X-macro field lists for replay solver direct vectors, tornado vectors,
  converted contact/cache vectors, persistent contact row fields, cache row
  fields, and solver stats.
- `CaptureReplaySolverSnapshot` uses the shared inventory for clear, reserve,
  and vector-copy work.
- `RestoreReplaySolverSnapshot` uses the same direct-vector list and row-field
  lists; tornado replay state remains owned by `TornadoGameplay::SetReplayState`.
- Split sleep transition application into `ApplySleepIslandTransitions` without
  changing counter, visual-id, diagnostics, velocity-zeroing, or underwater-lock
  order.
- Comment audit checked `PhysicsWorld.cpp` and `PhysicsWorld.h`; checked 2,
  deferred 0, unchecked none.

## Structural Evidence

Scoped `PhysicsWorld.cpp` scanner result:

- `PhysicsWorld::ProcessObjectNarrowphasePair`: 292 lines
- `PhysicsWorld::RunSolverPhysics`: 271 lines
- `PhysicsWorld::RunSleepIslandStage`: 257 lines
- `PhysicsWorld::ApplySleepIslandTransitions`: 118 lines
- No `PhysicsWorld` function exceeded 300 lines.

Residual note: a broader `SkullbonezSource/Physics` scan still reports older
non-Plan-02 functions above 300 lines:
`PersistentContactSolver::Solve`, `RunPhysicsStandaloneSmoke`, and
`ConvexHullShape::LoadFromFile`. They were not touched by this slice and should
only be handled by a separate plan.

## Validation

All final gates passed after the last source edit:

- `tools\validate_build.bat Profile`
  - Log:
    `Agentic\Reports\validate_build_profile_replay_snapshot_table_final_2026-07-09.log`
  - Runtime: 14.70s
  - Result: `PASS: Build Profile|x64 succeeded.`
  - 0 warnings, 0 errors
- `tools\validate_physics.bat`
  - Log:
    `Agentic\Reports\validate_physics_replay_snapshot_table_final_2026-07-09.log`
  - Runtime: 26.69s
  - Result: `VALIDATE_PHYSICS: ALL PASSED`
  - Debug/Profile builds: 0 warnings, 0 errors
  - Physics baseline: byte-exact
- `tools\validate_replay_scrub.bat`
  - Log:
    `Agentic\Reports\validate_replay_scrub_replay_snapshot_table_final_2026-07-09.log`
  - Runtime: 11.03s
  - Result: `VALIDATE_REPLAY_SCRUB: ALL PASSED`
- `tools\validate_physics_deep.bat`
  - Log:
    `Agentic\Reports\validate_physics_deep_replay_snapshot_table_final_2026-07-09.log`
  - Runtime: 84.09s
  - Result: `VALIDATE_PHYSICS_DEEP: ALL PASSED`

## SkullScope Accounting

Replay scrub trace command:

```bat
C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-scrub-test --physics-diag C:\SkullbonezCore\Debug\replay_scrub.physicsdiag.ndjson
```

Replay scrub query:

```bat
tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8
```

Replay restore trace command:

```bat
C:\SkullbonezCore\Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --shadows off --scene SkullbonezData/scenes/physics_roll.scene.json --frames 120 --replay on --replay-seconds 1 --replay-restore-test --physics-diag C:\SkullbonezCore\Debug\replay_restore.physicsdiag.ndjson
```

Replay restore query:

```bat
tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8
```

Artifact sizes:

- Scrub trace NDJSON: 54,932 bytes
- Scrub SQLite cache: 225,280 bytes
- Scrub query output read by GPT: 1,512 bytes
- Restore trace NDJSON: 54,912 bytes
- Restore SQLite cache: 225,280 bytes
- Restore query output read by GPT: 967 bytes
- Total GPT-read query output: 2,479 bytes

## Review

Rubber-duck review was performed as an in-session read-only critique because the
available multi-agent tool requires explicit user permission for delegation. No
blocking issues were found. Residual risk is limited to the documented scope
wording around older non-Plan-02 long physics functions.

## Next Resume Point

Resume from `engine-cleanup-plans/00-EXECUTION-GUIDE.md` item 10:
Plan 04, error-handling policy reconciliation.

Before editing on resume:

1. Read the startup contract files from `AGENTS.md`.
2. Run `git status --short --branch`.
3. Read `Agentic/Skills/orchestrator/SKILL.md`.
4. Open `engine-cleanup-plans/04-error-handling-policy-reconciliation.md` and
   start its first unchecked step.
