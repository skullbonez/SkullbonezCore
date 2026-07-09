# Engine Cleanup Restart Handoff - 2026-07-09

## Stop Point

Paused after finishing, validating, committing, and pushing the current Plan 02
solver-decomposition continuation. This is a pause point only; the overall
engine cleanup goal is not complete.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `cfa0905c cleanup(02): extract object CCD bypass decision`

This document is the restart handoff committed after the implementation slices.

## What Landed In This Continuation

Plan 02, Phase 1.2 wake/contact and object/object CCD helper slices:

- `c02adc7b cleanup(02): extract object contact body view helper`
  - Moved `contactBodyViewAtTime` into `ObjectContactBodyViewAtTime`.
  - Preserved candidate-time pose projection for exact contact checks and
    object/object sweep setup.
- `7cf07379 cleanup(02): extract terrain contact body view helper`
  - Moved `terrainContactBodyViewForIndex` into
    `TerrainContactBodyViewForIndex`.
  - Preserved terrain pose, material, threshold, radius, and fixed-body fields.
- `7188e7fb cleanup(02): extract persistent wake contact helper`
  - Moved `hasPersistentWakeContact` into `HasPersistentWakeContact`.
  - Preserved the exact persistent-overlap manifold check that wakes sleepers
    when a swept test missed an already-overlapping corrected awake body.
- `4e041168 cleanup(02): extract object contact time query`
  - Moved `hasObjectContactAtTime` into `HasObjectContactAtTime`.
  - Preserved the non-mutating candidate-time manifold query used by CCD
    refinement.
- `448dcb4f cleanup(02): extract object sweep refinement helper`
  - Moved `refineObjectSweepContactTime` into
    `RefineObjectSweepContactTime`.
  - Preserved the 48-step forward contact-window search and 12-iteration binary
    search.
- `941c66d4 cleanup(02): extract object sweep query helper`
  - Moved `sweepObjectPair` into `SweepObjectPair`.
  - Preserved default collision-time initialization, bounds checks, and the
    `SweepObjectContact` call shape.
- `f333f1c7 cleanup(02): extract persistent cache lookup`
  - Moved `objectPairHasPersistentContactCache` into
    `ObjectPairHasPersistentContactCache`.
  - Named the cache-key `lower_bound` comparator as
    `PersistentContactCacheEntryPrecedesKey`.
- `cfa0905c cleanup(02): extract object CCD bypass decision`
  - Moved `objectPairNeedsSweptCcd` into `ObjectPairNeedsSweptCcd`.
  - Preserved the settled-pair CCD bypass early returns and travel thresholds.

Plan ledger updated:

- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`

The Plan 02 Phase 1.2 lambda inventory is now checked through:

- `bodyIsFixed`
- `bodyPosition`
- `bodyRadius`
- `applyForcesAt`
- `broadphaseCandidateCanTouch`
- `appendCandidatePairIfMissing`
- `isFastSmallSweepBody`
- `sweptSegmentTouchesExpandedBody`
- anonymous fixed/fixed `remove_if` predicate
- anonymous point-joint pair `remove_if` predicate
- anonymous sleep/sleep `remove_if` predicate with trace emission
- `hasWakeEnergy`
- `wakeSleepingModel`
- `contactBodyViewAtTime`
- `terrainContactBodyViewForIndex`
- `hasPersistentWakeContact`
- `hasObjectContactAtTime`
- `refineObjectSweepContactTime`
- `sweepObjectPair`
- `objectPairHasPersistentContactCache`
- anonymous `lower_bound` cache-key comparator
- `objectPairNeedsSweptCcd`

Plan 02 Step 1.2 remains open. The next unchecked item is:

- `recordObjectNarrowphaseEvent`

## Validation Evidence

All source slices ran `git diff --check` before commit and passed with no
output.

Current continuation gates:

- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_contact_body_view_validate_physics_20260709_0958.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_terrain_contact_view_validate_physics_20260709_1001.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_persistent_wake_contact_validate_physics_20260709_1004.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_object_contact_at_time_validate_physics_20260709_1007.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_refine_object_sweep_validate_physics_20260709_1010.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_sweep_object_pair_validate_physics_20260709_1014.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_persistent_cache_lookup_validate_physics_20260709_1021.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.
- `tools\validate_physics.bat`
  - Log:
    `TestOutput\agent_logs\plan02_object_pair_needs_ccd_validate_physics_20260709_1025.log`
  - Result: Debug/Profile builds reported 0 warnings and 0 errors; final
    `VALIDATE_PHYSICS: ALL PASSED`.

No SkullScope trace workflow was used in these slices.

## Comment Audit

Comment audit skill loaded:

- `Agentic/Skills/comment-style-audit/skill.md`
- `Agentic/Reference/comment-style-guide.md`

Touched source-bearing files audited:

- `SkullbonezSource/Physics/PhysicsWorld.cpp`

Checked count: 1.
Deferred count: 0.
Unchecked files: none.

Result:

- `PhysicsWorld.cpp` already has the required learning header.
- The moved exact-contact, refinement, sweep, cache-prefix, and CCD bypass code
  stayed near named helpers without needing unrelated comment churn.
- No unrelated comment-only churn was made.

## Current Open Work

Plan 02 remains in progress:

- Step 1.1 is complete.
- Step 1.2 is open and should continue one lambda/stage at a time.
- Step 1.3 and later closure work remain open.

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
5. Confirm the branch is at or after implementation commit `cfa0905c`.
6. Continue Plan 02 Step 1.2 from the next unchecked item,
   `recordObjectNarrowphaseEvent`, unless the user gives a different
   instruction.
7. Do not continue Plan 11 until the human RenderGraph decision is made.
8. Do not continue Plan 13 FAC-005, Plan 03, or Plan 07 without the required
   human decision/sign-off.

## Rubber-Duck Accounting

No rubber-duck pass was run for these incremental slices. The orchestrator skill
calls for rubber-duck review at a major plan/checkpoint or repeated-failure
loop, not per helper extraction.

## Goal Pause Note

Codex goal state has no real pause status, only active/complete/blocked. The
overall goal is still active and incomplete; this document is the manual pause
point for restart or handoff.

## Timing

Current continuation began at:

- `2026-07-09T10:19:19.7450905+10:00`

This handoff was drafted at:

- `2026-07-09T10:26:05.5077434+10:00`
