# Replay Policy Debt Closure

Status: Registered — 0/4 tasks (RP0-RP3)
Owner: repository owner; registered 2026-07-21 from replay-boundary RB1 audit
Evidence: `../../Reports/2026-07-21/replay-boundary-containment-closure.md`
RB1 findings RB1-F1 through RB1-F3
Ledger: RP0-RP3
Depends on: `replay-boundary-containment` RB2 closure

## Objective

Close the three findings exposed by the replay boundary audit without
shrinking Replay or changing replay behavior: every live prediction/
presentation allocation must be attributed to an existing registered replay
owner, and `PhysicsSceneObjectId` must become Replay's durable cross-system
identity while serialized artifact scalars remain byte-compatible.

## Findings And Binding Decisions

1. A strict two-generation prediction/presentation probe recorded 41,606
   gameplay allocation violations and 41,603 reserve-policy violations, led by
   unregistered Replay, render, and steady-gameplay call sites. The ordinary
   performance gate is clean because it does not exercise this path. This is a
   real coverage and policy defect, not authority to weaken the guard.
2. `ReplayBodyId` and raw `replayBodyId` values remain in Replay, Physics, and
   Rendering even though the standing Scene Object Identity Policy names
   `PhysicsSceneObjectId` as the single durable cross-system identity. The
   legacy scalar is derived from `PhysicsSceneObjectId`; converge the C++ type
   boundary while preserving the durable artifact's existing integer bytes and
   schema version.
3. Replay already has exactly three registered byte-budget owners. Use those
   owners or preallocated/fixed storage; do not register a broad fourth owner,
   wrap an entire frame in a permissive owner scope, or relabel unrelated
   allocations. Owner scopes must be as narrow as the allocation they approve.
4. No replay, physics, screenshot, or behavioral baseline refresh is
   authorized. Divergence is a failed change.

## Tasks

- [ ] RP0 — Attribution contract: reproduce the strict two-generation probe,
  symbolize or otherwise map every material owner-zero callsite, classify it
  as Replay prediction/trajectory, Runtime presentation/render, or unrelated
  steady work, and record the exact owner/preallocation remedy. Add a focused
  repeatable probe if the current interaction command is not deterministic.
  Investigation only; no behavior change. Validation: targeted probe.
- [ ] RP1 — Allocation closure: route every live Replay-owned growth through
  one of the three existing registered owners or prepare fixed capacity before
  steady use; eliminate owner-zero render/steady allocations caused by replay
  presentation without blanket scopes. Correct the recorder/solver policy
  comments and `REPLAY_GROWTH_OWNER_POLICIES` evidence against the strict-run
  allocator high-water/counters. Fix the two ReplayRecorder `.cpp`/`.h`
  allowlist rows to name `replay_recorder_samples`, its aggregate 32 MiB cap,
  and recorder-owned storage. Extend focused coverage so the strict probe fails
  on any gameplay or reserve-policy violation. Validation:
  `tools\validate_fast.bat`,
  `python tools\check_allocation_policy.py --self-test`,
  `python tools\check_allocation_policy.py --repo .`,
  `tools\validate_perf.bat`, the focused strict probe,
  `tools\validate_full.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.
- [ ] RP2 — Identity convergence: replace Replay-owned `ReplayBodyId` and raw
  cross-system `replayBodyId` surfaces with `PhysicsSceneObjectId`, including
  Physics authored refresh/body/collider APIs and storage plus Rendering
  instance/override values; retain dense model rows only as repairable hints.
  Delete definition-only `PhysicsReplaySolverSnapshotView` and
  `SceneWorld::TryQueueReplayRenderPoseOverride` unless a concrete non-Replay
  consumer is proven. Keep artifact encoding/decoding as the existing
  fixed-width scalar at the cold IO boundary and prove existing artifacts
  load/save byte-identically. Validation: focused identity/restore tests,
  `tools\validate_physics.bat`, `tools\validate_full.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.
- [ ] RP3 — Closure: rerun the strict allocation probe, inbound Replay include
  proof, identity census, allocation inventory, comment audit, and one
  independent rubber-duck review. Final gates are cumulative:
  `tools\validate_full.bat`, `tools\validate_physics.bat`,
  `tools\validate_perf.bat`, and
  `tools\validate_replay_visual_fidelity.bat`.

## Acceptance

- The strict two-generation path reports zero gameplay allocation violations
  and zero reserve-policy violations without suppressing guard coverage.
- Exactly three replay reserve owners remain, with unchanged or explicitly
  evidence-approved caps and complete counters.
- Replay, Physics, Rendering, and Runtime scene/presentation boundaries use
  `PhysicsSceneObjectId`; the two definition-only facades are gone unless a
  concrete retained consumer is documented; durable artifact bytes and schema
  are unchanged.
- All mapped gates pass without baseline refresh and independent review is
  clear.

## Validation Summary

RP0-RP3 pending.
