# Worker System & Physics Parallelization Plan

## Problem Statement

SkullbonezCore is entirely single-threaded. With `MAX_GAME_MODELS = 512` and the physics solver running in `RunSolverPhysics`, the physics tick is the primary bottleneck preventing scaling to more objects. The engine needs a lightweight, general-purpose worker system that can parallelize embarrassingly-parallel workloads (physics integration, force application) and eventually enable island-decomposed narrowphase collision.

## Design Decisions (Confirmed)

| Decision | Choice |
|----------|--------|
| Thread count | User-specified via engine.cfg / CLI (`worker_threads = N`); default = `hardware_concurrency() - 1` |
| Thread model | Main thread + N worker threads (total = N+1 active) |
| Primitives | C++ standard library only (std::thread, std::mutex, std::condition_variable) |
| Amortized mode | Tasks can be chunked across multiple frames (progress state persists) |
| Deadlock detection | Lock-order directed graph with runtime validation in Debug builds only |
| Physics scope | Phase 1: parallel embarrassingly-parallel stages + Phase 2: island decomposition |
| Renderer coupling | Workers are decoupled from renderer — physics workers never touch GPU state |

---

## Architecture Overview

### Thread Model (How ParallelFor Actually Works)

The system creates a **fixed pool of N worker threads at startup** (e.g., 7 threads on an 8-core machine). These threads live for the entire application lifetime — they sleep on a condition variable when idle, wake to process assigned work, then go back to sleep. **No threads are created or destroyed per frame.**

When `ParallelFor(0, 512, fn)` is called with 4 workers:
```
Main thread:  "Here's 512 items. Worker 0 take [0,128), Worker 1 take [128,256), etc."
              → wakes 4 sleeping workers via condvar
              → blocks until all 4 signal completion

Worker 0:    wakes → loops fn(0), fn(1), ..., fn(127) → signals done → sleeps
Worker 1:    wakes → loops fn(128), fn(129), ..., fn(255) → signals done → sleeps
Worker 2:    wakes → loops fn(256), fn(257), ..., fn(383) → signals done → sleeps
Worker 3:    wakes → loops fn(384), fn(385), ..., fn(511) → signals done → sleeps

Main thread:  all 4 done → continues to next stage
```

This is identical to how game engines like Unreal (TaskGraph), Unity (Job System), and id Tech (parallel_for) handle per-frame parallelism. The thread count never exceeds what was configured at startup.

### Frame Pipeline Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│  Main Thread (SkullbonezRun::Run)                                   │
│                                                                     │
│  Input → [FENCE: InputComplete]                                     │
│        → Submit physics work → [FENCE: PhysicsComplete]             │
│        → Submit broadphase viz → Render → Present                   │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │  WorkerPool (N threads)                                       │  │
│  │                                                               │  │
│  │  Thread 0 ─┐                                                  │  │
│  │  Thread 1 ─┤── Wait on condition_variable                     │  │
│  │  Thread 2 ─┤── Wake → pop task from queue → execute           │  │
│  │  Thread N ─┘── Signal fence when batch complete                │  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Component Breakdown

### 1. `SkullbonezWorkerPool` — Thread Pool & Task Queue

**File:** `SkullbonezSource/SkullbonezWorkerPool.h` / `.cpp`  
**Namespace:** `SkullbonezCore::Threading`

**Core API:**
```cpp
class WorkerPool {
public:
    static WorkerPool& Instance();
    void Initialise( int threadCount );  // Create N worker threads (created ONCE at startup, reused every frame)
    void Shutdown();                     // Join all threads (at exit only)

    // Submit a single task (lambda or callable)
    void Submit( std::function<void()> task );

    // Submit a parallel-for: calls fn(index) for [begin, end), split across workers.
    // Does NOT create threads — divides the range into N contiguous chunks and assigns
    // one chunk to each pre-existing worker thread.  Blocks until all chunks complete.
    //
    // Example: ParallelFor(0, 512, fn) with 4 workers:
    //   Worker 0 loops: fn(0), fn(1), ..., fn(127)
    //   Worker 1 loops: fn(128), fn(129), ..., fn(255)
    //   Worker 2 loops: fn(256), fn(257), ..., fn(383)
    //   Worker 3 loops: fn(384), fn(385), ..., fn(511)
    //   Main thread blocks until all 4 workers signal done.
    //
    // If (end - begin) < MIN_PARALLEL_SIZE, runs entirely inline on the calling thread
    // (avoids wake/signal overhead for trivially small ranges).
    void ParallelFor( int begin, int end, const std::function<void(int)>& fn );

    // Submit with fence — all tasks submitted before Fence() complete before
    // anything submitted after Fence() begins
    FenceHandle Fence();
    void WaitForFence( FenceHandle fence );  // Block calling thread until fence completes

    int GetThreadCount() const;
};
```

**Implementation details:**
- **Thread count is fixed at startup** — e.g., 4 workers = 4 `std::thread` objects created once in `Initialise()`, live until `Shutdown()`. No per-frame thread creation/destruction.
- Each worker thread loops: wait on condvar → receive (begin, end, fn) chunk → iterate `fn(begin)` through `fn(end-1)` → signal done → loop back to wait
- `ParallelFor` is a **fork-join** within the pre-existing pool: wake N workers with their chunk ranges, then block until all N signal complete. Total threads active = N workers + 1 main thread blocked.
- Lock-free MPSC queue (single consumer = worker pop, multiple producers = main thread + other workers)
- Actually: use `std::mutex` + `std::deque` for simplicity since we're not micro-optimizing yet; profile later
- Zero per-frame heap allocation: task queue pre-allocated, `std::function` uses small-buffer optimization for lambdas ≤ ~32 bytes (capture by reference for hot paths)
- `MIN_PARALLEL_SIZE` constant (e.g., 32): if the range is smaller than this, `ParallelFor` runs the loop inline on the calling thread — the overhead of waking/signalling workers is not worth it for tiny ranges

### 2. `SkullbonezFence` — Frame Synchronization Barriers

**File:** `SkullbonezSource/SkullbonezFence.h`  
**Namespace:** `SkullbonezCore::Threading`

```cpp
class Fence {
public:
    Fence( int taskCount );          // Construct with expected task count
    void Signal();                   // Decrement remaining (called by worker on task completion)
    void Wait();                     // Block until remaining == 0
    bool IsComplete() const;         // Non-blocking query
    void Reset( int taskCount );     // Reuse for next frame (no reallocation)
};
```

**Implementation:** `std::atomic<int>` counter + `std::mutex`/`std::condition_variable` for wait. The atomic gives zero-cost polling (`IsComplete`), the condvar gives efficient blocking.

### 3. `SkullbonezLockOrderValidator` — Deadlock Detection (Debug Only)

**File:** `SkullbonezSource/SkullbonezLockOrderValidator.h` / `.cpp`  
**Namespace:** `SkullbonezCore::Threading`

```cpp
// Debug-only wrapper around std::mutex
class TrackedMutex {
public:
    TrackedMutex( const char* name );  // Human-readable name for diagnostics
    void lock();                       // Records edge in lock-order graph, asserts no cycle
    void unlock();                     // Removes edge
    bool try_lock();
private:
    std::mutex m_inner;
    const char* m_name;
    uint32_t m_id;  // Unique ID for graph node
};

class LockOrderValidator {
public:
    static LockOrderValidator& Instance();
    void RecordAcquisition( uint32_t lockId, uint32_t threadId );
    void RecordRelease( uint32_t lockId, uint32_t threadId );
    bool DetectCycle();  // DFS cycle detection on current lock-order graph
};
```

**In Release builds:** `TrackedMutex` compiles down to a plain `std::mutex` wrapper (zero overhead). The validator is `#ifdef _DEBUG` only.

**Lock-order graph:**
- Nodes = mutex IDs
- Directed edge A→B means "some thread acquired B while holding A"
- On each new acquisition: add edge, run DFS from new node — if cycle detected, `assert(false)` with diagnostic message showing the cycle path + thread IDs + mutex names

### 4. `SkullbonezAmortizedTask` — Worker-Based Multi-Frame Chunked Work

**File:** `SkullbonezSource/SkullbonezAmortizedTask.h`  
**Namespace:** `SkullbonezCore::Threading`

#### What Problem Does This Solve?

Some tasks are too expensive to complete in a single frame without causing a hitch, but they don't need to be finished instantly. Rather than running on the main thread (which would stall the frame), an amortized task runs **on a worker thread** — it processes a chunk of work, yields back to the pool, and picks up where it left off next frame.

The main thread never blocks on an amortized task. The worker does its budget, returns to the idle pool, and is available for other work (ParallelFor chunks, etc.) until the next frame ticks it again.

**This is NOT used for frame-critical physics** (physics must complete each frame). It's for work that tolerates latency:
- Rebuilding broadphase debug visualization data
- Streaming terrain LOD computation
- Precomputing shadow maps for distant objects
- AI pathfinding that doesn't need instant response

#### How It Works — Concrete Example

Say you need to process 1000 items and your budget is 250 per frame:

```
Frame 1:  Main thread submits amortized tick to pool
          → Worker 2 wakes, processes items [0, 250), yields back to pool
          → Main thread does NOT wait — continues to render

Frame 2:  Main thread submits amortized tick to pool
          → Worker 0 wakes, processes items [250, 500), yields back to pool

Frame 3:  Worker processes [500, 750)
Frame 4:  Worker processes [750, 1000) → IsComplete() = true
Frame 5:  Reset() → cursor back to 0, ready for next pass
```

**Key properties:**
- The task maintains persistent state (cursor position) across frames
- Each frame, the main thread submits a "tick" to the worker pool — a worker picks it up, does the budgeted chunk, and returns
- The main thread **never blocks** on this — it fires and forgets. It can check `IsComplete()` or `GetProgress()` non-blockingly at any time
- The work runs on whichever worker is available — it doesn't pin to a specific thread
- Between ticks, the worker is free to service other tasks (ParallelFor chunks, etc.)

#### API

```cpp
class AmortizedTask {
public:
    // totalItems = total work units; itemsPerFrame = budget per tick
    AmortizedTask( int totalItems, int itemsPerFrame, std::function<void(int begin, int end)> work );

    void SubmitTick( WorkerPool& pool ); // Submit one chunk of work to the pool (non-blocking)
    bool IsComplete() const;             // All items processed this cycle (atomic read)
    void Reset();                        // Restart cursor from 0 (next cycle)
    float GetProgress() const;           // 0.0 → 1.0 (atomic read)

    void SetBudget( int itemsPerFrame ); // Adjust budget at runtime (e.g., if frame time is tight, reduce)
};
```

#### Usage in the Main Loop

```cpp
// Created once (e.g., in Initialise)
AmortizedTask vizRebuild( 2048, 512, [&]( int begin, int end ) {
    for ( int i = begin; i < end; ++i )
        RebuildVizCell( i );
});

// Called every frame in Run() — non-blocking, worker handles it
if ( !vizRebuild.IsComplete() )
    vizRebuild.SubmitTick( WorkerPool::Instance() );
else
    vizRebuild.Reset();  // Start next cycle
```

**Key distinction from ParallelFor:**
- `ParallelFor` = "do ALL items THIS frame, split across threads, **main blocks until done**" (synchronous, parallel)
- `AmortizedTask` = "do SOME items this frame on a worker, **main does NOT block**" (asynchronous, spread across time)

Both use the same worker pool — the threads are shared. An amortized tick is just another task in the queue, but it only processes a portion of the total work before returning the worker to the pool.

**Use case:** Rebuilding spatial grid with 10K cells over 3 frames, or streaming terrain LOD computation.

### 5. Frame Fence Definitions — Dependency System

The main loop defines named fences representing frame-step boundaries:

```
FENCE_INPUT_COMPLETE      — Input polling done; safe to read input state
FENCE_FORCES_COMPLETE     — All ApplyForces() calls done
FENCE_BROADPHASE_COMPLETE — Spatial grid built, candidate pairs ready
FENCE_NARROWPHASE_COMPLETE — All collision responses resolved
FENCE_TERRAIN_COMPLETE    — All terrain collisions resolved
FENCE_INTEGRATE_COMPLETE  — All positions updated, sleep evaluated
FENCE_PHYSICS_COMPLETE    — Full physics tick done; safe to read positions for render
FENCE_RENDER_COMPLETE     — Render submitted; safe to present
```

Tasks declare their fence dependencies:
```cpp
pool.Submit( FENCE_FORCES_COMPLETE, [&]{ /* broadphase insert */ } );
// This task won't execute until FENCE_FORCES_COMPLETE is signalled
```

### 6. Configuration — engine.cfg Extension

```ini
# Threading
worker_threads = -1        # -1 = auto (hardware_concurrency - 1), 0 = disable, N = exact count
physics_parallel = 1       # Enable parallel physics stages
amortize_broadphase = 0   # Spread broadphase rebuild across frames (0 = per-frame)
```

**CLI override:** `--workers N`

---

## Phase 1: Parallel Embarrassingly-Parallel Physics

### What Gets Parallelized

| Stage | Current | Parallelized | Strategy |
|-------|---------|-------------|----------|
| ApplyForces | `for(x=0..N) model[x].ApplyForces(dt)` | `ParallelFor(0, N, [](int x){ model[x].ApplyForces(dt); })` | Per-model independent; no shared writes |
| Broadphase Insert | `for(i=0..N) grid.Insert(i, pos, r)` | Serial (grid has shared state) | Keep serial in Phase 1 |
| Broadphase GetPairs | Single call | Serial | Already O(n+k), fast |
| Narrowphase | Sequential pair loop | **Serial** | Pairs share model state |
| Terrain Response | `for(x=0..N) if(awake) respond()` | `ParallelFor(0, N, [](int x){ if(awake) respond(); })` | Per-model independent after narrowphase |
| Integration + Sleep | `for(x=0..N) integrate()` | `ParallelFor(0, N, [](int x){ integrate(); })` | Per-model independent |

### Modified `RunSolverPhysics` Flow

```cpp
void GameModelCollection::RunSolverPhysics( float dt )
{
    auto& pool = WorkerPool::Instance();

    // Stage 1: Apply forces (parallel)
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    pool.ParallelFor( 0, modelCount, [&]( int x ) {
        if ( m_sleepState[x] ) { m_timeRemaining[x] = 0.0f; return; }
        m_gameModels[x].ApplyForces( dt );
    });
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // Stage 2: Broadphase (serial — shared grid state)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    for ( int i = 0; i < modelCount; ++i )
        m_spatialGrid.Insert( i, m_gameModels[i].GetPosition(), m_gameModels[i].GetBoundingRadius() );
    m_spatialGrid.GetCandidatePairs( m_candidatePairs );
    PROFILE_END( "Frame/Physics/Broadphase" );

    // Stage 3: Narrowphase (serial — pair state coupling)
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    // ... existing sequential pair loop unchanged ...
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Stage 4: Terrain response (parallel — per-model independent)
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    pool.ParallelFor( 0, modelCount, [&]( int x ) {
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f ) return;
        float colTime = m_gameModels[x].CollisionDetectTerrain( m_timeRemaining[x] );
        if ( m_gameModels[x].IsResponseRequired() ) {
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[x].CollisionResponseTerrain( m_timeRemaining[x] - colTime );
            m_groundedThisFrame[x] = 1;
            m_timeRemaining[x] = 0.0f;
        }
    });
    PROFILE_END( "Frame/Physics/Terrain" );

    // Stage 5: Integration + sleep (parallel)
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    pool.ParallelFor( 0, modelCount, [&]( int x ) {
        if ( m_sleepState[x] ) return;
        if ( m_timeRemaining[x] > 0.0f ) m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        // ... sleep evaluation (reads vel/omega, writes sleepState[x]) ...
    });
    PROFILE_END( "Frame/Physics/Integrate" );
}
```

### Thread Safety Analysis (Phase 1)

| Data | Access Pattern | Safe? |
|------|---------------|-------|
| `m_gameModels[x]` (per-model) | Each index accessed by exactly one worker | ✅ No contention |
| `m_sleepState[x]` | Written only by worker owning index x | ✅ No contention |
| `m_timeRemaining[x]` | Written only by worker owning index x | ✅ No contention |
| `m_groundedThisFrame[x]` | Written only by worker owning index x | ✅ No contention |
| `m_spatialGrid` | Serial broadphase stage | ✅ No parallelism |
| `m_candidatePairs` | Serial narrowphase stage | ✅ No parallelism |
| `WorldEnvironment*` / `Terrain*` | Read-only during physics | ✅ Shared reads safe |

**No mutexes needed in Phase 1** — all parallel stages have disjoint per-index writes. The fence system ensures ordering between stages.

---

## Phase 2: Island Decomposition for Narrowphase

### Concept

Objects that don't interact (no collision pair between them this frame) form independent "islands." Islands can be solved in parallel because they share no state.

### Algorithm

1. After `GetCandidatePairs()`, build an undirected graph: nodes = model indices, edges = candidate pairs
2. Find connected components (union-find / BFS) — each component = one island
3. Sort pairs by island
4. Submit each island's pair-set as an independent task to the worker pool
5. Workers execute narrowphase for their island — no cross-island data access

### Implementation Steps

```cpp
struct Island {
    int id;
    std::vector<int> modelIndices;      // Models in this island
    std::vector<std::pair<int,int>> pairs; // Pairs to solve
};

class IslandBuilder {
public:
    void Build( const std::vector<std::pair<int,int>>& pairs, int modelCount );
    int GetIslandCount() const;
    const Island& GetIsland( int i ) const;
};
```

**Union-Find** with path compression + rank: O(α(n)) per operation, effectively O(1). No allocation if we pre-size the parent/rank arrays.

### Concurrency Considerations

- Each island's models are **disjoint** by construction — no two islands share a model index
- Within an island, pairs are solved sequentially (same as current narrowphase)
- Cross-island parallelism is embarrassingly parallel
- Sleep/wake transitions that cross islands must be handled: if an awake object in island A collides with a sleeping object in island B, they **must** be in the same island (union-find merges them via the pair edge)

### Expected Gains

With 512 objects:
- Typical scene: 5-15 islands (one big cluster + scattered singles)
- "Spread out" scene: 50+ islands → near-linear speedup on narrowphase
- Worst case (all objects in one cluster): 1 island → no parallel gain (same as Phase 1)

---

## File Structure

```
SkullbonezSource/
├── SkullbonezWorkerPool.h          # Thread pool + ParallelFor + Fence
├── SkullbonezWorkerPool.cpp
├── SkullbonezFence.h               # Atomic fence barrier
├── SkullbonezLockOrderValidator.h  # Debug-only ABBA detection
├── SkullbonezLockOrderValidator.cpp
├── SkullbonezAmortizedTask.h       # Multi-frame chunked work
├── SkullbonezIslandBuilder.h       # Phase 2: union-find island decomposition
├── SkullbonezIslandBuilder.cpp
```

---

## Implementation Order (Todos)

### Infrastructure
1. **worker-pool-core** — `SkullbonezWorkerPool` with thread lifecycle, task queue, `Submit()`, `ParallelFor()`
2. **fence-system** — `SkullbonezFence` atomic barrier; integrate with `ParallelFor` return value
3. **lock-order-validator** — `TrackedMutex`, lock-order graph, DFS cycle detection (Debug only)
4. **amortized-task** — `SkullbonezAmortizedTask` multi-frame chunking infrastructure
5. **config-integration** — Add `worker_threads` to engine.cfg + `--workers` CLI arg

### Phase 1: Physics Parallelization
6. **parallel-apply-forces** — Convert ApplyForces loop to `ParallelFor`
7. **parallel-terrain-response** — Convert terrain collision loop to `ParallelFor`
8. **parallel-integration** — Convert integration + sleep loop to `ParallelFor`
9. **profiler-threading** — Make profiler thread-safe (per-thread marker stacks) or restrict to main thread
10. **physics-stress-test** — Create scene with 512 balls, benchmark single-threaded vs worker pool
11. **raise-model-cap** — Increase `MAX_GAME_MODELS` from 512 → 2048+ once parallel physics proves stable

### Phase 2: Island Decomposition
12. **island-builder** — Union-find connected components from candidate pairs
13. **parallel-narrowphase** — Submit per-island narrowphase tasks to worker pool
14. **island-wake-handling** — Ensure sleep/wake transitions merge islands correctly
15. **island-stress-test** — Benchmark with clustered vs spread-out scenes

### Validation & Polish
16. **determinism-test** — Verify physics regression CSVs match between single-threaded and multi-threaded runs (fixed-step mode should produce identical results regardless of thread count)
17. **deadlock-stress** — Targeted test that hammers lock acquisition patterns to validate ABBA detection

---

## Risk Analysis

| Risk | Mitigation |
|------|-----------|
| Non-determinism from float reordering | `ParallelFor` always assigns same index ranges to same chunk order; reduction (if any) uses ordered accumulation |
| Profiler not thread-safe | Phase 1 keeps profiler macros on main thread only; worker timings use separate counters merged at fence |
| False sharing on adjacent array elements | `m_gameModels` elements are large (~400+ bytes); `m_sleepState` is `uint8_t` array — pad or use `alignas(64)` per-cache-line grouping if perf shows contention |
| Worker pool overhead > gain for small N | `ParallelFor` has a threshold: if `(end - begin) < MIN_PARALLEL_SIZE`, runs inline on calling thread |
| Island decomposition single-island degenerate | Acceptable — falls back to serial narrowphase (same as Phase 1) |

---

## Success Criteria

1. **Phase 1 complete:** 512 balls at ≥60 FPS (currently the bottleneck is ~200 balls at 60 FPS with vsync off)
2. **Phase 2 complete:** 1024+ balls at ≥60 FPS in spread-out configurations
3. **Determinism preserved:** Physics regression CSVs bit-identical in fixed-step mode
4. **Zero data races:** No TSan/address-sanitizer warnings under stress
5. **Zero deadlocks:** Lock-order validator catches ABBA before it ships
6. **Profiler integration:** Worker time shows up in profiler overlay as "Frame/Physics/Workers"
