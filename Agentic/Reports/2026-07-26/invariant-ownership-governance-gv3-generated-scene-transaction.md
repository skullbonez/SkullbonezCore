# Invariant Ownership Governance GV3 Generated-Scene Transaction

Date: 2026-07-26

Plan phase: GV3 — Repair invariant-shaped rows outside the concrete bag census

Impact: Runtime/Scene ownership, Runtime/App input commands,
Runtime/Capture graphics stress, and focused owner-request tests

## Result

GV3 is complete. The sole additional invariant-shaped repair row from the GV1
census now has one concrete owner:
`SceneGeneratedControlTransaction`.

The legal phase walk is:

`Idle -> DrainAndReset -> Repopulate -> PublishFollowUps -> Complete`

The transaction is non-copyable and stack-scoped. It stores only request,
policy, resolved-count, result, and cursor values. `EngineConfig`,
`SceneController`, UI overrides, camera, simulation, tools, and the optional
DX12 frame owner are synchronous `Execute` borrows and are never retained.
`AdvanceOrFatal` makes every out-of-order phase transition lane F fatal.

## Arbitration and failure semantics

- Model-count commands clear both exact solver overrides.
- Exact solver commands clear the model-count override.
- A partial ball or box command reads the newest accepted sibling override
  before falling back to `SceneSessionState`, preserving same-frame
  ball-before-box arbitration.
- Exact stress requests preserve balls first and trim boxes second when the
  combined count exceeds capacity.
- Invalid negative UI sentinels remain rejected without entering the phase
  machine.
- An absent current scene updates only the accepted override values and
  publishes no replay/profiler follow-up.
- A failed GPU drain returns lane R before UI overrides, topology, simulation,
  or tool state mutate.
- Setup failures retain the established partial-topology behavior: report the
  setup owner, publish the replay/profiler follow-ups, and clamp camera tracking
  to the surviving entity count.

The focused cursor test enumerates all 36 transitions among `Idle`,
`DrainAndReset`, `Repopulate`, `PublishFollowUps`, `Complete`, and the invalid
`Count` sentinel. It also proves that rejected jumps leave the cursor at its
current phase so the next legal transition remains available.

## Deletion and call-site proof

The following authority-free participant bags and free operations are deleted:

- `SceneGeneratedControlPolicy`
- `SceneGeneratedControlPresentation`
- `SceneGeneratedControlResetParticipants`
- `ApplyUIModelCountOverride`
- `ApplyUISolverObjectCounts`
- `ApplySceneGeneratedModelCountUICommand`
- `ApplySceneGeneratedSolverBallCountUICommand`
- `ApplySceneGeneratedSolverBoxCountUICommand`

This command returns zero rows across source and tests:

```powershell
rg -n "SceneGeneratedControlPolicy|SceneGeneratedControlPresentation|SceneGeneratedControlResetParticipants|ApplyUIModelCountOverride|ApplyUISolverObjectCounts|ApplySceneGenerated(ModelCount|SolverBallCount|SolverBoxCount)UICommand" SkullbonezSource SkullbonezTests
```

`InputFrame.cpp` now creates one concrete transaction per model, solver-ball,
or solver-box request and executes it before recording the accepted command.
`RuntimeStressController.cpp` uses the same owner for model-count and exact
solver-count churn. Neither call site reconstructs the phase order.

## Comment audit

Touched-file audit: 5 checked, 0 deferred, 0 unchecked. A subsystem checklist
was not required because this was a touched-file pass.

Audited files:

- `SkullbonezSource/Runtime/App/InputFrame.cpp`
- `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
- `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp`
- `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`

The transaction header names the exact phase-order invariant, synchronous
owner-borrow lifetime, GPU-drain hazard, lane-F policy, and focused test.
The implementation documents same-frame count arbitration, fail-before-mutate
drain behavior, setup-failure consequences, and detached follow-up ownership.
No stale helper, participant, or prior-owner claim remains in the touched
files.

## Validation

- `tools\validate_tests.bat`: PASS; 395/395 cases and
  2,403,407/2,403,407 assertions.
- `tools\validate_full.bat`: PASS; formatting, 783/783 production
  project/filter items, dependency graph, mandatory CPU/coverage umbrella,
  Automation/replay smoke, DX12 renderer validation, and byte-exact
  44,401-line physics regression.
- `tools\run_graphics_stress.bat 1`: PASS; the bounded DX12 stress process ran
  for one minute and was stopped by the expected PID timeout.
- Retired-symbol scan: empty.
- Tracked baseline/config/scene/golden/replay-artifact/physics-CSV diff: empty.

The first broad-gate attempt stopped in formatting preflight on two paragraph
breaks in the new implementation. `tools\format_fix.bat` repaired the GV3
diff; Git status confirmed no unrelated source file changed. The authoritative
full-gate retry passed without a source, baseline, golden, artifact, scene,
config, or physics CSV refresh.

The plan-level independent hostile review remains GV4's required whole-plan
closure review.
