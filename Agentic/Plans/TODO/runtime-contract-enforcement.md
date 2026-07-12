# Runtime Contract Enforcement

Date: 2026-07-12
Status: Not started — 0/5 phases complete
Impact area: core logging/fatal path, worker-pool primitives, physics broadphase
input validation
Owner: core runtime + physics
Priority: Must do (2026-07-12 adversarial review, round 2)

## Problem And Evidence (measured 2026-07-12)

A second adversarial pass over the post-remediation source found no
production-crash-class defects, but four places where an internal contract is
enforced by convention or a comment instead of by the code:

1. **EngineLog is thread-unsafe on the fatal path.** `EngineLog` wraps an
   unguarded `std::unordered_map<std::string, FILE*>`
   (`SkullbonezSource/Core/Log.h:81`) with no mutex anywhere in `Log.cpp` and
   no stated thread-affinity invariant. `SB_FATAL` writes through it
   (`SkullbonezSource/Core/FatalError.cpp:60-63`), and worker threads execute
   code containing `SB_FATAL` (e.g.
   `SkullbonezSource/Physics/PhysicsBodyStore.cpp:1038` via the parallel
   integration stage, and the prediction `PhysicsEngine` running inside
   `AmortizedTask` workers). A worker-side fatal mutates the log map
   concurrently with any main-thread `Writef` — undefined behavior exactly
   while producing crash evidence. Debug-only in effect (Release logging is a
   no-op), which caps severity but not the diagnostic cost.
2. **SpatialGrid trusts non-finite input.** `SpatialGrid::InsertBounds`
   (`SkullbonezSource/Physics/SpatialGrid.cpp:180-196`) casts
   `floorf(coordinate * inverseCellSize)` to `int` with no finiteness or
   world-extent check. A NaN position (the exact state Debug's
   `Vector3` NaN-poisoning exists to expose) silently drops the body from
   broadphase — presents as tunneling with no diagnostic. Huge finite
   coordinates hit float→int UB (`cvttss2si` → `INT_MIN` on MSVC), and a
   min/max straddle can drive the cell loop through billions of iterations
   before the entry-pool fatal ends it. Cosmetic sibling: bucket cell coords
   truncate to `int16_t` (`:131`), wrapping visualization beyond ±32767 cells.
3. **AmortizedTask's lifetime rule lives in a caller comment.**
   `SubmitTick` stores a raw `this` in the worker pool's task ring
   (`SkullbonezSource/Core/AmortizedTask.h:64`). Destroying the task while
   `m_inFlight` is true dangles that pointer; the only guard is a comment at
   the single call site (`SkullbonezSource/Runtime/Replay/RunReplayTools.cpp:3864`).
   The type has no destructor check, and `Reset()` silently no-ops when
   in flight, so a caller cannot distinguish "reset" from "refused".
4. **Dead exception plumbing in a no-exceptions engine.**
   `WorkerPool::ParallelForChunksNoAlloc` retains `try/catch`,
   `std::exception_ptr`, and `std::rethrow_exception`
   (`SkullbonezSource/Core/WorkerPool.h:279-294`). The `std::function`
   removal eliminated the plausible thrower, and policy forbids user chunk
   functions from throwing. The plumbing is dead weight with policy tension.

## Goal

Each contract above is enforced by the code that owns it: the log survives a
worker-side fatal, the broadphase rejects non-finite input loudly, destroying
an in-flight task is a named fatal instead of memory corruption, and the
worker pool contains no exception machinery.

## Non-Goals

- No logging redesign (no async log, no new sinks); Release stays a no-op.
- No broadphase behavior change for valid input — physics baselines must
  remain byte-exact.
- No resurrection of the owner-parked comment-rot sweep; new code carries the
  comments the comment standard requires for new code, but no existing prose
  is reworked beyond lines the code changes force.

## Phases

- [ ] **E1 — EngineLog thread safety.** Add a mutex guarding map lookup/insert
  and file writes in `EngineLog` (Debug-only builds already scope the cost),
  or — if the owner prefers zero contention — an asserted main-thread-affinity
  contract plus a dedicated lock-free crash lane used by `FatalError`. Either
  way `FatalError`'s write/flush must be safe from any thread. State the
  chosen invariant in `Log.h`. Acceptance: a doctest exercising concurrent
  `Writef`/`WriteEventf` from multiple threads passes under the ASan lane
  (`tools\validate_native_diagnostics.bat` healthy run), and a worker-thread
  `SB_FATAL` path is exercised by test or recorded manual proof.
- [ ] **E2 — SpatialGrid input validation.** In `InsertBounds`/`InsertSwept`,
  validate all six bound components are finite and inside a named
  world-extent constant before converting to cell coordinates; violation is
  Lane F (`SB_FATAL` with body index and offending values) — a non-finite
  position is engine-state corruption, not recoverable input. Document the
  `int16_t` visualization wrap or clamp it at the same boundary. Acceptance:
  a focused CPU test proves NaN and >extent positions fatal with the named
  diagnostics; `tools\validate_physics.bat` stays byte-exact (guards must not
  perturb valid-input arithmetic).
- [ ] **E3 — AmortizedTask lifetime and Reset contract.** Destructor fails
  fatally when `IsInFlight()` (Lane F: dangling worker pointer imminent).
  `Reset()` returns bool (or asserts) so refusal is observable. Audit the
  single consumer (`RunReplayTools` prediction build) still satisfies the
  order; keep its wait-before-destroy behavior. Acceptance: CPU test covers
  reset-while-idle succeeding and documents the destructor guard; worker
  self-test still passes.
- [ ] **E4 — Remove worker-pool exception plumbing.** Delete the
  `try/catch`/`exception_ptr`/`rethrow` machinery from
  `ParallelForChunksNoAlloc` and `ParallelForChunksState`; submission failure
  paths become fatal or impossible-by-construction. Remove now-unused
  `<exception>` includes. Acceptance: worker self-test and all CPU suites
  pass; no `throw`, `catch`, or `rethrow` remains under `Core/WorkerPool*`.
- [ ] **E5 — Review and gates.** Independent review of the four contract
  boundaries (no new callback seams, no allocation-policy violations, lanes
  named correctly), comment-style audit limited to touched files, then final
  validation per the map below.

## Dependencies And Decisions

- Independent of all other plans; validation-gate V3 remains externally
  blocked and is unaffected.
- E1 open decision (small): mutex versus asserted-affinity + crash lane.
  Default to the mutex unless profiling shows contention — Debug-only cost.
- E2 world-extent constant needs an owner-visible home (proposal: alongside
  the existing broadphase capacity derivation in `SpatialGrid.h`, sized from
  the largest authored scene extent with generous margin, e.g. ±100,000
  world units).

## Acceptance

All phase acceptances; physics baselines byte-exact; the concurrent-log test
runs clean under ASan; no exception machinery remains in the worker pool.

## Validation

Core/threading and physics scope: `tools\validate_all_cpu_tests.bat` for the
new/changed CPU tests, `tools\validate_physics.bat` for E2 byte-exactness,
one healthy `tools\validate_native_diagnostics.bat` ASan lane for E1, and a
final `tools\validate_full.bat` (broad core scope). No DX12 source is in
scope; if any DX12 file ends up touched, add `tools\validate_dx12_renderer.bat`
plus `tools\run_graphics_stress.bat 1` with recorded evidence per MASTER
rule 10.
