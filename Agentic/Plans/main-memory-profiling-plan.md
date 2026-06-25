# Main Memory Profiling Plan

Date: 2026-06-25
Status: Draft
Impact area: diagnostics/profiling, replay runtime, game object and physics stores, in-game UI, command-line automation
Validation note: this plan-only edit requires no validation. Runtime implementation will touch `Run*`, `Init*`, diagnostics, replay, and UI paths, so PR-bound work should use `tools\validate_full.bat`. Add `tools\validate_ui.bat` for profiler-window layout changes and `tools\validate_perf.bat` if memory accounting is sampled continuously in normal Profile runs.

## Goal

Show current main memory consumption in the in-game Profiler window and expose
the same data through an automated dump path.

The required top-level numbers are:

```text
Main Memory (TaskMgr-aligned process total)
  Replay
  Game Objects
  Other/Unattributed Process Memory
  Sum
```

The sum shown by the engine must reconcile to the same process memory metric the
user sees for the app in Task Manager. Engine-owned buckets cannot explain every
byte directly, so the report must include an explicit unattributed bucket:

```text
unattributed_process_bytes =
    task_manager_process_bytes - tracked_engine_main_memory_bytes
```

After that bucket is included, the displayed sum must equal the sampled process
memory number.

## Current Read

Relevant existing pieces:

| Area | File | Current behavior |
|------|------|------------------|
| Perf memory logging | `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp` | `RuntimeDiagnostics::LogPerfMemory()` already uses `GetProcessMemoryInfo()` and writes `# MEM ... working_set_mb=...` to perf CSV logs. |
| Diagnostics ownership | `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` | `DiagnosticsRuntime` owns capture/perf/SkullScope controllers and is the right boundary for memory dump plumbing. |
| Profiler window | `SkullbonezSource/UI/UITabProfiler.h/.cpp` | Profiler tab owns timing rows, worker controls, timeline, and performance histogram. |
| UI frame data | `SkullbonezSource/UI/UI.h` | `InGameUIFrameData` carries per-frame engine state into the UI without letting widgets mutate runtime state. |
| UI data assembly | `SkullbonezSource/Runtime/RunUiTextPass.cpp` | Builds `InGameUIFrameData` before drawing the in-game UI. |
| Replay owner | `SkullbonezSource/Runtime/Replay/ReplayRuntime.h` | Owns presentation replay, solver replay, event replay, loaded presentation, scrubber/prediction/cause-tree state, focus masks, render backups, and ghost requests. |
| Replay recorder stats | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | Existing stats expose counts/capacities, but not retained byte counts. |
| Replay samples | `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` | Presentation and solver samples contain nested vectors; solver samples also carry full `ReplaySolverWorldSnapshot` vectors. |
| Game object owner | `SkullbonezSource/GameObjects/GameModelCollection.h` | Owns `std::vector<GameModel>`, SoA cache, physics engine, and replay body ids. |
| Physics stores | `SkullbonezSource/Physics/PhysicsScene.h` | Physics facade owns body/collider/render stores plus `PhysicsWorld`, which holds sleep/contact/broadphase/debug vectors. |

There is also an existing broader plan at
`Agentic/Plans/replay-memory-quality-tuning-plan.md`. This plan should land
first as instrumentation: it measures and reconciles current memory before any
replay compaction or memory-budget enforcement work.

## Definitions

| Term | Meaning |
|------|---------|
| Main memory | CPU-visible process memory. Do not mix GPU-only default heap memory into these numbers. |
| TaskMgr process memory | The OS process metric selected to match Task Manager's Memory column on the target Windows build. |
| Tracked engine memory | Bytes attributed to engine-owned C++ objects and their dynamic storage. |
| Replay memory | Main-memory storage owned by `ReplayRuntime` and its recorders, loaded replay data, prediction data, cause/path state, render-pose backups, focus masks, and replay ghost requests. |
| Game object memory | Main-memory storage owned by `GameModelCollection`, its `GameModel` vector, render/physics stores, SoA cache, and model-driven physics/debug/broadphase vectors. |
| Unattributed process memory | The difference between TaskMgr process memory and tracked engine memory. This includes allocator slack, CRT heap metadata, thread stacks, DLL/runtime overhead, driver mappings, file buffers, UI/fonts, diagnostics not yet bucketed, and any engine systems not tracked in the first pass. |

## Process Metric Contract

Do not assume `WorkingSetSize` is the exact number Task Manager shows on every
Windows version and Task Manager view.

Phase 1 should add a small Windows process-memory sampler that reports at least:

| Field | Source | Purpose |
|-------|--------|---------|
| `working_set_bytes` | `PROCESS_MEMORY_COUNTERS_EX::WorkingSetSize` | Existing behavior and broad resident memory. |
| `private_commit_bytes` | `PROCESS_MEMORY_COUNTERS_EX::PrivateUsage` | Committed private virtual memory. |
| `pagefile_usage_bytes` | `PROCESS_MEMORY_COUNTERS_EX::PagefileUsage` | Compatibility with older process counter naming. |
| `task_manager_memory_bytes` | Chosen calibrated field | The value used for UI reconciliation. |
| `task_manager_metric_name` | String | Makes dumps self-describing, for example `working_set` or `private_working_set`. |

Implementation should locally compare the sampled fields against Task Manager
for `Profile\SKULLBONEZ_CORE.exe` and document the chosen metric. If neither
`WorkingSetSize` nor `PrivateUsage` matches the Task Manager Memory column
closely enough, add private-working-set sampling through the Windows working-set
query APIs and use that field for `task_manager_memory_bytes`.

The UI and dump should still include all sampled OS fields so automated tests
can detect future metric drift.

## Target Data Shape

Add a lightweight diagnostics data model, for example under runtime diagnostics:

```cpp
struct MainMemoryProcessStats
{
    uint64_t taskManagerMemoryBytes = 0;
    uint64_t workingSetBytes = 0;
    uint64_t privateCommitBytes = 0;
    uint64_t pagefileUsageBytes = 0;
    char taskManagerMetricName[32] = {};
};

struct MainMemoryReplayStats
{
    uint64_t presentationBytes = 0;
    uint64_t solverBytes = 0;
    uint64_t eventsBytes = 0;
    uint64_t loadedPresentationBytes = 0;
    uint64_t predictionBytes = 0;
    uint64_t pathAndCauseBytes = 0;
    uint64_t renderScratchBytes = 0;
    uint64_t totalBytes = 0;
};

struct MainMemoryGameObjectStats
{
    uint64_t modelBytes = 0;
    uint64_t soaCacheBytes = 0;
    uint64_t physicsStoreBytes = 0;
    uint64_t renderStoreBytes = 0;
    uint64_t physicsWorldBytes = 0;
    uint64_t debugAndBroadphaseBytes = 0;
    uint64_t totalBytes = 0;
};

struct MainMemoryStats
{
    MainMemoryProcessStats process;
    MainMemoryReplayStats replay;
    MainMemoryGameObjectStats gameObjects;
    uint64_t trackedEngineBytes = 0;
    uint64_t unattributedProcessBytes = 0;
    uint64_t reconciledTotalBytes = 0;
    int modelCount = 0;
    uint64_t replayPresentationSamples = 0;
    uint64_t replaySolverSamples = 0;
};
```

`reconciledTotalBytes` should always equal `process.taskManagerMemoryBytes` after
clamping negative deltas to zero and recording any overshoot separately:

```text
trackedEngineBytes = replay.totalBytes + gameObjects.totalBytes + otherTrackedBytes
unattributedProcessBytes = max(0, taskManagerMemoryBytes - trackedEngineBytes)
trackedOvershootBytes = max(0, trackedEngineBytes - taskManagerMemoryBytes)
reconciledTotalBytes = trackedEngineBytes + unattributedProcessBytes
```

If `trackedOvershootBytes` is nonzero, show it in dumps and the Profiler window
as an accounting warning. That means the engine estimates are larger than the OS
resident metric and the chosen Task Manager metric or bucket math needs review.

## Accounting Rules

Use owned capacity, not logical size, for engine buckets:

```cpp
template <typename T>
uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() ) * sizeof( T );
}
```

Rules:

1. Include the owning object itself when it is stored inline inside a larger owner only once.
2. Include dynamic vector capacity, not only `size()`.
3. Include nested vector capacities for replay samples and solver snapshots.
4. Include ring capacity even if the current sample count is lower; reserved memory is still resident or committed by the process.
5. Do not count borrowed pointers.
6. Do not count GPU default-heap resources in main memory.
7. Do not double-count the same vector through both a high-level owner and a lower-level store.
8. Keep first-pass estimates honest: allocator metadata and page rounding belong in `unattributedProcessBytes` unless the project later adds allocator hooks.

## Replay Bucket

Add memory stats methods to replay owners:

```cpp
ReplayMemoryStats ReplayRecorder::GetMemoryStats() const;
ReplayMemoryStats ReplaySolverRecorder::GetMemoryStats() const;
ReplayEventMemoryStats ReplayEventRecorder::GetMemoryStats() const;
MainMemoryReplayStats ReplayRuntime::CollectMemoryStats() const;
```

Presentation recorder should count:

- `m_samples` capacity times `sizeof(ReplayPresentationSample)`.
- Each retained or allocated sample's `bodies.capacity() * sizeof(ReplayBodyPresentationSample)`.
- `m_checkpoints` capacity.
- scratch vectors: `m_contactCountScratch`, `m_maxPenetrationScratch`, `m_normalImpulseSumScratch`.
- hash-log object is not meaningfully measurable without allocator hooks; leave its stream internals to unattributed.

Solver recorder should count:

- `m_samples` capacity times `sizeof(ReplaySolverFrameSample)`.
- Each sample's `bodies` capacity.
- Each sample's `ReplaySolverWorldSnapshot` vector capacities:
  - sleep/timer/tornado vectors,
  - persistent contact vectors,
  - contact cache vectors,
  - debug contact vectors,
  - pipeline trace vectors,
  - collision cell keys,
  - sleep island vectors.
- Each sample's `ReplayLauncherVisualSample` nested vectors.
- `m_checkpoints` capacity.
- scratch vectors.

Event recorder should count:

- `m_events.capacity() * sizeof(ReplayEventSample)`.

Replay runtime should also count:

- loaded presentation samples and their body vectors,
- prediction world snapshots and live restore snapshots,
- prediction body backup vectors,
- prediction frame vectors and their body/debug-contact vectors,
- future path nodes, path targets, cause-tree rows,
- render pose backups, ghost draw requests, focus model mask,
- launcher visual backup vectors.

Performance guard:

Do not fully scan every retained replay sample every rendered frame. Either keep
byte totals incrementally updated as ring slots are allocated/overwritten, or
refresh the expensive nested-vector scan only:

- when the Profiler tab is visible,
- on an explicit memory dump,
- at a coarse interval such as once per second,
- after recorder configuration/reset.

## Game Object Bucket

Add `CollectMemoryStats()` or equivalent methods across the game-object and
physics boundaries:

```cpp
MainMemoryGameObjectStats GameModelCollection::CollectMemoryStats() const;
PhysicsMemoryStats PhysicsEngine::CollectMemoryStats() const;
PhysicsMemoryStats PhysicsScene::CollectMemoryStats() const;
PhysicsWorldMemoryStats PhysicsWorld::CollectMemoryStats() const;
```

First-pass game object accounting should count:

| Bucket | Contents |
|--------|----------|
| `modelBytes` | `m_gameModels.capacity() * sizeof(GameModel)`. |
| `soaCacheBytes` | SoA cache vector capacities and any stream/cache buffers. |
| `physicsStoreBytes` | `PhysicsBodyStore`, `ColliderStore`, and related body/collider arrays. |
| `renderStoreBytes` | `RenderInstanceStore` CPU-side arrays. |
| `physicsWorldBytes` | Solver-owned sleep, contact, manifold, persistent-contact, joint, tornado, and integration vectors. |
| `debugAndBroadphaseBytes` | Debug contacts, pipeline trace, collision cell keys, spatial grid storage, visual state arrays. |

If a store does not expose its vectors cleanly yet, add a local memory-stats
method inside that owner. Do not make diagnostics reach through private storage
with friendship just for this report.

## Profiler Window UI

Extend `InGameUIFrameData` with a `MainMemoryStats` snapshot or a compact
display-only version.

Add a `Main Memory` section to the Profiler tab, below timing/worker controls
and above or below the performance histogram. The first version should be dense
and scan-friendly:

```text
MAIN MEMORY
TaskMgr: 812.4 MiB  Metric: private_working_set
Replay: 438.1 MiB
  Presentation 61.7  Solver 348.9  Events 1.2  Prediction 26.3
Game Objects: 154.6 MiB
  Models 18.2  Stores 44.1  PhysicsWorld 92.3
Unattributed: 219.7 MiB
Sum: 812.4 MiB
```

UI requirements:

1. Use MiB consistently and keep raw bytes in automated dumps.
2. Show the metric name beside the process total.
3. Show a warning if `trackedOvershootBytes > 0`.
4. Keep the section allocation-free while drawing.
5. Reuse the Profiler tab scroll area and row layout conventions.
6. Avoid claiming GPU memory is included.

Optional later polish:

- add collapsible sub-rows like the profiler marker tree,
- add a small history sparkline,
- color the unattributed row amber when it is larger than tracked engine memory.

## Automated Dump

Add a command-line driven dump for automated tests, for example:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\perf_test.scene.json --fixed-step --memory-dump TestOutput\memory\current.json
```

Suggested CLI:

| Flag | Behavior |
|------|----------|
| `--memory-dump <path>` | Writes the latest `MainMemoryStats` JSON at scene completion or process shutdown. |
| `--memory-dump-frame <n>` | Optional: writes once when the current scene frame reaches `n`. |
| `--memory-dump-interval <n>` | Optional: writes NDJSON every `n` frames for trend tests. |

Keep the minimum implementation simple: `--memory-dump <path>` writes one JSON
object at shutdown using the latest collected stats. If a scene reaches its
target frame and exits normally, that dump is the automated "current usage" at
the end of the run.

JSON schema:

```json
{
  "schema": "skullbonez.main_memory.v1",
  "checkpoint": "shutdown",
  "frame": 240,
  "process": {
    "task_manager_metric_name": "private_working_set",
    "task_manager_memory_bytes": 851902464,
    "working_set_bytes": 913948672,
    "private_commit_bytes": 1046478848
  },
  "replay": {
    "total_bytes": 459382784,
    "presentation_bytes": 64692224,
    "solver_bytes": 365875200,
    "events_bytes": 12582912,
    "prediction_bytes": 16232448
  },
  "game_objects": {
    "total_bytes": 162109440,
    "model_bytes": 19087360,
    "physics_store_bytes": 46268416,
    "physics_world_bytes": 96753664
  },
  "tracked_engine_bytes": 621492224,
  "unattributed_process_bytes": 230410240,
  "tracked_overshoot_bytes": 0,
  "reconciled_total_bytes": 851902464,
  "reconciliation_delta_bytes": 0
}
```

Automated tests should assert:

```text
reconciled_total_bytes == process.task_manager_memory_bytes
reconciliation_delta_bytes == 0
tracked_overshoot_bytes == 0
replay.total_bytes >= replay.presentation_bytes + replay.solver_bytes + replay.events_bytes
game_objects.total_bytes >= game_objects.model_bytes
```

## Implementation Phases

### Phase 1: Process Memory Sampler

Goal: replace the current coarse perf-log memory line with a reusable process
memory sampler.

Tasks:

1. Add a Windows-only sampler near diagnostics, not in the timing profiler.
2. Use `PROCESS_MEMORY_COUNTERS_EX`.
3. Keep the old perf-log `# MEM` line working, but include the chosen metric and additional OS fields.
4. Compare output against Task Manager and document which field feeds `task_manager_memory_bytes`.
5. Add a small helper for formatting bytes as MiB for UI text.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Perf log memory line | Still appears at existing start/periodic/end checkpoints. |
| OS fields | Dump/log includes working set and private commit. |
| Task Manager comparison | Chosen field is documented and visible in output. |

### Phase 2: Replay Memory Stats

Goal: make replay's main-memory use visible by owner and track.

Tasks:

1. Add memory-stats structs for presentation, solver, and event recorders.
2. Count vector capacities and nested sample vectors.
3. Count loaded replay, prediction, path/cause, ghost, focus-mask, and render-backup storage in `ReplayRuntime`.
4. Cache expensive totals or update them incrementally so the UI does not scan the whole replay ring every frame.
5. Add Debug/Profile assertions or comments that clarify bytes are owned-capacity estimates, not allocator-exact resident pages.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Replay disabled | Replay total is near zero except fixed owner structs and inactive scratch buffers. |
| Replay enabled | Presentation, solver, and event bytes increase with retention/object count. |
| Prediction enabled | Prediction bucket increases separately from retained replay. |
| Large retained run | UI refresh remains responsive. |

### Phase 3: Game Object And Physics Memory Stats

Goal: show how much current scene objects and object-driven stores consume.

Tasks:

1. Add memory stats to `GameModelCollection`.
2. Add memory stats to SoA cache and stores.
3. Add memory stats to `PhysicsEngine`, `PhysicsScene`, and `PhysicsWorld`.
4. Break debug/broadphase memory out from core model memory.
5. Include model count and model vector capacity in both UI and dump output.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Generated object count increases | Game object memory increases predictably. |
| Zero-object scene | Game object total drops to fixed store overhead. |
| Physics debug/broadphase enabled | Debug/broadphase sub-buckets reflect added storage. |

### Phase 4: Reconciled Snapshot

Goal: produce one coherent `MainMemoryStats` snapshot from process, replay, and
game-object data.

Tasks:

1. Add `DiagnosticsRuntime::CollectMainMemoryStats(...)` or an equivalent method that receives `ReplayRuntime` and `GameModelCollection` references.
2. Compute `trackedEngineBytes`.
3. Compute `unattributedProcessBytes`, `trackedOvershootBytes`, `reconciledTotalBytes`, and `reconciliationDeltaBytes`.
4. Keep this collection side-effect-free: no simulation, recorder, renderer, or UI mutation.
5. Throttle normal UI sampling to a safe cadence.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Normal run | `reconciledTotalBytes == task_manager_memory_bytes`. |
| Engine estimates below process total | Difference appears as unattributed. |
| Engine estimates exceed process metric | Overshoot warning appears and dumps show nonzero `tracked_overshoot_bytes`. |

### Phase 5: Profiler Window Integration

Goal: write the memory report into the in-game Profiler window.

Tasks:

1. Extend `InGameUIFrameData` with memory stats.
2. Populate the stats in `RunUiTextPass.cpp` when the UI is being built.
3. Add a `Main Memory` section in `UITabProfiler.cpp`.
4. Add content height for the new rows.
5. Keep formatting compact enough for common desktop and minimum UI sizes.
6. Add a warning row if the accounting overshoots the process metric.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Profiler tab visible | Shows TaskMgr process total, replay, game objects, unattributed, and sum. |
| Replay disabled/enabled | Replay row visibly changes. |
| Object count changed | Game object row visibly changes. |
| Small UI window | Text remains readable and does not overlap controls. |

### Phase 6: Automated Memory Dump

Goal: let automated tests capture current memory usage without screen scraping.

Tasks:

1. Add CLI parsing for `--memory-dump <path>`.
2. Store dump path in runtime diagnostics state.
3. Write JSON at scene completion or shutdown.
4. Add optional frame/interval dump flags only if the first automated use case needs them.
5. Write atomically enough for tests: use a temp file plus rename, or flush and close before exit.
6. Print a short stdout line with the dump path and TaskMgr MiB total.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| `--memory-dump TestOutput\memory.json` | File is produced on normal exit. |
| JSON parse | All required numeric fields are present as bytes. |
| Reconciliation | `reconciled_total_bytes` equals `process.task_manager_memory_bytes`. |
| Existing perf logs | Existing `perf_log.csv` behavior is preserved. |

## Validation Strategy

Do not run repository validation scripts while iterating unless a specific
question requires it. For PR-bound implementation:

| Change | Validation |
|--------|------------|
| Plan only | No validation required. |
| Process sampler and diagnostics dump only | `tools\validate_fast.bat`, then `tools\validate_full.bat` because `Init*`/`Run*` paths are touched. |
| Profiler window layout | `tools\validate_ui.bat` plus `tools\validate_full.bat`. |
| Hot-path or continuous memory sampling | `tools\validate_perf.bat` plus `tools\validate_full.bat`. |
| Replay stats methods that only inspect storage | `tools\validate_fast.bat`; add `tools\validate_perf.bat` if refresh cost is nontrivial. |
| Game object/physics stats methods that only inspect storage | `tools\validate_fast.bat`; add `tools\validate_physics.bat` only if solver state or behavior changes. |

## Rubber-Duck Review Notes

Expected outcome: the plan must let an implementer produce UI and JSON memory
reports that reconcile to Task Manager while still showing replay and game
object ownership separately.

Findings:

- Blocking: "Task Manager memory" is ambiguous unless the implementation
  calibrates and names the exact OS metric. The plan requires multiple OS fields
  and a named `task_manager_metric_name`.
- Blocking: tracked engine buckets will not naturally equal process memory.
  The plan explicitly requires `unattributedProcessBytes` so the displayed sum
  can equal the process total without false precision.
- Non-blocking: vector-capacity estimates omit allocator metadata and page
  rounding. This is acceptable because those bytes reconcile through
  unattributed memory.
- Non-blocking: scanning every replay sample to count nested vectors can become
  expensive. The plan requires cached or throttled stats before UI integration.

Missing evidence:

- A local comparison against Task Manager is needed during implementation to
  choose `task_manager_memory_bytes`.
- Large-scene timing evidence is needed if memory stats refresh more often than
  manual dumps or visible Profiler-tab sampling.

Next step:

Implement Phase 1 and Phase 4 first, then wire the JSON dump before spending UI
time. That proves the Task Manager reconciliation contract early.

## Definition Of Done

This work is complete when:

1. The Profiler tab shows main memory consumption with process total, replay,
   game objects, unattributed memory, and reconciled sum.
2. The process total is explicitly labeled with the Task Manager-aligned metric
   name.
3. Replay memory includes presentation, solver, events, loaded replay,
   prediction, path/cause, and replay render scratch storage.
4. Game object memory includes `GameModelCollection`, model capacity, SoA cache,
   physics stores, render stores, physics world, debug, and broadphase storage.
5. Automated `--memory-dump <path>` output writes byte-exact JSON fields for the
   same stats shown in the UI.
6. Dump reconciliation satisfies:
   `reconciled_total_bytes == process.task_manager_memory_bytes`.
7. Existing perf-log memory checkpoints keep working.
8. UI and automated dumps stay side-effect-free and do not change physics,
   replay, or rendering behavior.
9. Required validation gates pass for the implementation changes that land.
