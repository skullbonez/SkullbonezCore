# SkullbonezCore Simulation Optimisation Plan

This document lists the likely performance improvements for the SkullbonezCore simulation, ordered from **largest expected benefit** to **smallest expected benefit**. The ordering is based on inspection of the current hot paths and should be validated with the existing profiler markers in the 300-ball `perf_test.scene` workload.

## Baseline before changing code

Before implementing any individual optimisation, capture a baseline from the existing profiler output for the same scene, backend, resolution, and config:

- Use `SkullbonezData/scenes/perf_test.scene` as the primary reproducible workload because it is already configured as a 300-ball performance test.
- Compare the profiler sections already present in `GameModelCollection::RunPhysics`:
  - `Frame/Physics/ApplyForces`
  - `Frame/Physics/Broadphase`
  - `Frame/Physics/Narrowphase`
  - `Frame/Physics/Terrain`
  - `Frame/Physics/Integrate`
- Record both average and high-percentile frame times; several of these issues are bursty, especially logging and GPU/CPU synchronisation.
- Change one thing at a time and keep a short before/after table in the PR that implements each optimisation.

---

## 1. Disable the always-on vector CSV logging in scene/perf runs

### Why this is probably the largest accidental slowdown

`VECTOR_LOG_ENABLED` is currently unconditionally defined in `SkullbonezRun.cpp`. In scene mode, the code logs every 6 frames for every ball. For the 300-ball perf scene, that means each logging frame performs hundreds of formatted writes and math operations, then flushes the file.

Per logged ball, the current logging path does roughly:

- Reads velocity and angular velocity.
- Computes horizontal velocity magnitude with `sqrtf`.
- Computes horizontal angular-velocity magnitude with `sqrtf`.
- Computes a dot product and clamps it.
- Computes `acosf` to produce an angle in degrees when both vectors are non-trivial.
- Emits one CSV row via `fprintf`.

Per logging frame, it then calls `fflush`, forcing buffered file data toward the OS. In perf/scene runs this is especially damaging because the workload is supposed to measure simulation and rendering, not diagnostic I/O.

### Recommended change

Make vector CSV logging **off by default** and enable it only through an explicit scene flag, for example:

```text
vector_log on
vector_log_interval 6
vector_log_path Debug/vector_log.csv
vector_log_flush on
```

Suggested implementation details:

1. Remove the unconditional `#define VECTOR_LOG_ENABLED`.
2. Add scene-level config fields such as:
   - `m_sceneVectorLogEnabled = false`
   - `m_sceneVectorLogInterval = 6`
   - `m_sceneVectorLogFlush = false`
   - `m_sceneVectorLogPath = "Debug/vector_log.csv"`
3. Parse the scene flag in the scene loader.
4. Only open the file if the flag is enabled.
5. Do not flush every logging frame by default. Either:
   - flush only at scene end,
   - flush every N logging frames,
   - or keep `vector_log_flush on` as a debugging option.
6. Consider writing fewer derived values. For example, log raw `v` and `omega` only, then compute magnitudes/angles offline in Python if needed.

### Expected benefit

Very high for scene/perf runs. This removes repeated file I/O, formatted output, transcendental math, and forced flushing from the benchmark path. It also makes perf results less noisy and more representative of the real simulation.

---

## 2. Remove unconditional pipeline stalls and flushes from normal frame paths

### Problem

Several backend methods intentionally force GPU/CPU synchronisation or immediate submission. These are useful for screenshots, resize, debug capture, readback, or deterministic teardown, but they should not be part of the normal performance path unless explicitly requested.

Examples to audit:

- OpenGL `FlushGPU()` and `Finish()` call `glFinish()`, which blocks the CPU until the GPU is idle.
- OpenGL screenshot capture correctly needs a finish/readback path, but this should remain isolated to screenshots, not normal frames.
- Direct3D 11 `Finish()` and `FlushGPU()` call `ID3D11DeviceContext::Flush()`.
- Direct3D 11 resize correctly uses `ClearState()`/`Flush()` as a special-case resource-lifetime barrier.
- Direct3D 12 upload-buffer exhaustion currently closes and executes the command list, then waits for the GPU mid-frame.
- Direct3D 12 `FlushGPU()` closes/submits work and waits for the GPU.
- Present paths use `Present(1, 0)`, which enables VSync and can cap or quantise frame timing. For perf runs, this should be configurable.

### Recommended change

Make forced synchronisation opt-in, and separate debug correctness barriers from normal runtime paths.

Suggested implementation details:

1. Add config/scene flags such as:
   - `gpu_flush_debug on|off`
   - `vsync on|off`
   - `gpu_timer_readback on|off`
   - `force_finish_before_readback on|off`
2. Keep hard synchronisation for operations that truly require it:
   - screenshots/readback,
   - resize/resource destruction,
   - explicit user/debug capture,
   - shutdown validation.
3. Avoid calling generic `FlushGPU()` from regular scene/perf frames unless a diagnostic flag is enabled.
4. For Direct3D 12 upload exhaustion, prefer a larger upload ring, per-frame upload pages, or deferred reclamation by fence instead of a mid-frame `WaitForGpu()`.
5. For perf runs, disable VSync so the simulation can run unconstrained and profiler output is not dominated by presentation timing.

### Expected benefit

Very high if any of these paths run during normal scene/perf frames. GPU/CPU sync points destroy parallelism and can make CPU-side simulation appear slower or noisier than it really is.

---

## 3. Precompute and cache terrain collision data

### Problem

Every model with remaining frame time checks terrain collision each frame. The terrain collision path locates the current terrain polygon, constructs triangle data, computes a plane/height, and may compute an intersection time.

`LocatePolygon` also performs bounds checks, `floorf`, `fmodf`, gradient logic, and vertex lookups. That is reasonable for occasional queries, but expensive when multiplied by hundreds of balls every frame.

### Recommended change

Create a fast terrain query path for physics:

1. Precompute per-cell/per-triangle plane equations during terrain build.
2. Store for each terrain quad:
   - triangle A plane,
   - triangle B plane,
   - triangle A normal,
   - triangle B normal,
   - any constants needed for direct height evaluation.
3. Replace physics collision sampling with direct indexing:
   - compute quad indices once,
   - choose triangle via a simple local-coordinate comparison,
   - read cached plane/normal,
   - compute height directly from `n.x*x + n.y*y + n.z*z = d`.
4. Add a separate analytic fast path for flat-slope terrain. The current flat-slope path fabricates a triangle for a known plane; physics should return height and normal directly.
5. Keep the existing `LocatePolygon` API for less frequent debug/render/helper use, but avoid it in the physics hot path.

### Expected benefit

High if `Frame/Physics/Terrain` is one of the larger profiler sections. This removes repeated triangle construction and plane recomputation from the per-ball frame loop.

---

## 4. Add cheap narrowphase early-outs before swept sphere collision

### Problem

The broadphase produces candidate pairs, then each pair performs a swept sphere collision test. The swept test uses vector magnitude, normalisation, dot products, and a square root for the quadratic root. Candidate pairs that obviously cannot collide should be rejected before reaching that path.

### Recommended change

Before the full swept collision:

1. Compute squared centre distance.
2. Compute `radiusSum` and `radiusSumSq`.
3. If already overlapping, go directly to static overlap/contact handling.
4. Compute relative velocity and `maxTravel = relativeSpeed * dt`.
5. If `distanceSq > (radiusSum + maxTravel)^2`, skip the full swept test.
6. If relative velocity is separating and the spheres are not overlapping, skip.
7. Only then run the current swept collision calculation.

Also consider rewriting the swept sphere test to use squared values until the final collision-time calculation, avoiding `sqrtf` unless a collision is plausible.

### Expected benefit

High in scenes where the spatial grid still produces many non-colliding candidate pairs.

---

## 5. Tune the broadphase grid cell size per workload

### Problem

`broadphaseCell` is configurable. If it is too small relative to ball diameter, each ball overlaps many cells and insertion/pair dedup work rises. If it is too large, each cell contains too many balls and candidate-pair counts rise.

### Recommended change

Run a small sweep against `perf_test.scene` and record:

- broadphase time,
- narrowphase time,
- candidate-pair count,
- average entries per object,
- max bucket occupancy.

Try values around the common ball diameter and radius distribution, for example:

```text
broadphaseCell = 8
broadphaseCell = 11
broadphaseCell = 16
broadphaseCell = 24
broadphaseCell = 32
```

Then choose the value that minimises `Broadphase + Narrowphase`, not just one section alone.

### Expected benefit

Medium to high, and this may require only configuration changes once the right value is known.

---

## 6. Track active buckets in the spatial grid

### Problem

The spatial grid uses generation stamping and fixed arrays, which is good. However, candidate-pair generation scans the entire bucket table every frame, even though only a subset of buckets may be active.

### Recommended change

Track active bucket indices when a bucket is first created for the current generation:

```cpp
int activeBuckets[TABLE_SIZE];
int activeBucketCount;
```

In `FindOrCreate`, when a stale bucket is claimed for the current generation, append its index to `activeBuckets`. Then `GetCandidatePairs` can iterate only active buckets.

Also review the local `cellIndices[64]` staging buffer. If a dense bucket exceeds 64 entries, pair generation truncates the cell. Either guard/assert clearly in release diagnostics, enlarge the buffer, or generate pairs directly from the linked list.

### Expected benefit

Medium in sparse scenes, lower in fully dense scenes, but it improves scalability and makes the grid cost proportional to active cells rather than table size.

---

## 7. Remove per-frame allocations in `RunPhysics`

### Problem

`RunPhysics` constructs `timeRemaining` and `groundedThisFrame` vectors every frame. For the perf scene, this happens continuously and creates unnecessary allocator/cache churn.

### Recommended change

Make these persistent members of `GameModelCollection`:

```cpp
std::vector<float> m_timeRemaining;
std::vector<uint8_t> m_groundedThisFrame;
```

Each frame:

```cpp
m_timeRemaining.assign(modelCount, fChangeInTime);
m_groundedThisFrame.assign(modelCount, 0);
```

Avoid `std::vector<bool>` in this hot path because it is bit-packed and can generate slower proxy operations than a byte vector.

### Expected benefit

Medium to small, but low risk and easy to verify.

---

## 8. Gate or decimate terrain rolling/orientation correction

### Problem

Terrain collision response performs a physically rich contact solve, including normal impulse, friction impulse, spin damping, rolling friction, rolling omega reconstruction, and pole/orientation correction. The pole/orientation correction path includes normalisation, square roots, `acosf`, cross products, and quaternion rotation.

This may be valuable visually, but it is expensive if run for many grounded balls every frame.

### Recommended change

Make the expensive orientation correction conditional:

1. Skip if the ball is moving/spinning below a threshold.
2. Skip if pole alignment is already inside tolerance.
3. Run every N frames per ball instead of every contact frame.
4. Add a scene/config flag for high-fidelity rolling visual correction.
5. In perf scenes, default to the cheaper path unless the test specifically measures rolling-orientation correctness.
6. Replace `acosf` angle extraction with a cheaper approximation for very small corrections, or use a direct quaternion-from-two-vectors helper that avoids computing the explicit angle where possible.

### Expected benefit

Medium when many balls are grounded; low when most balls are airborne.

---

## 9. Specialise hot physics paths for sphere-only scenes

### Problem

The collision shape abstraction uses `std::variant`, but the variant currently contains only `BoundingSphere`. The abstraction is good for future shape support, but in the current sphere-only simulation it adds dispatch and makes hot-path code less direct.

### Recommended change

Keep the generic shape abstraction for extensibility, but add sphere-specialised fast paths for the common case:

1. Store radius, radius squared, volume, projected area, and drag coefficient directly on `GameModel` or a compact physics proxy.
2. Use direct sphere access in broadphase, narrowphase, and terrain bottom-offset calculations.
3. Avoid visitor dispatch for per-frame `GetBoundingRadius`, `GetSubmergedVolumePercent`, and `TestShapeCollision` calls when the scene is known sphere-only.
4. Preserve the generic path behind assertions/tests so new shape support remains possible later.

### Expected benefit

Small to medium. This is less important than logging, stalls, terrain caching, and narrowphase early-outs, but it cleans up repeated per-object work.

---

## 10. Cache immutable per-ball physics values

### Problem

Several values are recalculated or fetched through layers of getters even though they are immutable after the ball is created:

- radius,
- radius squared,
- volume,
- projected surface area,
- drag coefficient,
- inverse mass,
- inertia/inverse inertia.

### Recommended change

Create a compact per-ball physics cache or ensure existing fields are read directly in hot loops. The force application path should not need to visit the shape abstraction to get immutable sphere properties every frame.

Potential cached structure:

```cpp
struct BallPhysicsCache
{
    float radius;
    float radiusSq;
    float volume;
    float projectedArea;
    float dragCoefficient;
    float mass;
    float invMass;
    Vector3 inertia;
    Vector3 invInertia;
};
```

### Expected benefit

Small to medium, but it compounds across hundreds of balls and simplifies other optimisations.

---

## 11. Reduce duplicate vector math in collision response

### Problem

Some collision response code recomputes expressions such as tangent velocity, projected velocity, or normal components multiple times in the same function.

### Recommended change

Within each response function:

1. Compute `normalVelocity = velocity * normal` once.
2. Compute `tangentVelocity = velocity - normal * normalVelocity` once.
3. Reuse tangent speed and tangent direction.
4. Avoid normalising vectors more than once.
5. Prefer squared-length checks before computing actual length.

### Expected benefit

Small, but safe and easy to combine with larger terrain-response work.

---

## 12. Optimise shadow/terrain sampling only after physics is clean

### Problem

Shadow rendering already batches instances and uses a fused shadow matrix path, which suggests rendering has already received some optimisation. However, shadow generation still samples terrain per model and uploads instance data each frame.

### Recommended change

Only revisit this after physics/logging/stalls have been addressed. If rendering remains hot:

1. Reuse the cached terrain query path from the physics terrain optimisation.
2. Avoid inserting into `m_shadowInstanceData` with repeated small appends if a direct indexed write is practical.
3. Consider skipping shadow updates for off-camera or very distant balls.
4. Consider lower-frequency shadow updates in perf/debug modes.

### Expected benefit

Small to medium depending on GPU/backend and whether shadows are enabled. Likely below the physics and logging items for the current perf scene.

---

## 13. Make diagnostic text and perf flushing configurable

### Problem

Diagnostic output is useful, but text overlays and frequent file flushing can distort perf measurements. The perf log currently flushes periodically, and text rendering can add CPU/GPU work that is unrelated to the simulation.

### Recommended change

1. Keep `text off` available in perf scenes when measuring simulation only.
2. Batch perf-log writes and flush at scene end by default.
3. Add explicit flags for `perf_log_flush_interval` or `perf_log_flush on` when crash-resilient logs are needed.
4. Ensure benchmark scenes document whether they measure simulation-only, render-only, or full-frame performance.

### Expected benefit

Small to medium. This mainly improves benchmark stability and prevents debug features from hiding real wins.

---

## Suggested implementation order

1. Disable vector CSV logging by default and add explicit scene flags.
2. Audit normal scene/perf frame paths for unconditional GPU flushes, finishes, waits, readbacks, and VSync caps.
3. Precompute/cached terrain plane data and add a physics-specific terrain query API.
4. Add narrowphase early-outs before swept sphere collision.
5. Tune `broadphaseCell` and add candidate-pair/grid diagnostics.
6. Track active buckets in `SpatialGrid`.
7. Remove per-frame allocations in `RunPhysics`.
8. Gate terrain orientation correction.
9. Add sphere-only fast paths and immutable per-ball caches.
10. Clean up duplicate vector math.
11. Revisit shadows/text/perf flushing once the above are measured.

## Measurement checklist for each optimisation PR

Each optimisation PR should include:

- Scene used, backend used, resolution, VSync state, and build configuration.
- Average frame time before/after.
- P95/P99 frame time before/after if available.
- Relevant profiler section before/after.
- Candidate-pair count before/after for broadphase/narrowphase changes.
- Confirmation that the visual/physics behaviour is unchanged, or a clear note explaining the intentional tradeoff.
