# Physics Body Store Aggressive Ownership Plan

Date: 2026-06-26
Status: Draft next implementation slice
Impact area: physics body ownership, activation/sleep, pose and velocity state
Validation for this document-only change: none required

## Goal

Move real runtime authority for activation, sleep, position, and velocity out of
`GameModel` and into `PhysicsBodyStore`.

This is the aggressive slice: stop adding scaffolding and make the physics store
the state that `PhysicsWorld` reads and mutates during stepping. `GameModel`
stays only as a compatibility presentation/writeback target until render,
replay, tools, and scene code move to store/handle APIs.

## Non-Goals

- Do not change solver math.
- Do not move collider shape ownership in this slice.
- Do not change mass, inertia, restitution, drag, or broadphase semantics except
  where pose/velocity ownership requires read access.
- Do not refresh physics baselines as cleanup.
- Do not add another identity layer.

## Rules

1. `PhysicsBodyStore` is the owner during the physics step.
2. `GameModel` is loaded into the store at scene/reset boundaries and written
   back after the step for compatibility.
3. No permanent double-authority loop. Any store-to-model sync must be named as
   compatibility writeback and be easy to delete.
4. Preserve model-order iteration until a separate deterministic ordering change
   is intentionally validated.
5. No per-frame allocations or map scans in the solver hot path.
6. Physics CSV must remain byte-exact unless a deliberate behavior change is
   approved.

## Implementation Steps

1. Extend `PhysicsBodyRecord` to hold full mutable body step state:
   - position,
   - orientation,
   - linear velocity,
   - angular velocity,
   - sleep/activation flags,
   - pending force/impulse fields that are currently read from `GameModel`.
2. Add explicit store APIs:
   - `MutableRecords()` or narrower mutable access for `PhysicsWorld`,
   - `LoadFromModels(...)` for compatibility input,
   - `WriteBackToModels(...)` for compatibility output,
   - handle-based `WakeBody`, `SeedBodyAsleep`, `ApplyBodyImpulse`, and pending
     impulse commands.
3. Change `PhysicsWorld::RunPhysics()` to read and mutate body records for:
   - activation/wake/sleep state,
   - position integration,
   - orientation integration,
   - linear and angular velocity updates,
   - force and impulse application.
4. Keep existing `GameModelCollection&` entry points temporarily, but make them
   wrappers that pass body-store access into the solver and then write back to
   `GameModel`.
5. Delete or downgrade the old `PhysicsWorld` sleep arrays once store sleep state
   is authoritative. If a mirror remains temporarily, name it compatibility and
   assert it matches the store.
6. Update diagnostics and Debug assertions to compare store state against
   writeback state after the step, not before.
7. Extend the runtime boundary checker only if new compatibility APIs are added.

## Cost Budget

- Allowed short-term cost: one compatibility writeback pass after each physics
  step while legacy render/replay/tool code still reads `GameModel`.
- Not allowed: solver hot-loop heap allocations, per-body associative lookups,
  permanent store/model resync every time a caller asks for state, or extra
  render-store refreshes caused by the ownership change.
- Exit pressure: once render/replay/tools read store-backed views, remove the
  writeback from normal runtime paths.

## Validation

Documentation-only planning requires no validation.

For the implementation commit:

- Minimum gate: `tools\validate_physics.bat`
- Add `tools\validate_perf.bat` if solver hot-loop layout, broadphase inputs, or
  per-frame allocation behavior changes.
- Add `tools\validate_full.bat` if scene load, replay restore, render
  presentation timing, or compatibility writeback timing changes beyond the
  physics step.

## Done Criteria

- `PhysicsWorld` no longer reads or mutates `GameModel` for activation, sleep,
  position, orientation, linear velocity, or angular velocity during the step.
- `GameModel` receives those fields only through a named compatibility writeback.
- Handle-based body commands work for wake/sleep/impulse paths used by the
  migrated step.
- `physics_regression_solver.csv` remains byte-exact.
- No new baseline files are committed.
- This plan moves to `Agentic/Plans/Done/` only after the implementation is
  committed and validated.
