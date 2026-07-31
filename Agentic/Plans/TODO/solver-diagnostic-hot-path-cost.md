# Solver Diagnostic Hot-Path Cost

Date: 2026-07-31
Status: IN PROGRESS — 3/4 phases complete
Impact area: Physics contact solver, step diagnostics, replay sample identity
Owner: Physics
Priority: High

## Problem And Evidence

`PersistentContactSolveTransaction::SolveRowsIterations` gates its convergence
diagnostics correctly. The same loop does not gate the pipeline trace.

At `SkullbonezSource/Physics/PersistentContactSolver.cpp:1305`, inside
`for (iter) { for (contact) { ... } }`:

```cpp
if ( canRecordPipeline() )
{
    Physics::PhysicsPipelineRecord record;
    ...
    record.point = PhysicsBodyPosition( hotRead, static_cast<size_t>( c.bodyA ) ) + c.rA;
    record.scalarC = sqrtf( c.accT1 * c.accT1 + c.accT2 * c.accT2 );
    recordPipeline( record );
}
```

`PhysicsStepDiagnostics::RemainingPipelineRecordCapacity()`
(`Stages/PhysicsStepDiagnostics.cpp:118`) is not build-gated. It returns
`PHYSICS_MAX_PIPELINE_TRACE_RECORDS - size()` in every configuration, and the
trace is cleared each step. Every fixed step in Debug, Profile, and Release
therefore pays, per contact row per solver iteration: one capacity compare, one
`PhysicsBodyPosition` load across three SoA streams, one `sqrtf`, and one
44-byte record append — up to the 4096-record ceiling. Six further producers
push to the same trace (`PersistentContactSolver.cpp` lines 349, 759, 877, 1192,
1635, 1733, 1846; `Stages/PhysicsSleepController.cpp:75`).

What consumes it outside Debug overlays: `ReplayRecorder.cpp:2017` reads
`ReadPipelineTrace( physics ).size()` and stores a single `uint16_t`
`pipelineRecordCount`. Nothing in the shipping path reads a record's contents.

**The contrast is the finding.** Two diagnostics live in the same innermost
loop. One is eliminated at compile time through the
`CollectConvergenceDiagnostics` template parameter, with a comment explaining
that private prediction worlds must not spend simulation budget on observational
work. The other runs unconditionally in the shipping binary to produce a number.

## Goal

Delete the pipeline-trace payload cost from every configuration where no
consumer is live, while `pipelineRecordCount` stays byte-identical everywhere.

## Non-Goals

- No replay golden refresh. `pipelineRecordCount` is hashed into every replay
  sample (`ReplayRecorder.cpp:1576`, `:1815`, `:2033`) and replay fidelity
  validates from the Profile test binary. A moved replay hash means the task is
  wrong, not that the manifest needs updating. Inventory rule 11 applies.
- No physics behavior change. The trace is write-only observational state; a
  physics CSV byte difference means production arithmetic was disturbed.
- No removal of the overlay or SkullScope capability. When a consumer is live,
  it receives exactly the records it receives today.

## Owner Ruling

Counting and recording are separated. The engine always counts what the full
trace would have pushed, with identical saturation at
`PHYSICS_MAX_PIPELINE_TRACE_RECORDS`, so replay sample identity is unchanged in
every configuration. Record payloads are constructed only when a consumer is
actually active — the same shape as the existing `collectConvergenceDiagnostics`
gate, not a new configuration macro layered beside it.

Removing the trace from Release alone is explicitly rejected: it would leave the
cost in Profile, which is where `validate_perf` measures, and would still move
Profile replay hashes if later extended.

## Phases

- [x] **HP0 — Census producers, consumers, and per-configuration reachability.**
  Enumerate every pipeline-trace producer and every `ReadPipelineTrace` /
  `GetPhysicsPipelineTrace` consumer
  (`Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp:186`,
  `Runtime/Render/RenderModelFramePublisher.cpp:53`,
  `Runtime/Replay/ReplayRecorder.cpp:2017`). For each consumer, state in which
  configurations and under which runtime toggles it is live. Resolve whether
  `PhysicsSolverSnapshot::pipelineTrace`
  (`Physics/PhysicsSolverSnapshot.h:136`) and its restore path
  (`Stages/PhysicsStepDiagnostics.cpp:254`) compare record contents or only
  counts. Measure current cost on `perf_1000`: records produced per fixed step,
  saturation frequency, `sqrtf` count, and inclusive solver time. Confirm the
  `pipelineRecordCount` → sample-hash edge and that Profile is the
  replay-hash-producing configuration. Evidence:
  `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp0-census.md`.
  Complete 2026-07-31: all 16 stage producers are configuration-independent;
  full records are required by solver Replay snapshots/hash/artifacts,
  prediction, SkullScope, and the pipeline overlay, while presentation Replay
  needs only the saturated count. The current record is 56 bytes (correcting
  the provisional 44-byte statement). A bounded two-run `perf_1000` SkullScope
  witness retained 296,714 records per 180-frame run, averaged 1,648.411111
  records per step, hit zero 4,096-row saturations, and executed 866
  pipeline-only `sqrtf` calls. The complete two-pass Profile scene measured
  `SolveRows` at 0.120002 ms mean and the inclusive persistent-contact scope at
  0.272922 ms mean.

- [x] **HP1 — Separate counting from recording with exact count preservation.**
  Introduce one trace recorder owner that either retains full records or counts
  only, with byte-identical saturation behavior in both modes across every
  producer found in HP0. `pipelineRecordCount` must be unchanged in Debug,
  Profile, and Release for identical input. Add focused coverage that pins count
  equality between the two modes, including at and beyond the 4096 ceiling, and
  that an active consumer still receives complete records. Evidence:
  `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp1-recorder.md`.
  Complete 2026-07-31: `PhysicsPipelineTraceRecorder` owns one saturated
  4,096-event count plus optional ordered payload retention. Ordinary Runtime
  selects count-only mode; Replay capture, the pipeline overlay, Debug
  SkullScope, and default direct/prediction engines retain full rows. Focused
  Profile/Debug saturation and field-faithfulness coverage passes, the original
  allocation-owner identity and 229,376-byte reservation remain unchanged, and
  `validate_fast` passes 455 cases / 2,423,400 assertions with clean ownership
  and reachability inventories.

- [x] **HP2 — Eliminate payload construction on the counting path.** On the
  counting path there must be no `PhysicsBodyPosition` load, no `sqrtf`, no
  record fill, and no per-row capacity compare against a bound that cannot
  change within a step. Mode selection follows the existing
  `CollectConvergenceDiagnostics` precedent: compile-time where the call is
  already templated, one hoisted branch outside the row loop otherwise. Do not
  introduce a callback, sink interface, or service bag on the hot path — a
  counting recorder is a value, not a polymorphic consumer. Evidence:
  `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-hp2-payload.md`.
  Complete 2026-07-31: all producer mode decisions are compile-time or hoisted
  outside their row loops. Count-only narrowphase and terrain slots hold
  disengaged payload optionals, stage counts are submitted in batches, and the
  persistent solver omits trace-only body-position loads and the diagnostic
  tangent-magnitude `sqrtf`. `RecordEvents` rejects full mode through Lane F.
  Focused equivalence and fatal-contract coverage, synchronized
  Automation/Debug/Profile reachability, 456 cases / 2,424,707 assertions,
  `validate_fast`, a 17/17 comment audit, and independent re-review pass.

- [ ] **HP3 — Prove exactness and record the measured win.** Physics CSV
  byte-exact against committed baselines. Replay sample hashes and the
  visual-fidelity golden unchanged **without refresh**. Overlay and SkullScope
  output identical when enabled. Allocation policy unchanged, including whether
  the 4096-record reservations are still required in counting mode. Report
  before/after Profile solver-inclusive and Physics-frame timings from HP0's
  scene. Evidence:
  `Agentic/Reports/2026-07-31/solver-diagnostic-hot-path-cost-closure.md`.

## Acceptance

No configuration constructs a pipeline-trace record that nothing reads.
`pipelineRecordCount` is byte-identical everywhere, so no replay artifact moves.
Physics remains byte-exact. The measured Profile solver cost is lower and the
number is recorded, not asserted.

## Validation

`tools\validate_tests.bat`, `tools\validate_physics.bat`,
`tools\validate_replay_visual_fidelity.bat`, allocation-policy self-test and
repository scan, `tools\validate_perf.bat`, `tools\validate_full.bat`.
