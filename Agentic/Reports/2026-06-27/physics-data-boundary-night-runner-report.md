# Physics Data Boundary Night Runner Report

Date: 2026-06-27  
Branch: `Night-Runner-27th-June`  
Plan: `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`  
Impact area: physics, game object storage, runtime tools, scene setup, replay, diagnostics

## Outcome

The physics step, wake, sleep, and impulse boundary no longer accepts
`GameModelCollection&`. `GameModelCollection` remains the compatibility facade,
but it now constructs a narrow `PhysicsModelView` and passes body handles into
`PhysicsEngine`, `PhysicsScene`, `PhysicsWorld`, diagnostics, sleep propagation,
ragdoll constraint solving, and persistent contact solving.

This preserves the legacy deterministic model order while removing the broad
scene/render/editor collection type from the physics stepping surface.

## Authority Table

| Area | Current owner after this slice | Compatibility still present |
|------|--------------------------------|-----------------------------|
| Physics body identity | `PhysicsBodyHandle` in `PhysicsBodyStore` records | Handles still map to legacy model indices for this migration step. |
| Collider identity | `PhysicsColliderHandle` in `ColliderStore` records | Collider records still refresh from model-order collision shapes. |
| Render instance identity | `RenderInstanceHandle` in `RenderInstanceStore` records | Production rendering still consumes the legacy render scene view. |
| Step boundary | `PhysicsModelView` plus `PhysicsBodyStore` | The view borrows model order and cache invalidation from the facade. |
| Wake/sleep/impulse commands | `PhysicsBodyHandle` routed through `GameModelCollection` facade helpers | Runtime/editor/scene code still starts from model indices where UI/replay stores them. |
| Diagnostics | `PhysicsDiagnosticsSink` receives `PhysicsModelView` | SkullScope emission still calls back into the compatibility facade in Debug. |
| Fixed tree release | `PhysicsModelView` callback | The actual tree release is still a `GameModelCollection` responsibility. |

## Dependency Inventory

Removed from the physics step boundary:

- `PhysicsEngine` no longer declares or defines step, wake, sleep, or impulse
  APIs that take `GameModelCollection&`.
- `PhysicsScene` no longer declares or defines refresh, step, wake, sleep, or
  impulse APIs that take `GameModelCollection&`.
- `PhysicsWorld` no longer declares or defines solver, diagnostics, sleep,
  wake, tornado, or point-joint helper APIs that take `GameModelCollection&`.
- `PersistentContactSolver`, `SleepIslandSystem`, `PhysicsDiagnosticsSink`, and
  `Ragdoll::SolvePointJoints` now receive `PhysicsModelView&`.

Remaining allowed compatibility dependencies:

- Physics debug visualizers still render from `GameModelCollection`.
- `Ragdoll::AddSimpleHumanoid` still creates legacy models and constraints.
- `SimulationSystem` still has a legacy input struct that borrows
  `GameModelCollection`.
- Comment-only references document the migration state.

## Command And Query Boundary

Command call sites moved off direct `PhysicsEngine` collection APIs:

- Runtime fixed-step execution calls `GameModelCollection::RunPhysics`.
- Replay prediction calls `GameModelCollection::RunPhysics`.
- Runtime/editor wake operations call `GameModelCollection::WakeModel`.
- Scene authored/generated pending impulses call
  `GameModelCollection::SetPendingBodyImpulse`.
- Mouse-pickup and ray-test impulses call
  `GameModelCollection::ApplyBodyImpulse`.
- Object placement and ragdoll setup sleep seeding call
  `GameModelCollection::SeedModelAsleep`.

Read/query boundaries already available for follow-up slices:

- `PhysicsBodyStore`, `ColliderStore`, and `RenderInstanceStore` expose handle
  lookup and read-only record views.
- `PhysicsApi.h` names the public command/query structs that future scene,
  replay, rendering, and tool migrations should use.

## Determinism And Identity Notes

- Body iteration order is unchanged: compatibility handles use the current model
  index plus `PHYSICS_COMPATIBILITY_HANDLE_GENERATION`.
- `PhysicsBodyStore::ModelIndexForHandle` is the only conversion used by the
  public wake/sleep/impulse facade before mutating store state.
- `PhysicsWorld::RunPhysics` still writes back through `PhysicsBodyStore` so
  non-migrated render, replay, and tool callers continue to see the same model
  state after each fixed step.
- No physics baselines were updated.
- SkullScope was not used for this slice, so there are no trace commands or
  model-ingested diagnostic artifact sizes to report.

## Boundary Guardrail

`tools/check_runtime_boundaries.py` continues to reject new non-allowlisted
`GameModelCollection` dependencies under `SkullbonezSource/Physics` after
stripping comments. A logged direct run after the boundary migration reported:

```text
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
PASS: Runtime boundary validation passed.
CHECK_RUNTIME_BOUNDARIES_EXIT=0
CHECK_RUNTIME_BOUNDARIES_SECONDS=3.019
Log: TestOutput\validation\night_runner_physics_boundary_runtime_boundaries.log
```

After the first rubber-duck review, the active allowlist was tightened to only
the currently remaining debug, ragdoll-creation, and SimulationSystem
compatibility dependencies. The checker now has synthetic tests that reject the
old `PhysicsEngine::Step(GameModelCollection&)` and
`PhysicsWorld::RunPhysics(GameModelCollection&)` signatures:

```text
Runtime boundary summary: TestOutput\validation\runtime_boundaries\summary.json (0 errors)
PASS: Runtime boundary validation passed.
CHECK_RUNTIME_BOUNDARIES_EXIT=0
CHECK_RUNTIME_BOUNDARIES_SECONDS=3.143
Log: TestOutput\validation\night_runner_physics_boundary_runtime_boundaries_final.log
```

## Validation Evidence

Focused iteration build:

```text
tools\validate_build.bat Profile
PASS: Build Profile|x64 succeeded.
Build succeeded.
0 Warning(s)
0 Error(s)
Log: TestOutput\validation\night_runner_physics_boundary_profile_build_02.log
Elapsed: 17.006s
```

Project-filter coverage for the new header:

```text
Project filter summary: TestOutput\validation\project_filters\summary.json (0 errors, 483 project items, 483 filter items)
PASS: Project filter validation passed.
VALIDATE_PROJECT_FILTERS_EXIT=0
VALIDATE_PROJECT_FILTERS_SECONDS=0.884
Log: TestOutput\validation\night_runner_physics_boundary_project_filters.log
```

Whitespace check:

```text
git diff --check
Exit: 0
```

An earlier focused build failed while converting call sites; those failures were
fixed before this report:

```text
Log: TestOutput\validation\night_runner_physics_boundary_profile_build_01.log
Exit: 1
```

Final validation:

```text
tools\validate_fast.bat
PASS: All source files correctly formatted.
PASS: Project filter validation passed.
PASS: Runtime boundary validation passed.
Build succeeded.
0 Warning(s)
0 Error(s)
VALIDATE_FAST_EXIT=0
VALIDATE_FAST_SECONDS=16.091
Log: TestOutput\validation\night_runner_physics_boundary_validate_fast_final.log
```

```text
tools\validate_physics.bat
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_PHYSICS_EXIT=0
VALIDATE_PHYSICS_SECONDS=75.437
Log: TestOutput\validation\night_runner_physics_boundary_validate_physics.log
```

```text
tools\validate_full.bat
DX12 validation errors: 0
PASS: DX12 screenshots match committed baselines.
PASS: physics_regression_solver.csv (20001 lines, byte-exact match)
VALIDATE_FULL_EXIT=0
VALIDATE_FULL_SECONDS=27.737
Log: TestOutput\validation\night_runner_physics_boundary_validate_full.log
```

```text
tools\validate_perf.bat
VALIDATE_PERF_EXIT=0
VALIDATE_PERF_SECONDS=22.270
Log: TestOutput\validation\night_runner_physics_boundary_validate_perf.log
```

Perf warning acceptance:

- The perf script completed with exit 0, but it is not clean perf evidence.
- A repeat run reproduced the same warning shape:
  `TestOutput\validation\night_runner_physics_boundary_validate_perf_rerun.log`,
  `VALIDATE_PERF_RERUN_EXIT=0`, `VALIDATE_PERF_RERUN_SECONDS=21.796`.
- The DX12 perf comparison was skipped by the script because this machine label
  differs from the committed baseline machine.
- The `physics_bench` warning is accepted for this boundary-only slice because
  the total physics step marker stayed within noise in both runs:
  `Frame/Physics` avg +3.6%, p50 +4.9% in the first run; avg +2.9%, p50 +0.9%
  in the repeat run.
- The listed `physics_bench` failures are not caused by the migrated
  `GameModelCollection` to `PhysicsModelView` step boundary:
  `Frame/Render` is renderer work, `Frame/VsyncWait` is wait time,
  `Frame/Input` is input/replay UI work, and the memory delta exists at process
  start/restart/end rather than being allocated by the new stack-only
  `PhysicsModelView` adapter.
- The low-level physics submarkers with large percentages are tiny absolute
  costs (`ApplyForces` 0.0027 ms to 0.0076 ms, `Broadphase` 0.0031 ms to
  0.0064-0.0067 ms) and did not trip the script's failure list. They are
  recorded as residual perf watch points, not release blockers for this
  ownership-boundary PR.
- No code change in this slice touches render submission, GPU work, vsync
  policy, replay UI feature shape, process startup allocation policy, or perf
  baseline files.

## Checklist Reconciliation

- [x] Startup contract was run before source edits.
- [x] Pre-existing dirty files were checked; Plan 1 was cleanly committed before
      Plan 2 edits began.
- [x] The existing authority inventory was read and refreshed in this report.
- [x] `GameModelCollection&` dependencies in the physics step boundary were
      inventoried and removed.
- [x] Mutable direct command call sites touched by this boundary were routed
      through facade command helpers.
- [x] Current model-index iteration order and replay-body-id compatibility
      assumptions are recorded above.
- [x] Boundary validation rejects new non-allowlisted physics dependencies on
      `GameModelCollection`.
- [x] Comment-style audit is recorded in
      `Agentic/Reports/2026-06-27/physics-data-boundary-comment-audit.md`.
- [x] Source plan checklist is reconciled in
      `Agentic/Plans/engine-evaluation-fix-02-physics-data-boundary-plan.md`.
- [x] Final validation gates passed for fast, physics, and full. Perf completed
      with exit 0 and a documented warning acceptance.

## Residual Follow-Up

The next storage slices should make `PhysicsBodyStore`, `ColliderStore`,
`RenderInstanceStore`, and scene/entity metadata fully authoritative, then
delete the compatibility view. This slice intentionally avoids solver math,
body ordering, contact ordering, baseline refreshes, and broad ECS work.
