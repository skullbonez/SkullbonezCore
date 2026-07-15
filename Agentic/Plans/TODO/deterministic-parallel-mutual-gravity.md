# Deterministic Parallel Mutual Gravity — Exact Sum, Bitwise-Identical, Any Thread Count

Date: 2026-07-15
Status: Active — 0/4 tasks complete
Impact area: `PhysicsWorld::PrepareMutualGravityForces`, worker pool usage,
allocation policy scratch reservation, physics determinism baselines
Owner: physics

## Problem And Evidence

Mutual gravity is a serial O(n²) triangular loop on the fixed-tick critical
path (`SkullbonezSource/Physics/PhysicsWorld.cpp:3470-3540`), with an explicit
comment keeping it serial so "worker scheduling" cannot enter deterministic
state, and an `SB_FATAL` on scratch exhaustion at `PhysicsWorld.cpp:3483`.

Adversarial review 2026-07-15, finding #6: determinism is being used as the
reason to leave single-threaded scalar work on the hot path, but a
deterministic parallel exact sum exists.

Key insight recorded with the owner decision: the current serial loop delivers
each body its force contributions in **ascending other-body-index order**
(for body `m`: pairs `(0,m) … (m-1,m)` in row order, then `(m,m+1) … (m,n-1)`
in column order). If rows are partitioned into fixed chunks (chunk boundaries
a pure function of `modelCount`, never of thread count), each worker
accumulates into a private per-body force buffer, and the per-chunk buffers
are reduced serially in ascending chunk order, then every body's additions
happen in exactly the current serial order. Result: **bitwise-identical to
today at any thread count, zero baseline refresh.**

## Goal

`PrepareMutualGravityForces` runs the pair loop across the worker pool with a
fixed-chunk, ascending-order reduction that is bitwise-identical to the
current serial output, deterministic for any worker thread count including
zero, and compliant with the runtime allocation policy (all scratch
preallocated).

## Non-Goals

- No approximation: no Barnes-Hut, no interaction caps, no softening changes
  (rejected for now by the 2026-07-15 owner decision; candidate future work if
  body counts grow past what exact O(n²) tolerates).
- No change to force math, mass gating, sleep gating, or the fatal-on-
  exhaustion capacity policy (capacity sizing may grow for the new scratch,
  the failure lane stays fatal).

## Tasks

- [ ] T1 — Chunking design in code: fixed row-chunk table computed from
      `modelCount` only (e.g. fixed chunk size constant, last chunk ragged),
      an explicit `Invariant:` comment stating chunk boundaries and reduce
      order are independent of `workerPool.GetThreadCount()`, and the serial
      path expressed as the same code with one chunk so there is exactly one
      force-accumulation implementation.
- [ ] T2 — Scratch ownership: per-chunk `Vector3` force buffers
      (`chunkCount × modelCount`) preallocated through the existing physics
      scratch reservation path, registered with the allocation policy
      (owner, phase, cap) — no steady-state growth. High-water and capacity
      diagnostics match the existing scratch conventions; exhaustion remains
      lane-F fatal with owner/capacity/required in the message.
- [ ] T3 — Parallel dispatch: run chunks via
      `workerPool.ParallelForNoAlloc`/`ParallelForChunksNoAlloc` behind the
      existing `config.physicsExecution.parallel` family (new
      `parallelMutualGravity` toggle, default following the sibling stages),
      honoring the existing min-work threshold pattern so small scenes stay
      serial. Serial reduce in ascending chunk order into
      `m_mutualGravityForces`. Worker profiler markers follow the
      `Frame/Physics/...` naming convention.
- [ ] T4 — Proof and gates. A focused determinism check runs the same scene
      with threads 0, 1, and N and byte-compares physics CSV output (extend
      `TestDeterminism` or a bounded standalone check — record which). Then
      `tools\validate_physics.bat` byte-exact against the unchanged committed
      baseline (this is the bitwise-identity proof against today's serial
      code), then `tools\validate_perf.bat` for the hot-path evidence.

## Dependencies And Decisions

- Owner decision 2026-07-15: "parallel exact sum only" chosen over Barnes-Hut
  threshold and interaction-cap options; approximation is out of scope.
- Hot-path rules apply: value records, bounded scratch, no polymorphic
  services, no allocation inside the pass (`AGENTS.md` hot-path review rule).
- Independent of the Vector3 plans in mechanism, but should land after
  `vector3-inline-hot-math.md` so perf evidence reflects the inlined ops.

## Acceptance

- Same-scene physics CSV is byte-identical across worker counts 0/1/N and
  identical to the pre-change committed baseline (no refresh).
- No steady-state allocation; scratch is policy-registered with a cap.
- Perf gate shows the pass scaling with cores on a mutual-gravity scene (or
  documents why the threshold kept it serial for existing scenes).

## Validation

- Focused multi-thread-count determinism comparison (command + byte-compare
  result recorded), then `tools\validate_physics.bat`, then
  `tools\validate_perf.bat`; output pasted at closure.
