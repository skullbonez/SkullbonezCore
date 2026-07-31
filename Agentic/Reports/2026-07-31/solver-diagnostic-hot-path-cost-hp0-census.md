# Solver Diagnostic Hot-Path Cost — HP0 Census

Date: 2026-07-31
Branch: `nightrunner-30th-JUL-26`
Source tip: `9b09de95` (source-bearing code is unchanged from `1967a863`)
Scope: pipeline-trace producers, consumers, replay identity, and current
`perf_1000` cost

## Result

The pipeline trace is produced in Debug, Profile, and Release whether or not a
full-record consumer is active. The 16 enum stages all have live producer paths.
Every producer ultimately appends to the single
`PhysicsStepDiagnostics::m_physicsPipelineTrace` fixed list, which is cleared at
the start of each fixed step and saturates at 4,096 rows.

The plan's provisional 44-byte statement was incorrect. The current
`PhysicsPipelineRecord` is 56 bytes:

- four-byte padded `PhysicsPipelineStage`;
- three `int` fields and one `uint32_t`;
- two 12-byte `Vector3` values (`Vector3.h` asserts `sizeof(Vector3) == 12`);
- three `float` values.

The committed 4,096-row backing is therefore 229,376 bytes. This correction is
measurement evidence only and does not change the plan's design or acceptance.

The consumer split is real:

- presentation replay always needs the saturated row **count**;
- solver replay, Replay v2 save/restore, prediction cause-tree publication,
  SkullScope, and the pipeline overlay need complete ordered records;
- ordinary gameplay with Replay off, SkullScope off, and the pipeline overlay
  off has no full-record consumer but still constructs every retained payload.

HP1 must therefore preserve a saturated count independently of whether the
56-byte payload list is populated. It cannot delete `pipelineTrace` from solver
snapshots or replace the snapshot with a count.

## Producer Census

All rows below are outside configuration preprocessor gates. They execute in
Debug, Profile, and Release whenever their owning physics path is reached.
`PhysicsWorld` serially commits worker-emitted events into
`PhysicsStepDiagnostics`, while the persistent solver is already given the
remaining row capacity and emits into a bounded side-effect list.

| Stage | Producer owner and current sites | Payload-specific work |
|---|---|---|
| `BroadphaseCandidate` | `PhysicsBroadphaseStage.cpp:316-346`, `TryRecordBroadphaseCandidatePair` | two position loads for midpoint, two more for delta, magnitude and normalization |
| `SleepPrunedPair` | `PhysicsBroadphaseStage.cpp:293-314`, `TryRecordSleepPrunedCandidatePair` | two position loads and midpoint |
| `WakeDecision` | `PhysicsNarrowphaseStage.cpp:404-416,481-493`, worker event committed by `PhysicsWorld.cpp:738-744` | two position loads and midpoint |
| `SweptObjectHit` | `PhysicsNarrowphaseStage.cpp:369-386,446-463,539-554`, worker event committed by `PhysicsWorld.cpp:738-744` | two position loads and midpoint |
| `SweptObjectMiss` | `PhysicsNarrowphaseStage.cpp:521-533,569-580`, worker event committed by `PhysicsWorld.cpp:738-744` | two position loads and midpoint |
| `TerrainHit` | `PhysicsTerrainStage.cpp:185-205`, prepared commit appended by `PhysicsWorld.cpp:920-930` | body position fallback plus manifold values |
| `TerrainManifold` | `PersistentContactSolver.cpp:789-800`, `PersistentContactSolveTransaction::BuildTerrainRows` | first manifold point and normal |
| `TerrainRow` | `PersistentContactSolver.cpp:831-843`, `PersistentContactSolveTransaction::BuildTerrainRows` | point, normal, penetration and row metadata |
| `ManifoldRow` | `PersistentContactSolver.cpp:722-734`, `PersistentContactSolveTransaction::BuildManifolds` | point, normal, penetration and manifold indexes |
| `WarmStart` | `PersistentContactSolver.cpp:1147-1159`, `PersistentContactSolveTransaction::PrecomputeRows` | body position plus contact arm, normal and accumulated values |
| `SolverIteration` | `PersistentContactSolver.cpp:1308-1321`, `PersistentContactSolveTransaction::SolveRowsIterations` | body position plus contact arm and one `sqrtf(accT1² + accT2²)` per retained row |
| `VelocityWriteback` | `PersistentContactSolver.cpp:1653-1661`, `PersistentContactSolveTransaction::WriteBack` | body position and two vector magnitudes |
| `PositionCorrection` | `PersistentContactSolver.cpp:1808-1820`, `PersistentContactSolveTransaction::CorrectPositions` | body position plus contact arm |
| `CacheStore` | `PersistentContactSolver.cpp:1889-1901`, `PersistentContactSolveTransaction::StoreCache` | body position plus contact arm |
| `SleepSupportEdge` | `PersistentContactSolver.cpp:389-420`, `PersistentContactSolveTransaction::BuildManifolds` | two position loads and midpoint |
| `SleepIslandDecision` | `PhysicsSleepController.cpp:749-756,860-868`, `RunIslandStage` and `ApplyTransitions` | island/sleep decision values |

The append paths reconcile as follows:

1. broadphase owns `MutablePipelineTrace()` directly and bounds both helper
   producers against 4,096;
2. narrowphase worker events carry at most one record and
   `PhysicsWorld::CommitObjectNarrowphaseEvent` calls
   `RecordPipelineStage`;
3. terrain prepares one record and `PhysicsWorld::RunSolverPhysics` commits it;
4. `PersistentContactSolveTransaction` receives one remaining-capacity value,
   checks `effects.pipelineRecords.size()` through its `canRecordPipeline`
   closure, and `PhysicsWorld::CommitContactSolverConsequences` commits the
   bounded list;
5. `PhysicsSleepController` appends directly to the same list with its local
   bounded helper.

No producer is build-gated. The existing `CollectConvergenceDiagnostics`
template gate applies only to the sibling convergence trace.

## Consumer And Reachability Census

| Consumer | Full records or count | Build/configuration reachability | Runtime activation |
|---|---|---|---|
| Presentation `ReplayRecorder::Capture` / presentation hash (`ReplayRecorder.cpp:2005-2033`) | Count only | Debug, Profile, Release | Replay recording enabled by `--replay on` or a replay hash-log path |
| Solver `ReplayRecorder::Capture` (`ReplayRecorder.cpp:2754-2768`) | Count plus a full `PhysicsSolverSnapshot` | Debug, Profile, Release; Profile is used by replay visual-fidelity validation | Same Replay recording switch; presentation and solver recorders are configured together in `ReplayTimeline::ConfigureRecording` |
| Solver snapshot hash (`ReplayRecorder.cpp:1555-1560`) | Complete ordered records and every field | Debug, Profile, Release | Whenever a solver replay sample/snapshot is hashed |
| Replay v2 artifact (`ReplayV2Artifact.cpp:655-660,1740-1747`) | Complete ordered records and every field | Debug, Profile, Release | Explicit Replay artifact save/load/probe paths |
| Prediction cause-tree lookup (`ReplayAuthoringCauseTree.cpp:275-291`, `ReplayPredictionPublication.cpp:1084-1100`) | Record identity fields and stable list index | Debug, Profile, Release | Prediction/cause-tree publication from retained solver snapshots |
| Physics solver restore (`PhysicsStepDiagnostics.cpp:236-260`) | Complete records copied back in order | Debug, Profile, Release | Replay solver restore/scrub |
| SkullScope (`SkullScope.cpp:443-468`) | Complete records, reduced to stage counts during serialization | Debug only | `--physics-diag <path>`; `PhysicsStepDiagnostics::ShouldEmitStepDiagnostics` rejects it outside `_DEBUG` |
| Runtime post-physics overlay (`RuntimeOverlayDiagnostics.cpp:176-189`) | Borrowed span every frame; no iteration by this call | Debug, Profile, Release | Always builds the view; full-record iteration is deferred to render policy |
| Render model publication (`RenderModelFramePublisher.cpp:37-54`) | Borrowed span every rendered frame; no iteration by publication | Debug, Profile, Release | Always publishes the view |
| `PhysicsDebugVisualizer::Render` (`PhysicsDebugVisualizer.cpp:612-646`) | Complete records for the selected pipeline stage | Debug, Profile, Release | Only when `physicsDebugFlags` contains `PHYSICS_DEBUG_PIPELINE`; set by scene data, `--physics-debug`, Legacy/ImGui controls, or the C-key `All` mode |

`PhysicsSolverSnapshot::pipelineTrace` is not count-only storage. Capture and
restore copy every record; the solver-snapshot hash includes list size, order,
and all record fields; Replay v2 serializes the same content; and prediction
searches records by feature/body identity and retains their indexes. HP1/HP2
must select full recording whenever any of those consumers can run.

The replay identity edge is independently present in all presentation and
solver sample hashes:

- `ReplayRecorder.cpp:1576`;
- `ReplayRecorder.cpp:1815`;
- `ReplayRecorder.cpp:2033`;
- `ReplayRecorder.cpp:2796`;
- the corresponding test hash in `TestDeterminism.cpp:842`.

Profile is the configuration used by
`tools\validate_replay_visual_fidelity.bat`; a changed Profile count changes the
golden hash and is a task failure, not refresh authority.

## Current `perf_1000` Measurement

### Profile timing

Command:

```text
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --hide-top-text --automation-hidden-window --scene SkullbonezData/scenes/perf_1000.scene.json
```

The scene emitted two deterministic performance passes. Each pass discarded
the first 30 frames and retained frames 31-1000, for 970 rows per pass and
1,940 measured rows total.

| Scope | Mean ms | P50 ms | P95 ms | P99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| `Frame/Physics` | 1.575496 | 1.463350 | 2.447700 | 2.693900 | 5.957800 |
| `Frame/Physics/Narrowphase/PersistentContacts` | 0.272922 | 0.119050 | 0.822700 | 0.926200 | 4.512600 |
| `Frame/Physics/Narrowphase/PersistentContacts/SolveRows` | 0.120002 | 0.019750 | 0.408800 | 0.458900 | 0.603600 |

The two `SolveRows` means were 0.121008 ms and 0.118996 ms. HP3 must use the
same command, scope names, two-pass shape, and warmup exclusion.

### Debug bounded trace

Exact trace command:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --hide-top-text --automation-hidden-window --frames 180 --scene SkullbonezData/scenes/perf_1000.scene.json --physics-diag Debug/hp0_perf_1000.physicsdiag.ndjson
```

The scene emitted two byte-identical 180-frame runs. Per run:

| Measurement | Value |
|---|---:|
| Fixed steps | 180 |
| Retained records | 296,714 |
| Mean retained records / step | 1,648.411111 |
| Minimum / maximum records | 1,000 / 2,356 |
| Saturated steps (`>= 4,096`) | 0 / 180 |
| `SolverIteration` records | 866 |
| Pipeline-only `sqrtf` executions | 866 |
| Retained payload bytes written (`records * 56`) | 16,615,984 |

The stage totals reconcile to 296,714 records. The largest contributors in the
bounded sample were 180,000 `SleepIslandDecision` rows and 114,000
`VelocityWriteback` rows; the inner solver path produced 866
`SolverIteration` rows. This sample deliberately bounds diagnostic artifact
size while the complete Profile run above supplies the 1,000-frame timing.

## SkullScope Data Accounting

Artifacts:

- `Debug/hp0_perf_1000.physicsdiag.ndjson`: 348,925,625 bytes;
- `Debug/hp0_perf_1000.physicsdiag.sqlite`: 173,805,568 bytes.

Queries read by the model:

1. `tools\physics_query.bat Debug\hp0_perf_1000.physicsdiag.ndjson pipeline --frames 0:179 --limit 20`
   - 8,240 characters / 8,240 UTF-8 bytes.
2. `tools\physics_query.bat Debug\hp0_perf_1000.physicsdiag.ndjson sql "select count(*) as frames, min(record_count) as min_records, max(record_count) as max_records, round(avg(record_count), 6) as avg_records, sum(case when record_count >= 4096 then 1 else 0 end) as saturated_frames from pipeline_stages where frame between 0 and 179" --limit 5`
   - 377 characters / 377 UTF-8 bytes.
   - This first aggregate intentionally exposed both runs as one 360-row
     aggregate and was superseded by query 3.
3. `tools\physics_query.bat Debug\hp0_perf_1000.physicsdiag.ndjson sql "select run_id, count(*) as frames, min(record_count) as min_records, max(record_count) as max_records, round(avg(record_count), 6) as avg_records, sum(case when record_count >= 4096 then 1 else 0 end) as saturated_frames from pipeline_stages where frame between 0 and 179 group by run_id order by run_id" --limit 10`
   - 526 characters / 526 UTF-8 bytes.

Total model-read SkullScope output: 9,143 characters / 9,143 UTF-8 bytes.
Neither raw artifact was read by the model. No query output was truncated.

## HP1 Contract

HP1 may proceed with these constraints:

1. the authoritative count saturates at 4,096 independently of retained
   payload rows;
2. all 16 producer stages count every attempt in their present canonical order;
3. full-record mode preserves the exact current 56-byte values and order;
4. count-only mode performs no `PhysicsPipelineRecord` construction;
5. full-record mode is required by pipeline overlay, Debug SkullScope, and
   enabled solver Replay/prediction/artifact paths;
6. presentation replay alone is a count consumer, but the current configuration
   enables its solver recorder sibling, so Replay-on selects full recording;
7. no configuration macro may substitute for the runtime consumer decision.
