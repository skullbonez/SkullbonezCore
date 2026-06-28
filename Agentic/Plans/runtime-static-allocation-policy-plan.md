# Runtime Static Allocation Policy Plan

Date: 2026-06-27
Status: Draft
Impact area: performance, runtime, physics, renderer, UI/diagnostics, tooling
Validation note: plan-only edits require no validation. Implementation touches
hot runtime paths, so PR-bound work should use `tools\validate_perf.bat` plus
the area-specific gates listed below.

## Goal

Make ordinary gameplay allocation-stable and bring every runtime dynamic growth
path, including replay, under one explicitly registered reserve allocator.

Target policy:

```text
No unregistered dynamic allocation during gameplay or replay runtime phases.
No per-frame dynamic allocation.
Runtime growth is allowed only through RuntimeReserveAllocator.
Emergency overflow should be rare: once or twice across an entire run, not every frame.
```

The preferred end state is static or preallocated storage sized at startup or
scene load from known capacity limits. Systems that cannot honestly guarantee
that must carry explicit comments next to the owning storage and must report
their emergency growth. Replay is not exempt from this rule: replay should have
enough working memory for expected capture, prediction, restore, and scrub
work, and any occasional bump must flow through the same reserve allocator.

## Carmack-Test Option 3 Checklist

This checklist turns the Carmack-test performance finding into markable work.
The problem statement is: performance cannot score above a conservative systems
bar until steady gameplay and replay allocation behavior are measured, enforced,
and reviewable.

This section extends the existing dynamic memory allocation plan rather than
creating a separate option 3 plan. Treat the sections below as the implementation
source of truth, and use this checklist as the Carmack-test acceptance overlay.

### Evidence Baseline

- [x] Capture current `tools\validate_perf.bat` output and record whether it is
  clean, warning-bearing, or machine-label-limited.
- [x] Record current `dx12_perf.json` and `physics_bench_perf.json` baseline
  metadata, including machine label and commit.
- [x] Inventory all allocation-capable runtime paths listed in
  `Current Findings To Address`.
- [x] Add a small table mapping each runtime owner to its expected allocation
  phase: startup, scene load, backend init, steady gameplay, replay, capture, or
  shutdown.
- [x] Identify at least one representative non-replay gameplay launch and one
  replay-enabled launch for allocation guard proof.

### Implementation Details

- [ ] Implement phase-aware allocation tracking with fixed, non-allocating
  tracker storage and a reentrancy guard.
- [ ] Add runtime phase transitions for startup, scene load/reset,
  backend/resource init, steady gameplay, replay, screenshot/capture, and
  shutdown.
- [ ] Implement `RuntimeReserveAllocator` owner registration with owner name,
  phase, capacity source, hard cap, emergency bump cap, and diagnostic counters.
- [ ] Convert runtime growable owners to fixed storage, preallocated storage, or
  registered reserve bumps.
- [ ] Add policy comments beside every runtime growable storage owner.
- [ ] Make ordinary steady gameplay fail on nonzero unregistered allocations.
- [ ] Make replay scenarios fail on unregistered replay allocations while still
  allowing registered replay-phase reserve bumps within cap.
- [ ] Print owner-level allocation summaries with allocation count, bytes,
  high-water, emergency bump count, frame, and phase.
- [ ] Ensure the allocation tracker itself cannot allocate while reporting.

### Guardrails And Validation Integration

- [ ] Add a static checker for banned runtime dynamic allocation patterns.
- [ ] Add an allowlist format that requires owner, phase, reason, cap, and
  removal or wrapper plan.
- [ ] Add the allocation guard launch to the appropriate validation script after
  the first implementation slice lands.
- [ ] Make `tools\validate_perf.bat` output distinguish clean perf evidence from
  warning-bearing evidence that still exits 0.
- [ ] Add synthetic checker tests for rejected direct allocation, allowed
  startup allocation, and allowed registered reserve bumps.

### Independent Review And Handoff

- [ ] Ask a rubber-duck reviewer to inspect whether the guard can falsely pass
  when allocations happen in steady gameplay.
- [ ] Ask the reviewer to inspect recursion, thread-local tracking, WorkerPool
  dispatch, replay phases, screenshot/capture opt-outs, and DX12 telemetry.
- [ ] Record any accepted perf warnings with marker-level evidence, not just
  script exit code.
- [ ] Quote allocation guard output in the handoff.
- [ ] Do not mark the Carmack-test performance issue resolved until the guard
  and static checker are both active in validation.

### 2026-06-28 Evidence Baseline

`tools\validate_perf.bat` was captured at commit `cadf6366` in the local,
ignored log
`TestOutput\validation\agent_logs\allocation_policy_validate_perf_baseline.log`.
The durable evidence is copied here because that log is not a tracked artifact.
The command exited 0 and ended with `VALIDATE_PERF: COMPLETE`, but this is
warning-bearing evidence, not a clean performance proof:

- Profile and Debug builds both reported `0 Warning(s)` and `0 Error(s)`.
- DX12 comparison was machine-label-limited: the committed baseline is from
  `AMD Ryzen Threadripper 3970X 32-Core Processor` on Windows `10.0.22631`,
  while the current run is `AMD64 Family 23 Model 49 Stepping 0,
  AuthenticAMD` on Windows `10.0.26200`, so the script skipped the regression
  check.
- `physics_bench` ran on the same machine label as its committed baseline, but
  reported `PERF REGRESSION - 9 failure(s) [PHYSICS_BENCH]`: `Frame.avg`
  `+50.7%`, `Frame.p50` `+63.9%`, `Frame/Render.avg` `+366.9%`,
  `Frame/Render.p50` `+385.9%`, `Frame/VsyncWait.avg` `+138.3%`,
  `Frame/VsyncWait.p50` `+92.7%`, `mem_start` `+11.80 MB`, `mem_restart`
  `+71.88 MB`, and `mem_end` `+71.88 MB`.

| Artifact | Commit | Machine label | Frames | Frame avg/p50/p99/p99.9 ms | Memory start/restart/end MB |
|----------|--------|---------------|--------|-----------------------------|-----------------------------|
| `TestOutput\baselines\dx12_perf.json` | `fba8600` | `AMD Ryzen Threadripper 3970X 32-Core Processor`, Windows `10.0.22631` | 1970 | 0.8482 / 0.7320 / 3.3043 / 4.0644 | 84.42 / 141.54 / 141.54 |
| `Profile\dx12_perf.json` | `cadf6366` | `AMD64 Family 23 Model 49 Stepping 0, AuthenticAMD`, Windows `10.0.26200` | 1970 | 1.9456 / 1.8742 / 2.6548 / 3.2046 | 83.95 / 151.99 / 151.99 |
| `TestOutput\baselines\physics_bench_perf.json` | `14795e0` | `AMD64 Family 23 Model 49 Stepping 0, AuthenticAMD`, Windows `10.0.26200` | 2370 | 0.4050 / 0.3509 / 1.1667 / 2.3573 | 72.20 / 78.07 / 78.07 |
| `Profile\physics_bench_perf.json` | `cadf6366` | `AMD64 Family 23 Model 49 Stepping 0, AuthenticAMD`, Windows `10.0.26200` | 2370 | 0.6103 / 0.5750 / 1.0637 / 1.9022 | 84.00 / 149.95 / 149.95 |

Representative allocation-guard launch candidates:

| Role | Launch target | Why this target |
|------|---------------|-----------------|
| Non-replay gameplay/perf stress | `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\perf_1000.scene.json --frames 600 --fixed-step --vsync off` | Fixed-step, vsync-off scene with 1000 solver balls and an existing perf log path; future allocation-guard work should add the guard flag once implemented. |
| Replay-enabled interaction/prediction | Existing launch from `tools\validate_interaction_clicks.bat`: `Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\interaction_replay_prediction_harness.scene.json --interaction-script SkullbonezData\interaction\replay_prediction_click.json --interaction-report TestOutput\interaction\replay_prediction_click_report.json --frames 150 --replay on --replay-seconds 2 --fixed-step --vsync off` | Scene/suite automation leaves replay off unless the command line opts in; this target explicitly enables replay and drives the replay prediction interaction path. |

Rubber-duck review: Herschel initially found one blocking issue: the proof
target claimed a replay-enabled scene while naming a scene without replay
command-line opt-in. This plan now names replay-enabled launches instead of
bare scenes, points to the existing interaction replay command, and calls out
that the perf log is local/ignored. Follow-up review found no blockers.

## Definitions

| Term | Meaning |
|------|---------|
| Steady gameplay frame | A frame after startup/scene load/resource warmup, while a scene is running, replay capture/export/restore is not doing work, and no explicit screenshot/backbuffer capture is requested. |
| Runtime allocation | CPU heap allocation through `new`, `malloc`, STL container growth, `std::function`, `std::string`, `std::shared_ptr`, stream buffers, or similar allocator-backed paths. |
| Static allocation | Fixed arrays, fixed rings, object members, preallocated arenas, or containers whose capacity is fully established before steady gameplay begins. |
| High-water allocation | Growth to a known capacity during startup, scene load, backend init, or explicit warmup, followed by reuse without further heap growth. |
| Emergency overflow | A bounded, logged, commented growth path used only when a scene exceeds a documented best-guess high-water mark. |
| RuntimeReserveAllocator | The single runtime path allowed to perform bounded dynamic reserve bumps for registered mostly-static memory owners. |
| Mostly-static memory | Runtime storage that is preallocated for the expected high-water mark and may grow only through a bounded `RuntimeReserveAllocator` bump. |
| Replay system | Storage and tools under replay capture, scrub, restore, prediction, artifact load/save, and replay diagnostics. Replay memory is allowed larger prediction/model-count reserves, but still must register and grow only through `RuntimeReserveAllocator`. |

## Current Findings To Address

The recent audit found allocation-capable runtime paths:

| Area | Current risk |
|------|--------------|
| Worker pool | Default parallel physics can allocate chunks, shared task state, task closures, and queue nodes during `ParallelFor`. |
| Physics | Candidate pairs, collision keys, terrain manifolds, contact rows, debug contacts, sleep/island scratch, and optional narrowphase island data can grow during live physics. |
| DX12 telemetry | Live barrier telemetry appends into vectors during frame/present paths. |
| Render graph diagnostics | Frame-graph dump/dry-run code uses `std::string`, `std::ostringstream`, and vectors when graph shape is rebuilt or diagnostics run. |
| Shadow prep | Active shadow caster batches are persistent, but still need explicit capacity policy and high-water reservation. Direct helper paths can allocate fresh if used. |
| UI/capture | Backdrop blur and screenshot/readback paths allocate CPU image buffers when invoked. |
| Runtime command queue | `std::deque<RuntimeCommand>` and `std::string` can allocate for UI/input commands. |
| Tornado visual/debug | Visual vortex and vertex vectors mostly reuse capacity, but need explicit warmup and overflow comments. |
| Replay working sets | Replay capture, prediction, scrub, restore, and artifact load/save should have prediction/model-count reserves, with any occasional bump routed through `RuntimeReserveAllocator`. |

### Runtime Owner Phase Map

| Runtime owner | Expected allocation phase | Steady/replay policy |
|---------------|---------------------------|----------------------|
| Worker pool task storage | Startup for worker thread/task infrastructure; scene load or warmup for fixed task buffers if capacity depends on scene size. | `ParallelFor` must not allocate chunks, closures, shared task state, or queue nodes during steady gameplay. |
| Physics candidate/contact/island scratch | Scene load/reset after active body, collider, and terrain capacity are known. | Collision, solver, sleep, and island storage must reuse preallocated capacity or request a bounded registered bump; replay follows the same rule with replay-sized reserves. |
| DX12 barrier/live-object telemetry | Backend init or explicit diagnostics startup. | Frame/present telemetry must append into fixed storage or a registered telemetry reserve; formatting/reporting is diagnostics, not steady gameplay. |
| Render graph diagnostics | Backend/resource init for graph shape; explicit diagnostics/capture phase for dumps. | Dry-run dumps, strings, and ostreams stay out of steady gameplay unless the allocation guard labels the frame as diagnostics/capture. |
| Shadow caster batches | Scene load/render warmup from active render-instance capacity. | Shadow prep reuses persistent batch storage; overflow must be bounded and reported by owner. |
| UI and screenshot/readback buffers | Startup/scene load for UI caches; capture phase for screenshot and readback CPU buffers. | Ordinary UI frame work should reuse caches; screenshot/readback allocation is allowed only under an explicit capture phase. |
| Runtime command queue | Startup as a fixed ring or preallocated queue sized from expected command pressure. | Input/UI commands may enqueue during steady gameplay without heap growth; string payloads need fixed storage or registered reserve bumps. |
| Tornado visual/debug storage | Scene load or runtime warmup when tornado/debug features are enabled. | Visual/debug vectors must reuse capacity during steady gameplay; optional debug growth needs a documented diagnostics/capture phase or registered owner. |
| Replay working sets | Replay phase setup from model count, prediction horizon, scrub window, and artifact metadata. | Replay capture, restore, prediction, path/cause rows, and artifact load/save can grow only through replay-phase registered reserves within cap. |

## Allocation Policy

### Default Rule

Every system on the steady gameplay path must satisfy one of these contracts:

1. Fixed storage: compile-time bounded arrays or fixed rings.
2. Preallocated storage: capacity is reserved before steady gameplay begins and
   never grows during steady gameplay.
3. Explicit emergency overflow through `RuntimeReserveAllocator`: growth is
   registered, bounded, logged, counted, commented, and expected to happen at
   most once or twice per run outside pathological scenes.

No system should rely on "vector probably already has enough capacity" as the
policy. The capacity owner must state when capacity is established and what
happens if it is exceeded.

No owner may call `new`, `malloc`, STL reserve/growth, `std::make_unique`,
`std::make_shared`, or equivalent runtime heap paths directly during gameplay.
If the storage can grow at runtime, that growth must be expressed as a
registered mostly-static reserve bump through `RuntimeReserveAllocator`.

### Repository Policy Update

Add a follow-up `AGENTS.md` rule after the implementation design lands:

```text
Runtime/gameplay dynamic allocation is banned by default. Any runtime storage
that can grow after startup, scene load, or backend init must be registered as a
mostly-static owner and may grow only through RuntimeReserveAllocator. Direct
STL container growth, heap allocation, std::function task allocation, and
string/stream growth in steady gameplay paths are review and lint failures.
Replay is covered by the same policy.
```

This is intentionally planned as an `AGENTS.md` update, not just an
implementation detail, so future agents treat dynamic allocation as forbidden
unless the allocator policy explicitly allows it.

## RuntimeReserveAllocator Spec

`RuntimeReserveAllocator` is the single choke point for all runtime reserve
bumps. It owns the "mostly static" memory contract: storage is pre-sized from a
capacity policy, then any rare overflow is requested, counted, logged, and
bounded through this allocator.

### Responsibilities

1. Register every runtime-growable owner before steady gameplay begins.
2. Store each owner's phase, subsystem, capacity source, initial reserve,
   hard cap, emergency growth allowance, and current growth count.
3. Provide the only API that can request runtime heap-backed growth for
   gameplay or replay memory.
4. Reject unregistered owners.
5. Reject growth from disallowed phases unless the owner explicitly allows that
   phase.
6. Reject growth after the owner's per-run emergency allowance is exhausted.
7. Log every growth with owner id, subsystem, phase, frame number, old capacity,
   requested capacity, granted capacity, bytes, growth count, and hard cap.
8. Feed allocation guard diagnostics and profiler/event markers.
9. Expose compact stats for memory diagnostics and validation logs.

### Suggested Types

```cpp
enum class RuntimeReservePhase
{
    Startup,
    SceneLoad,
    BackendInit,
    SteadyGameplay,
    Replay,
    Capture,
    Shutdown
};

enum class RuntimeReserveSubsystem
{
    Physics,
    WorkerPool,
    Renderer,
    DX12Telemetry,
    UI,
    RuntimeCommands,
    Replay,
    Diagnostics
};

struct RuntimeReserveOwnerDesc
{
    const char* ownerName;
    RuntimeReserveSubsystem subsystem;
    RuntimeReservePhase initPhase;
    int initialCapacity;
    int hardCapacity;
    int emergencyGrowthLimit;
    bool allowSteadyGameplayGrowth;
    bool allowReplayGrowth;
    const char* capacityReason;
};

struct RuntimeReserveGrowthRequest
{
    const char* ownerName;
    RuntimeReservePhase phase;
    int frameNumber;
    int oldCapacity;
    int requestedCapacity;
    int elementSizeBytes;
};

struct RuntimeReserveGrowthResult
{
    bool granted;
    int grantedCapacity;
    int growthCount;
};
```

The concrete API can evolve, but the policy should stay narrow:

```cpp
class RuntimeReserveAllocator
{
public:
    RuntimeReserveOwnerHandle RegisterOwner(const RuntimeReserveOwnerDesc& desc);
    RuntimeReserveGrowthResult RequestGrowth(
        RuntimeReserveOwnerHandle owner,
        const RuntimeReserveGrowthRequest& request);
    RuntimeReserveStats CollectStats() const;
};
```

### Container Integration

Runtime containers should not call `std::vector::reserve` directly from
gameplay code. Use narrow wrappers that require a registered owner handle:

```cpp
template <typename T>
class RuntimeReserveVector
{
public:
    void Preallocate(RuntimeReserveAllocator& allocator,
                    RuntimeReserveOwnerHandle owner,
                    int capacity);
    bool EnsureCapacity(RuntimeReserveAllocator& allocator,
                        RuntimeReserveOwnerHandle owner,
                        int requiredCount,
                        RuntimeReservePhase phase,
                        int frameNumber);
};
```

Preferred wrappers:

| Wrapper | Use |
|---------|-----|
| `FixedVector<T, N>` | Compile-time bounded storage. |
| `FixedRing<T, N>` | Compile-time bounded queues. |
| `RuntimeReserveVector<T>` | Mostly-static vector with registered reserve bumps. |
| `RuntimeReserveRing<T>` | Mostly-static queue/ring with registered reserve bumps. |
| `FixedString<N>` | Frame-path text and command payloads. |
| `RuntimeReserveString` | Rarely growable runtime text through the allocator only. |

Direct `std::vector`, `std::deque`, `std::string`, `std::ostringstream`,
`std::function`, `std::unordered_map`, and heap-owning smart-pointer creation
should be treated as banned in gameplay/replay source unless the code is inside
the allocator implementation, a wrapper implementation, startup-only tooling,
or an explicitly non-runtime tool path.

### Replay Policy

Replay registers as normal mostly-static memory. Its initial reserves should be
larger and derived from prediction size, model count, frame count, branch/event
budgets, and loaded artifact metadata when available.

Replay-specific rules:

1. Replay capture, prediction, scrub, restore, artifact load/save, and replay
   diagnostics each register owners under `RuntimeReserveSubsystem::Replay`.
2. Prediction reserves are sized from expected prediction frame count multiplied
   by active replay/model capacity.
3. Loaded artifacts may request larger reserves during artifact load before
   steady replay interaction begins.
4. Replay can allow bounded reserve bumps in `RuntimeReservePhase::Replay`, but
   the bump still goes through `RuntimeReserveAllocator`.
5. Replay reserve bumps must not be hidden as generic heap allocations.
6. Replay diagnostics report capacity, high-water, and bump count separately
   from ordinary gameplay owners.

Replay should normally run with zero bumps. Occasional bumps are acceptable when
prediction horizon, branch count, or model count exceeds the best-guess reserve,
but those bumps must be visible and bounded.

### Required Comment Shape

Every runtime-owned container that can grow after startup must carry a comment
near the member declaration or growth helper:

```cpp
// Runtime allocation policy:
//   Preallocated at <startup|scene load|backend init> for <capacity source>.
//   Steady gameplay must not grow this buffer.
//   Emergency overflow: <not allowed|RuntimeReserveAllocator owner TAG, max N bumps>.
```

For containers that must retain emergency growth:

```cpp
// Runtime allocation policy:
//   Best-guess reserve is <X> entries from <reason>.
//   Registered mostly-static owner: <RuntimeReserveAllocator owner TAG>.
//   If exceeded, this buffer may grow at most <N> times per run through the allocator.
//   Each growth logs <tag> and increments allocation guard counters.
```

This is intentionally a little loud. Future code review should see the policy
before seeing `push_back`.

### Emergency Overflow Rules

Emergency growth is acceptable only when all are true:

1. There is a documented best-guess reserve.
2. The owner is registered with `RuntimeReserveAllocator`.
3. The growth request flows through `RuntimeReserveAllocator::RequestGrowth`.
4. There is a hard cap or a fatal/assert path after the emergency allowance.
5. Growth count is tracked by owner and visible in logs or memory diagnostics.
6. The growth emits a profiler/event marker with owner, old capacity, new
   capacity, requested count, and frame number.
7. The steady-frame allocation guard can fail tests if the same owner grows
   every frame.

Suggested default:

```text
emergency_growth_limit_per_owner_per_run = 2
```

If an owner needs more than that, the owner is not actually emergency-only and
should be redesigned around a fixed arena or a larger scene-load reserve.

## Phase 1: Add A Gameplay Allocation Guard

Goal: prove allocation behavior with runtime evidence, not static inspection
alone.

Tasks:

1. Add a lightweight allocation tracker for Profile/Debug builds.
2. Track allocation count and bytes by runtime phase:
   - startup,
   - scene load/reset,
   - backend/resource init,
   - steady gameplay,
   - replay,
   - screenshot/capture,
   - shutdown.
3. Mark steady gameplay only after scene load and warmup are complete.
4. Add scoped opt-outs for replay and explicit capture paths so the non-replay
   policy remains clear. These opt-outs label phases only; replay reserve bumps
   still go through `RuntimeReserveAllocator`.
5. Add an automated launch mode such as:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\perf_1000.scene.json --frames 600 --allocation-guard gameplay
```

6. Fail the guard if steady gameplay allocations exceed zero, unless the owner
   is registered with `RuntimeReserveAllocator` and within its per-run cap.
7. Print a compact summary:

```text
allocation_guard phase=steady_gameplay allocations=0 bytes=0 emergency_grows=0
```

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Replay disabled steady scene | Reports zero non-replay steady allocations. |
| Replay enabled | Reports zero unregistered replay allocations, and all reserve bumps go through `RuntimeReserveAllocator`. |
| Screenshot/capture | Capture allocation is labeled capture-only. |
| Emergency growth | Logs owner, old/new capacity, frame, and count. |

## Phase 2: Preallocate Physics To Active Capacity

Goal: make fixed-step gameplay physics allocation-stable.

Tasks:

1. Add a single `PhysicsWorld::ReserveRuntimeCapacity(int modelCapacity)` entry
   point called during scene load/reset after active model capacity is known.
2. Reserve every per-model physics vector to the active capacity, including any
   currently missing reserves such as sleep state/counter and collision keys.
3. Reserve candidate-pair and contact storage from an explicit policy:

```text
candidate_pair_reserve = min(modelCapacity * pair_factor, hard_pair_cap)
contact_row_reserve = candidate_pair_reserve * max_contact_points_per_pair + terrain_row_reserve
```

4. Decide pair/contact hard caps per scene capacity. If a scene exceeds them,
   prefer a deterministic overflow path over silent container growth.
5. Replace ad hoc `reserve()` calls inside the solver with checks against the
   already-established capacity.
6. Flatten optional object narrowphase island storage:
   - replace nested `std::vector<int> pairIndices` per island with one
     preallocated pair-index array plus `start/count` ranges,
   - reserve island records at scene load.
7. Make terrain detection candidates and terrain manifolds scene-capacity
   owned.
8. Ensure physics debug contacts and pipeline trace have fixed caps and do not
   grow once the cap is reached.
9. Add required allocation-policy comments to every physics container that can
   still grow.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Core physics scene | Zero non-replay steady allocations after warmup. |
| 1000-object perf scene | Zero non-replay steady allocations after warmup. |
| Pair/contact overflow | Deterministic logged overflow, not recurring frame growth. |
| Physics determinism | Byte-exact physics baseline still passes. |

## Phase 3: Remove WorkerPool Per-Frame Heap Use

Goal: keep parallel physics enabled without task/chunk allocation every frame.

Tasks:

1. Replace per-call `std::vector<WorkerChunkRange>` allocation with fixed worker
   chunk storage owned by `WorkerPool`.
2. Replace `std::shared_ptr<ParallelForChunksState>` per dispatch with
   preallocated dispatch state slots.
3. Replace `std::function` task queues with fixed task records where possible.
   If type-erasure remains, use a fixed-capacity callable wrapper.
4. Replace `std::deque<Task>` with a fixed ring sized from worker count and max
   in-flight chunks.
5. Add a per-frame worker dispatch scratch reset after all jobs complete.
6. Keep worker-disabled and inline paths allocation-free.
7. Comment all remaining worker-owned growable storage with the runtime
   allocation policy.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| `physics_parallel_apply_forces=1` | No task/chunk heap allocations in steady gameplay. |
| Terrain detect parallel path | No task/chunk heap allocations in steady gameplay. |
| Integrate parallel path | No task/chunk heap allocations in steady gameplay. |
| Worker count changes | Capacity is rebuilt outside steady gameplay or rejected with a clear message. |

## Phase 4: Make Renderer And DX12 Telemetry Allocation-Stable

Goal: keep normal rendering and validation telemetry from growing the heap while
frames run.

Tasks:

1. Reserve DX12 live barrier and UAV barrier telemetry at backend init to their
   bounded maximum, or convert them to fixed arrays/rings.
2. Clear live barrier telemetry per frame if it is frame telemetry. If it is
   run telemetry, document that explicitly and preallocate the full run cap.
3. Move render-graph skeleton/dump string construction out of steady gameplay:
   - backend init,
   - shutdown,
   - validation-only command,
   - or fixed-buffer writer.
4. Ensure `DumpExecutedFrameGraphIfChanged` does not allocate during ordinary
   steady frames. Prefer a cached/fixed graph report and only rebuild it during
   state transitions outside the guarded phase.
5. Pre-reserve shadow caster batches from active model capacity during scene
   load or render warmup.
6. Retire or rewrite direct shadow helper paths that allocate stack-local batch
   vectors every call, unless they are proven unused and guarded.
7. Ensure transient render helper instance buffers are preallocated once and
   never grow during frame submission.
8. Add allocation-policy comments near all render-frame buffers.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Ordinary DX12 gameplay | Zero non-replay render allocations after warmup. |
| Shadow-heavy scene | Shadow caster batches reuse preallocated capacity. |
| DX12 telemetry | Barrier records do not trigger runtime heap growth. |
| Frame graph diagnostics | Diagnostics either run outside steady gameplay or use fixed storage. |

## Phase 5: Bound UI, Capture, And Runtime Commands

Goal: keep ordinary UI/input frames allocation-free while still allowing
explicit capture actions.

Tasks:

1. Replace `RuntimeCommandQueue` with a fixed ring.
2. Replace command `std::string` payloads with fixed-size char buffers or stable
   identifiers. Longer payloads should be rejected or routed through a
   non-steady scene/action path.
3. Preallocate UI draw/list/text buffers used every frame.
4. Mark screenshot and backdrop blur refresh as explicit capture phases, not
   steady gameplay. If backdrop blur is expected during ordinary UI movement,
   preallocate readback/result/source buffers and reuse them.
5. Add comments to any UI buffer that may emergency-grow.
6. Ensure in-game text formatting uses stack/fixed buffers on frame paths.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Normal UI open/closed | Zero non-replay steady allocations. |
| Runtime command input | Queue push/pop does not allocate. |
| Screenshot | Allocation is scoped to capture and reported separately. |
| Backdrop blur | Either preallocated or clearly capture/refresh-scoped. |

## Phase 6: Standardize Capacity Owners

Goal: remove duplicated capacity guesses and make reserves easy to audit.

Tasks:

1. Add central capacity policy structs, for example:

```cpp
struct RuntimeCapacityPolicy
{
    int modelCapacity;
    int physicsCandidatePairCapacity;
    int physicsContactRowCapacity;
    int shadowCasterCapacity;
    int workerChunkCapacity;
    int runtimeCommandCapacity;
};
```

2. Build the policy once from:
   - `ActiveGameModelCapacity()`,
   - scene-authored object count,
   - config,
   - known hard caps.
3. Pass the resolved policy into physics, renderer, worker, and UI owners at
   scene load or backend init.
4. Add `CollectRuntimeCapacityStats()` so memory/profiler diagnostics can show:
   - capacity,
   - high-water size,
   - emergency growth count,
   - overflow/drop count.
5. Add a concise diagnostics dump section:

```json
"runtime_capacity": {
  "physics_candidate_pairs": { "capacity": 16384, "high_water": 9210, "emergency_grows": 0 },
  "worker_tasks": { "capacity": 64, "high_water": 14, "emergency_grows": 0 }
}
```

Acceptance:

| Check | Expected result |
|-------|-----------------|
| Capacity audit | One place explains the current scene/run capacity assumptions. |
| Memory diagnostics | Shows capacity and high-water by owner. |
| Review | New runtime vectors without policy comments are easy to reject. |

## Phase 7: Add Guardrails To Tools And Reviews

Goal: stop regression to casual per-frame allocation.

Tasks:

1. Add a static checker that rejects dynamic memory types in gameplay/replay
   source unless they are inside approved wrappers, allocator implementation,
   startup-only code, scene-load code, backend-init code, or offline tooling.
2. Rejected patterns include:
   - local or member `std::vector`, `std::deque`, `std::string`,
     `std::ostringstream`, `std::unordered_map`, `std::map`, and
     `std::function` in runtime hot paths,
   - direct `new`, `delete`, `malloc`, `free`, `calloc`, `realloc`,
     `std::make_unique`, `std::make_shared`,
   - direct `reserve`, `resize`, `push_back`, `emplace_back`, `insert`, or
     map/set insertion on unapproved dynamic containers,
   - task closures or type-erased callables in worker dispatch,
   - any growable storage without a nearby allocation-policy comment and
     registered `RuntimeReserveAllocator` owner.
3. Treat new violations as lint failures, not warnings. Existing violations may
   be tracked by a temporary baseline only while the cleanup phases are in
   progress.
4. Add a static allowlist format that requires owner, phase, reason, and
   planned removal or allocator wrapper. Empty "because legacy" allowlist
   entries are not acceptable.
5. Add an allocation-policy checklist to code review docs:

```text
Does this runtime container reserve before steady gameplay?
Can this path allocate every frame?
If it can grow, where is the RuntimeReserveAllocator owner, policy comment, and emergency counter?
Is this a banned dynamic type that lint should reject?
```

6. Add a small allocation guard scenario to perf validation after the first
   implementation slice lands.

Acceptance:

| Check | Expected result |
|-------|-----------------|
| New per-frame vector | Tool rejects it. |
| New runtime `std::vector` or other dynamic type | Tool rejects it unless it is an approved wrapper or allocator implementation. |
| Missing policy comment | Tool rejects runtime-owned growable storage. |
| Allocation guard scene | Fails on non-replay steady allocations. |

## Implementation Order

1. Add the `AGENTS.md` blanket dynamic-allocation ban and point exceptions to
   `RuntimeReserveAllocator`.
2. Add `RuntimeReserveAllocator`, owner registration, and mostly-static wrapper
   types.
3. Add allocation guard and phase markers.
4. Add capacity policy comments and allocator owner registrations to current
   known allocation surfaces.
5. Bring replay memory owners into the allocator path.
6. Fix WorkerPool dispatch allocations.
7. Preallocate physics and flatten optional narrowphase islands.
8. Stabilize DX12 telemetry and render graph diagnostics.
9. Preallocate shadow/render buffers.
10. Convert runtime command queue and ordinary UI frame buffers.
11. Add central capacity policy and diagnostics.
12. Turn dynamic-type lint rejection and allocation guardrails into validation
    gates.

This order front-loads measurement. The guard should make every later phase
obvious: run scene, see which owner still allocates, fix that owner.

## Validation Strategy

Plan-only changes require no validation.

For PR-bound implementation:

| Change | Required PR gate |
|--------|------------------|
| Allocation guard/tooling only | `tools\validate_fast.bat`, then the new allocation guard launch. |
| WorkerPool dispatch storage | `tools\validate_perf.bat` and `tools\validate_full.bat`. |
| Physics storage/capacity changes | `tools\validate_physics.bat` and `tools\validate_perf.bat`. |
| Renderer/DX12 telemetry or shadow buffers | `tools\validate_dx12_renderer.bat` and `tools\validate_perf.bat`. |
| UI command/draw buffer changes | `tools\validate_ui.bat` if UI layout changes, plus `tools\validate_full.bat` for broad runtime input. |
| Mixed or uncertain scope | `tools\validate_full.bat` plus the allocation guard launch. |

Allocation guard launch candidates:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\perf_1000.scene.json --frames 600 --vsync off --allocation-guard gameplay
Profile\SKULLBONEZ_CORE.exe --fixed-step --scene SkullbonezData\scenes\water_ball_test.scene.json --frames 600 --vsync off --allocation-guard gameplay
```

The guard should print enough detail to quote in handoffs:

```text
allocation_guard result=pass phase=steady_gameplay allocations=0 bytes=0 emergency_grows=0
allocation_guard high_water owner=physics_candidate_pairs capacity=16384 high_water=9210
```

## Risks And Mitigations

| Risk | Mitigation |
|------|------------|
| Over-reserving wastes memory | Report capacity/high-water in diagnostics and tune policy from real scenes. |
| Fixed caps drop simulation data | Make overflow explicit, deterministic, and validation-visible before changing behavior. |
| Allocator becomes a loophole for casual runtime allocation | Require owner registration, hard caps, per-run bump limits, phase checks, lint rejection, and code-review comments. |
| Allocation tracker recurses | Use thread-local reentrancy guards and fixed logging buffers. |
| WorkerPool fixed task ring is too small | Size from worker count and max chunks; overflow fails loudly outside emergency policy. |
| Render graph diagnostics lose useful text | Move rich text generation to init/shutdown/validation commands, not steady frames. |
| UI/capture exceptions become confusing | Phase labels must distinguish ordinary UI from explicit capture/readback actions. |
| Replay gets under-reserved | Size replay working memory from prediction horizon, model count, branch/event budgets, and artifact metadata; allow bounded replay-phase reserve bumps only through `RuntimeReserveAllocator`. |

## Definition Of Done

This work is complete when:

1. A Profile allocation guard can run representative gameplay scenes with replay
   off and report zero non-replay steady gameplay allocations.
2. A replay allocation guard scenario reports zero unregistered replay
   allocations and bounded reserve bumps through `RuntimeReserveAllocator`.
3. No known runtime path allocates every frame, including replay paths.
4. Any runtime growable storage has an allocation-policy comment.
5. Any runtime growable storage is registered with `RuntimeReserveAllocator`.
6. Emergency growth is bounded, logged, counted, and visible in diagnostics.
7. Emergency growth does not recur every frame and defaults to no more than two
   growths per owner per run.
8. Physics, worker, renderer, UI, replay, and command-queue hot paths are either
   fixed storage or preallocated from central capacity policy.
9. Runtime capacity diagnostics show capacity, high-water, and emergency growth
   counts by owner.
10. Static guardrails reject new dynamic memory types and unregistered growth in
   gameplay/replay source before review misses them.
11. `AGENTS.md` carries the blanket dynamic-allocation ban and points any
    runtime growth exception to `RuntimeReserveAllocator`.
12. Required validation gates pass for the implementation phases that land.
