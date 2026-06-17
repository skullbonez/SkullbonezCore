# Physics And Shadow Worker Parallelization Plan

Status: implemented with worker dispatch enabled by default and profiler-visible
Created: 2026-06-17
Scope: worker-system first-use jobs, deterministic physics parallelization, CPU-side shadow-map preparation, renderer stretch goals

## Purpose

Use the worker system from `Agentic/Plans/worker-system-plan.md` to attack the
two current CPU bottleneck families:

- physics narrowphase/contact work,
- shadow-map generation CPU preparation.

The goal is not to make the engine generally multithreaded in one step. The
goal is to create the worker system with these two concrete jobs as its first
customers, prove deterministic behavior, and keep renderer command submission
on the main thread until the DX12 backend is ready for asynchronous command-list
recording.

## Related Plans

| Plan | Relationship |
|------|--------------|
| `Agentic/Plans/worker-system-plan.md` | Worker pool, fences, config, `ParallelFor`, and per-thread profiling support used by this plan. |
| `Agentic/Plans/dx12-render-graph-completion-plan.md` | Prerequisite direction for the stretch goal where render passes record independent DX12 command lists. |

## Validation Rule

This plan spans physics, renderer, and hot-path performance work. During
implementation, run focused builds or launches only when they answer a specific
question. Before PR-bound commits, use the narrowest applicable gates:

| Change | Required PR Gate |
|--------|------------------|
| Worker infrastructure only | `tools\validate_fast.bat` |
| Physics behavior or determinism | `tools\validate_physics.bat` |
| Shadow rendering, renderer backend, shader, or visual output | `tools\validate_dx12_renderer.bat` |
| Hot-path scheduling, worker overhead, or allocation-sensitive changes | `tools\validate_perf.bat` |
| Mixed physics + renderer slices | `tools\validate_full.bat` after narrower gates pass |

If profiling marker forwarding changes, also run:

```bat
Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers
```

## Worker Implementation Update: 2026-06-17

The deterministic worker-safe slices are implemented and enabled by default.
Focused probes showed that some same-machine workloads may not improve yet, but
the user explicitly accepted keeping the worker paths in place so profiler
evidence can guide the next optimization pass instead of hiding the behavior
behind compile-time deferrals.

- `worker_threads = -1`, `physics_parallel = 1`, and
  `shadow_parallel_prep = 1` are the default config state.
- Physics per-body jobs use workers for force application, tornado-field
  updates, terrain candidate detection, and remaining-time integration while
  preserving serial commit order for diagnostics and terrain manifolds.
- Object narrowphase has a deterministic event/commit boundary and island
  worker dispatch enabled through
  `Frame/Physics/Narrowphase/IslandWorkerDispatch/WorkerIslands`.
- Shadow caster batches are built once per frame and reused for both shadow
  maps. Ordered worker fill/scans are enabled for object-heavy scenes and expose
  `WorkerBuildBatches` / `WorkerScanBounds` profiler markers.
- The profiler tab now exposes a Workers on/off checkbox, worker-count slider,
  worker workload markers, and a CPU Cores section under Draw Calls that reports
  each worker in flight, job count, core ms, and frame span.
- Current acceptance prioritizes deterministic, controllable, profiler-visible
  worker execution over immediate speedup in every tested scene. Remaining
  same-machine regressions are tracked as follow-up optimization work.

## Current Constraints

| Area | Constraint |
|------|------------|
| Physics determinism | Fixed-step physics baselines are byte-exact. Worker execution must not reorder floating-point reductions or diagnostic output in a way that changes committed CSVs unless intentional. |
| Narrowphase side effects | The current object/object narrowphase loop wakes bodies, advances positions, changes `m_timeRemaining`, records diagnostics, and appends collision-cell keys while iterating candidate pairs. |
| Terrain contact output | Terrain detection appends manifolds and updates sleep support/inhibition state. Parallel workers must not push directly into shared vectors. |
| Persistent contact solver | The shared Catto-style row solver remains a serial barrier until rows can be partitioned by independent islands. |
| Shadow map rendering | The current pass renders a terrain/object shadow map and a tight object shadow map serially. Both object caster passes rebuild instance batches. |
| Renderer backend | `Gfx()` owns mutable render state and the DX12 backend records through one command list and one per-frame upload arena today. Worker threads must not call `Gfx()` in the initial implementation. |
| Profiler | Worker timings use thread-safe worker sample recording and are published as last-frame marker rows plus a CPU Cores table. Normal main-thread profiler scopes still remain stack-owned by the main thread. |

## Target Architecture

The worker system should expose two first-class job shapes:

1. `ParallelFor` for frame-critical work that must complete before the next
   pipeline stage.
2. Ordered parallel collection for work that produces records from independent
   chunks and merges them by stable input order.

Physics uses both shapes. Shadow-map preparation mostly uses ordered parallel
collection. The renderer stretch goal later uses independent pass command
recording, but that is deliberately outside the first implementation.

## Phase 0: Baseline And Boundaries

Goal: know exactly what is expensive before moving work to threads.

Steps:

1. Capture current profiler costs for:
   - `Frame/Physics/Narrowphase`,
   - `Frame/Physics/Terrain`,
   - `Frame/Physics/ContactSolver` or the nearest existing solver marker,
   - `Frame/Shadows`,
   - `Frame/Shadows/ShadowMap/BuildObjectFrame`,
   - `Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches`.
2. Add missing profiler markers only if needed to distinguish detection,
   event commit, solver, batch build, upload, and draw submission.
3. Record representative scenes:
   - clustered physics scene,
   - spread-out physics scene,
   - shadow-heavy object scene,
   - cinematic scene with terrain and object shadows enabled.
4. Confirm whether the shadow bottleneck is CPU batch construction, upload
   submission, draw-call recording, or GPU execution.
5. Document the baseline in an `Agentic/Reports/<date>/` report before
   changing behavior.

Acceptance criteria:

- The bottleneck markers are visible in the profiler UI or log.
- There is a baseline for single-threaded, worker-disabled performance.
- The report names the scenes and launch commands used.

## Phase 1: Worker System Built For These Jobs

Goal: implement the worker system from `worker-system-plan.md` with the
requirements imposed by physics and shadow prep.

Steps:

1. Add `SkullbonezWorkerPool` with a fixed thread count initialized once at
   startup.
2. Add config and CLI controls:
   - `worker_threads = -1`,
   - `physics_parallel = 0/1`,
   - `shadow_parallel_prep = 0/1`.
3. Add `ParallelFor(begin, end, fn)` with:
   - stable chunk partitioning,
   - inline fallback for small ranges,
   - no per-frame thread creation,
   - no unbounded per-frame allocation.
4. Add an ordered chunk helper:
   - each worker writes to chunk-local scratch,
   - the main thread merges chunks by chunk index,
   - output order matches the original serial order.
5. Add worker scratch storage for:
   - physics events,
   - terrain contact candidates,
   - shadow caster instance streams.
6. Keep `Gfx()` inaccessible from worker jobs by policy and debug assertions.
7. Add worker timing counters that merge into main-thread profiler rows at the
   synchronization point.
8. Add worker-disabled code paths so `worker_threads = 0` is a behaviorally
   identical baseline.

Acceptance criteria:

- Worker pool can run no-op and numeric `ParallelFor` tests deterministically.
- Worker-disabled and worker-enabled fixed-step physics output still matches
  before any physics stage is parallelized.
- No worker job calls renderer APIs.

## Phase 2: Physics Parallelization, Low-Risk Stages First

Goal: move independent per-body physics work onto workers without changing
narrowphase behavior yet.

Steps:

1. Convert force application to `ParallelFor`.
   - Each worker owns one body index at a time.
   - Sleeping and fixed bodies keep the same `m_timeRemaining` behavior.
2. Audit tornado-field application.
   - If it is per-body with disjoint writes, move it after force application
     into the same or adjacent `ParallelFor`.
   - If it emits shared visualization data, split field math from debug output.
3. Convert remaining-time integration to `ParallelFor`.
   - Each worker updates only body `x`.
   - Sleep-island decisions remain serial until their dependencies are split.
4. Convert terrain detection to parallel candidate generation.
   - Workers write `TerrainContactCandidate[x]`.
   - Workers do not push into `m_terrainContactManifolds`.
   - The main thread commits candidates in increasing body index.
5. Keep `m_contactSolver.Solve(...)` serial after terrain candidate commit.
6. Keep physics diagnostics in serial commit order.

Acceptance criteria:

- `tools\validate_physics.bat` passes at the PR gate.
- Worker-enabled and worker-disabled fixed-step outputs are byte-identical.
- `tools\validate_perf.bat` shows neutral or improved frame cost for the
  benchmark scenes.

## Phase 3: Object Narrowphase Event Split

Goal: isolate current narrowphase side effects so the pair loop can be safely
partitioned.

Steps:

1. Define an `ObjectNarrowphaseEvent` record for:
   - swept object hit,
   - swept object miss,
   - wake decision,
   - collision visual contact,
   - collision cell key,
   - collision time diagnostic.
2. Refactor the current serial pair loop into:
   - read/query stage,
   - ordered commit stage.
3. Preserve the exact current candidate-pair order in serial mode.
4. Move `RecordPhysicsPipelineStage`, `EmitPhysicsCollisionTime`,
   `MarkCollisionVisualContact`, and `m_collisionCellKeys.push_back` into the
   ordered commit stage.
5. For position and `m_timeRemaining` updates, choose one of two policies:
   - keep them in serial commit order for first refactor,
   - or restrict parallel writes to bodies proven disjoint by islands.
6. Add a compile-time or config switch that can run the event-split path
   serially for easier bisecting.

Acceptance criteria:

- Serial event-split mode matches existing physics baselines exactly.
- Diagnostics and visual collision cells appear in the same order as before.
- The narrowphase pair loop has a clear partition boundary for island work.

## Phase 4: Island-Based Narrowphase Parallelization

Goal: parallelize object/object narrowphase across independent connected
components.

Steps:

1. Build union-find components from the post-prune candidate pair list.
2. Include awake/sleeping candidate edges so wake decisions cannot cross island
   boundaries after partitioning.
3. Sort islands by the minimum original candidate-pair index.
4. Within each island, run candidate pairs in the original serial order.
5. Assign whole islands to workers.
   - Bodies are disjoint across islands.
   - Pair-local diagnostics are chunk-local.
6. Commit island outputs in deterministic island order.
7. Keep single-island scenes on the serial path or accept no speedup for that
   frame.
8. If the persistent contact solver becomes the next bottleneck, repeat the
   same island partitioning for solver rows after proving row graph isolation.

Acceptance criteria:

- Spread-out scenes show measurable narrowphase speedup.
- Clustered single-island scenes do not regress materially.
- `tools\validate_physics.bat` passes.
- Fixed-step output is byte-identical across `worker_threads = 0`, `1`, and
  several multi-worker counts.

## Phase 5: Shadow-Map CPU Preparation

Goal: reduce shadow-map CPU cost without worker threads touching `Gfx()`.

Steps:

1. Split shadow rendering into two CPU phases:
   - build immutable shadow caster batches,
   - submit those batches to the renderer on the main thread.
2. Build object caster batches once per frame.
   - Sphere, box, and pine instance streams are independent of light view/proj.
   - Reuse the same instance streams for the terrain/object broad map and the
     tight object map.
3. Replace helper-owned static batch vectors with explicit batch payloads:
   - `ShadowCasterBatches::spheres`,
   - `ShadowCasterBatches::boxes`,
   - `ShadowCasterBatches::pines`.
4. Build those payloads with ordered parallel collection.
   - Workers scan disjoint model index ranges.
   - Workers write chunk-local sphere/box/pine streams.
   - Main thread concatenates chunks by increasing model range.
5. Parallelize object shadow bounds as an ordered reduction.
   - Each worker computes local min/max.
   - Main thread merges min/max in chunk order.
6. Keep terrain shadow depth rendering serial.
   - Terrain mesh submission is `Gfx()` work.
   - Terrain frame-data math may remain on the main thread unless profiling
     proves it matters.
7. Add main-thread submit helpers:
   - bind target,
   - bind shadow depth shader constants for the current shadow frame,
   - upload the prebuilt instance stream,
   - draw the instanced mesh.
8. Preserve current draw order:
   - terrain caster first for the terrain shadow map,
   - object spheres,
   - object boxes,
   - object pines,
   - tight object map object batches in the same shape order.

Acceptance criteria:

- `Frame/Shadows/ShadowMap/RenderMap/ObjectCasters/BuildBatches` drops
  materially in object-heavy scenes.
- Draw count and visual output stay equivalent.
- `tools\validate_dx12_renderer.bat` passes at the PR gate.
- If CPU prep is now cheap and upload/draw submission dominates, defer deeper
  renderer work to the stretch goal.

## Phase 6: Cross-Pipeline Scheduling

Goal: use workers without creating main-thread stalls or physics/render races.

Steps:

1. Keep the frame dependency order explicit:
   - input,
   - physics workers,
   - physics complete fence,
   - render frame data build,
   - shadow batch prep workers,
   - shadow prep complete fence,
   - main-thread render submission.
2. Do not allow render prep to read body transforms before physics completes.
3. Consider starting shadow batch prep immediately after physics completes and
   before non-shadow render passes.
4. Keep `worker_threads = 0` as the simplest debugging and validation path.
5. Expose profiler rows for:
   - worker wait time,
   - physics worker time,
   - shadow prep worker time,
   - main-thread merge time.

Acceptance criteria:

- Worker time is visible enough to diagnose contention.
- No frame uses transforms from two different physics ticks.
- Worker-enabled render output is stable across repeated screenshots.

## Stretch Goal: Asynchronous DX12 Command List Recording

Status: stretch only. Do not make this a prerequisite for the first worker
system implementation.

Goal: allow independent render passes, including shadow maps, to record command
lists concurrently once the backend can support it safely.

Prerequisites:

1. Render graph owns resource-state transitions for shadow targets, scene
   targets, backbuffer, and sampled shadow maps.
2. Each worker command recorder has:
   - its own command allocator,
   - its own command list,
   - its own upload page or upload allocation range,
   - its own transient descriptor range,
   - isolated state cache.
3. PSO, shader, descriptor, and mesh registries are either immutable during
   recording or protected by narrow, measured synchronization.
4. Pass inputs and outputs are immutable while worker command lists record.
5. The main thread closes and executes recorded command lists in deterministic
   render-graph order.

Candidate first async passes:

1. Terrain/object broad shadow map command list.
2. Tight object shadow map command list.
3. Later, other independent off-screen passes that do not consume each other's
   outputs.

Non-goals for the stretch slice:

- No worker thread should present.
- No worker thread should resize or recreate swap-chain resources.
- No worker thread should mutate global `Gfx()` state directly.

Acceptance criteria:

- DX12 InfoQueue reports zero errors.
- `tools\validate_dx12_renderer.bat` passes.
- Three consecutive DX12 renderer validations pass if upload allocator or
  descriptor lifetime changes.
- GPU captures show command lists in the intended pass order.

## Implementation Order Summary

1. Baseline profiler report.
2. Worker pool, config, ordered chunk merge, worker timing.
3. Parallel per-body physics stages.
4. Serial event-split narrowphase refactor.
5. Island-based narrowphase parallelization.
6. Shadow caster batch payload refactor.
7. Parallel shadow batch build and object bounds reduction.
8. Cross-pipeline scheduling and profiler polish.
9. Stretch only: async DX12 command-list recording.

## Success Criteria

1. Physics fixed-step output is deterministic across worker counts.
2. Narrowphase worker dispatch is enabled, controllable, profiler-visible, and
   documented with measured cost even when a current scene regresses.
3. Shadow CPU prep worker dispatch is enabled, controllable, profiler-visible,
   and documented with measured cost even when a current scene regresses.
4. Main-thread renderer submission remains correct and validation-clean.
5. The worker system's first production jobs are the physics and shadow-prep
   jobs described here.
6. Async command-list recording remains isolated as a later renderer milestone.
