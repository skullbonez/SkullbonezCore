# Engine Cleanup Restart Handoff - 2026-07-09

## Stop Point

Paused by user request after finishing, validating, committing, and pushing the
current Plan 02 slice. This is a pause point only; the overall engine cleanup
goal is not complete.

Do not start another cleanup slice until the user restarts the machine and
explicitly resumes.

Branch:

- `nightrunner-8th-july`

Latest implementation commit before this handoff:

- `98e06366 cleanup(02): extract sleeping wake helper`

This document is the restart handoff committed after the implementation slice.

## What Just Landed

Plan 02, Phase 1.2 wake-sleep slice:

- Moved the local `wakeSleepingModel` lambda out of
  `PhysicsWorld::RunSolverPhysics`.
- Added the named anonymous-namespace helper `WakeSleepingSolverBody`.
- Threaded explicit solver state into the helper:
  `PhysicsBodyStore`, `ColliderStore`, `PhysicsWorldForces`, body records,
  sleep state, sleep counters, sleep island visual IDs, remaining time,
  underwater sleep locks, model count, body index, and `dt`.
- Preserved the original immediate force application after waking a body so a
  body re-entering the current frame receives the same force treatment as an
  already-awake body.
- Deliberately did not reuse `WakeDynamicBodyState`, because that existing path
  also clears underwater sleep locks and persistent contact cache state. The
  extracted solver helper preserves the narrower behavior of the original
  lambda.

Plan ledger updated:

- `engine-cleanup-plans/02-physicsworld-solver-decomposition.md`

The Plan 02 lambda inventory now has these Plan 1.2 items checked:

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

Plan 02 Step 1.2 remains open. The next unchecked wake/contact item is:

- `contactBodyViewAtTime`

## Validation Evidence

Current slice:

- `git diff --check`
- Result: passed with no output.

- `tools\validate_physics.bat`
- Log:
  `TestOutput\agent_logs\plan02_wake_sleeping_model_validate_physics_20260709_0951.log`
- Result: Debug/Profile builds reported 0 warnings and 0 errors, and the script
  ended with `VALIDATE_PHYSICS: ALL PASSED`.
- Logged build timings:
  - Debug build: `00:00:06.04`
  - Profile build: `00:00:08.77`
  - final Debug build/smoke step: `00:00:01.38`

Recent Plan 02 validation from this same restart session:

- Candidate pair append extraction:
  `TestOutput\agent_logs\plan02_append_candidate_pair_validate_physics_20260709_0934.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Fast-small-sweep classifier extraction:
  `TestOutput\agent_logs\plan02_fast_small_sweep_validate_physics_20260709_0938.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Swept segment/body test extraction:
  `TestOutput\agent_logs\plan02_swept_segment_validate_physics_20260709_0940.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Fixed/fixed prune predicate extraction:
  `TestOutput\agent_logs\plan02_fixed_pair_predicate_validate_physics_20260709_0942.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Point-joint prune predicate extraction:
  `TestOutput\agent_logs\plan02_point_joint_predicate_validate_physics_20260709_0944.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Sleep/sleep prune predicate extraction:
  `TestOutput\agent_logs\plan02_sleep_prune_predicate_validate_physics_20260709_0946.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.
- Wake-energy helper extraction:
  `TestOutput\agent_logs\plan02_wake_energy_validate_physics_attempt2_20260709_0949.log`
  passed with `VALIDATE_PHYSICS: ALL PASSED`.

No SkullScope trace workflow was used in this slice.

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
- `WakeSleepingSolverBody` carries the local wake/force-application comment that
  explains why waking applies forces in the same frame.
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
5. Confirm the branch is at or after implementation commit `98e06366`.
6. Continue Plan 02 Step 1.2 from the next unchecked item,
   `contactBodyViewAtTime`, unless the user gives a different instruction.
7. Do not continue Plan 11 until the human RenderGraph decision is made.
8. Do not continue Plan 13 FAC-005, Plan 03, or Plan 07 without the required
   human decision/sign-off.

## Goal Pause Note

Codex goal state has no real pause status, only active/complete/blocked. The
overall goal is still active and incomplete; this document is the manual pause
point for the computer restart.

## Timing

Current restart-session continuation began at:

- `2026-07-09T09:32:36.3405991+10:00`

This handoff was drafted at:

- `2026-07-09T09:52:28.5220228+10:00`
