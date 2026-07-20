# Physics Settings Snapshot

Status: In progress — 3/4 tasks (C0-C3)
Owner: repository owner; registered 2026-07-20 as campaign plan 3 of 8
Evidence: `../../Reports/2026-07-20/engine-architecture-review.md` (finding B)
Ledger: C0-C3
Depends on: `physics-facade-unification` (the stamp point is the surviving
PhysicsEngine); `dependency-direction-restoration` recommended first.

## Objective

Cut the one `EngineConfig` edge that matters: physics solver stages must not
see the Core process-config type. A physics-owned settings value record is
stamped once at `ApplyRuntimeConfig` time and threaded through
`RunPhysics`/stage contexts, replacing `const EngineConfig&` in every
fixed-step signature. Byte-exact baselines are the faithfulness oracle.

## Problem / Evidence

`Core/Config.h` is 595 lines / ~266 fields / 75 includers. It is threaded by
reference into the hot path: `PhysicsWorld::RunPhysics` takes
`const EngineConfig&`; solver code reads
`config.persistentContactSolver.iterations` and
`config.terrainContact.maxBaumgarteBias` at solve time
(`PersistentContactSolver.cpp:165-193`); eleven stage headers name
`EngineConfig`. The contact solver can currently see skybox texture paths.

## Non-Goals

- No decomposition of `EngineConfig` for renderer, UI, camera, or window
  consumers — this plan cuts the physics edge only.
- No tuning-value change of any kind: every snapshot field carries the exact
  value the same code read from `EngineConfig` before. Byte-exact CSV is the
  proof; a mismatch is a defect, never a baseline refresh.
- No engine.cfg schema change, no config format version bump.
- The snapshot is a domain value record, not a `*Runtime`/`*Compatibility`
  bag: fields group by owner (solver, sleep, broadphase, forces, terrain
  contact, fluid) per the Migration Cleanup Review Rule's domain-noun
  requirement.

## Binding Decisions

1. The record is named `PhysicsRuntimeSettings` (composed of existing or new
   domain sub-values such as `ContactSolverSettings`, `SleepSettings`,
   `BroadphaseSettings`, `FluidForceSettings`, `TerrainContactSettings`), is
   owned by `Physics/`, and includes nothing from `Core/Config.h`.
2. It is stamped exactly once per config application in
   `PhysicsEngine::ApplyRuntimeConfig` (and re-stamped on any existing
   runtime-config reapply path); fixed-step code never consults
   `EngineConfig`.
3. Field-by-field provenance is recorded: each snapshot field names the
   `EngineConfig` field it copies. The C2 unit test enforces the mapping.
4. Value semantics only; no pointers back into config storage.

## C0 Field Inventory

Inventory completed 2026-07-20 from the current CodeGraph plus exact
`EngineConfig`, field-access, parameter, and include searches under
`SkullbonezSource/Physics/`.

### Proposed Physics-Owned Shape

`PhysicsRuntimeSettings` is a value-only composite owned and stored by
`PhysicsEngine`:

| Sub-value | Responsibility | Field count |
|---|---|---:|
| `PhysicsMaterialSettings` | Authored drag/friction and solver friction response. | 4 |
| `BodySimulationSettings` | Authored angular cap, restitution threshold, and contact skin. | 3 |
| `ContactSolverSettings` | Object-contact PGS correction and iteration policy. | 4 |
| `TerrainContactSettings` | Terrain generation/correction policy. | 4 |
| `SleepSettings` | Speed thresholds and consecutive-frame policy. | 3 |
| `BroadphaseSettings` | Spatial-grid cell size. | 1 |
| `PhysicsExecutionSettings` | Master and per-stage worker switches. | 7 |
| `WorldForceSettings` | Gravity value currently re-read by contact solving. | 1 |
| **Total** | | **27** |

Migration-vocabulary governance for the binding root name:

- Owner: `PhysicsEngine` in the Physics module.
- Reason: immutable owner-native input replacing the Core process-config type at
  every fixed-step seam; it is the final domain API, not compatibility storage.
- Deletion condition: replace the root only if physics tuning becomes entirely
  owner-native and no cold Core-to-Physics translation remains.
- Review evidence: C2 field-faithfulness tests plus C3 independent bag-shape,
  clamp, and exact-edge review.

### Exact Provenance And Semantics

Defaults are the current `Core/Config.h` defaults. “Current transform” records
the behavior that C1/C2 must preserve exactly; it does not authorize moving a
clamp twice.

| Physics field | Exact `EngineConfig` source | Default | Current consumers / transform |
|---|---|---:|---|
| `material.sphereDragCoefficient` | `physicsMaterial.sphereDragCoeff` | `0.4f` | `PhysicsMaterial::FromConfig`; direct copy into sphere authored descriptors. |
| `material.terrainFrictionCoefficient` | `physicsMaterial.frictionCoeff` | `0.1f` | `PhysicsMaterial::FromConfig` and persistent terrain rows; direct value. |
| `material.objectFrictionCoefficient` | `physicsMaterial.objectFrictionCoeff` | `0.1f` | Persistent object contacts; forced to zero only in elastic-collision mode. |
| `material.rollingFrictionCoefficient` | `physicsMaterial.rollingFrictionCoeff` | `0.02f` | Persistent rolling response; `max(0, value)` at use. |
| `body.angularVelocityLimit` | `bodySimulation.velocityLimit` | `5.0f` | `BodySimulationLimits::FromConfig`; direct authored-body copy. |
| `body.contactRestitutionThreshold` | `bodySimulation.contactRestitutionThreshold` | `2.0f` | Authored policy, terrain detection, and solver limits; forced to zero only for elastic mode. |
| `body.contactEpsilon` | `bodySimulation.contactEpsilon` | `0.05f` | Authored/contact policy and solver rows direct; `PhysicsWorld` derives contact skin with `max(0, value)`. |
| `solver.slop` | `persistentContactSolver.slop` | `0.005f` | Persistent solver; `max(0, value)`. |
| `solver.baumgarteBeta` | `persistentContactSolver.baumgarteBeta` | `0.2f` | Persistent solver; `max(0, value)`. |
| `solver.positionCorrectionPercent` | `persistentContactSolver.positionCorrectionPercent` | `0.35f` | Persistent solver; clamp to `[0,1]`. |
| `solver.iterations` | `persistentContactSolver.iterations` | `12` | Persistent solver; `max(1, value)`. |
| `terrain.threshold` | `terrainContact.threshold` | `0.15f` | `ContactPolicy::FromConfig` and terrain candidate body policy; direct value. |
| `terrain.slop` | `terrainContact.slop` | `0.005f` | Persistent terrain rows; `max(0, value)`. |
| `terrain.baumgarteBeta` | `terrainContact.baumgarteBeta` | `0.3f` | Persistent terrain rows; `max(0, value)`. |
| `terrain.maxBaumgarteBias` | `terrainContact.maxBaumgarteBias` | `2.0f` | Object-contact setup and terrain rows; `max(0, value)`. |
| `sleep.linearSpeed` | `physicsSleep.linearSpeed` | `0.5f` | Solver support/wake limits and `ResolveStepPolicy`; `max(0, value)` in the policy, raw-derived `*2` limit in solver. |
| `sleep.angularSpeed` | `physicsSleep.angularSpeed` | `0.3f` | Solver support/wake limits and `ResolveStepPolicy`; `max(0, value)` in the policy, raw-derived `*2` limit in solver. |
| `sleep.frames` | `physicsSleep.frames` | `30` | Cold seed count clamps `[0,255]`; fixed-step policy clamps `[1,255]`. Both distinct semantics must remain. |
| `broadphase.cellSize` | `broadphase.cellSize` | `24.0f` | Cold apply and same-state Debug oracle; `max(BROADPHASE_MIN_CELL_SIZE, value)`. |
| `execution.parallel` | `physicsExecution.parallel` | `true` | Master gate for every worker lane. |
| `execution.parallelApplyForces` | `physicsExecution.parallelApplyForces` | `true` | Force application dispatch; combined with master gate. |
| `execution.parallelMutualGravity` | `physicsExecution.parallelMutualGravity` | `true` | Mutual-gravity pair build; combined with master gate and size/thread thresholds. |
| `execution.parallelTornadoField` | `physicsExecution.parallelTornadoField` | `false` | Tornado gameplay dispatch; combined with master gate. |
| `execution.parallelNarrowphase` | `physicsExecution.parallelNarrowphase` | `false` | Narrowphase island dispatch; combined with master gate and pair/island thresholds. |
| `execution.parallelTerrainDetect` | `physicsExecution.parallelTerrainDetect` | `true` | Terrain detection dispatch; combined with master gate. |
| `execution.parallelIntegrate` | `physicsExecution.parallelIntegrate` | `true` | Remaining-time integration dispatch; combined with master gate. |
| `worldForces.gravity` | `worldForces.gravity` | `-30.0f` | Persistent solver support/friction/force estimates; `fabsf` at each magnitude use. |

`physicsMaterial.spinFrictionCoeff` is not read anywhere in Physics and is not
part of this snapshot. Adding it “for completeness” would create an unowned bag
field and weaken C2's exact mapping.

### Edge And Consumer Census

| Layer | Current `EngineConfig` edge | Fields actually read |
|---|---|---|
| `PhysicsEngine::ApplyRuntimeConfig` | Sole intended cold stamp point; delegates to three object-policy translators and `PhysicsWorld::ApplyRuntimeConfig`. | Material/body/contact-policy fields above. |
| `PhysicsWorld::ApplyRuntimeConfig` | Cold forwarding edge to broadphase and sleep owners. | `broadphase.cellSize`, `physicsSleep.frames`. |
| `PhysicsEngine::Step` → `PhysicsWorld::RunPhysics` → `RunSolverPhysics` | Whole process config threaded through the fixed step. | All hot fields below; C1 replaces the parameter with the stored Physics value. |
| `PhysicsBroadphaseStageContext` | Whole config borrowed by the hot stage. | `broadphase.cellSize`, `bodySimulation.contactEpsilon`. |
| terrain detection/commit contexts | Whole config borrowed by worker/serial terrain phases. | `terrainContact.threshold`, `bodySimulation.contactRestitutionThreshold`. |
| contact-solver stage contexts → `PersistentContactSolverContext` | Whole config borrowed through narrowphase/contact solve. | Solver, terrain, material, body, sleep, and gravity fields listed above. |
| `PhysicsSleepController` | Cold apply plus fixed-step `PhysicsSleepConfig` borrow. | `frames`, `linearSpeed`, `angularSpeed`. |
| force, narrowphase, terrain dispatch | Core `PhysicsExecutionConfig` subrecord borrowed directly. | Master plus the matching per-stage switch. |
| tornado gameplay context | Whole config borrowed solely for worker gating. | `parallel`, `parallelTornadoField`. |

Negative census findings are binding for C1:

- `BuoyancySystem` and fluid-force integration do not read `EngineConfig`;
  they already consume the Physics-owned `PhysicsWorldForces` value containing
  gravity, fluid surface, liquid/gas density, and angular drag. No duplicate
  fluid fields belong in `PhysicsRuntimeSettings`.
- Tornado field/system tuning is already carried by Physics-owned
  `TornadoFieldConfig`/`TornadoSystemConfig`; only execution switches cross the
  Core boundary.
- `SpatialGrid` does not read config. `PhysicsBroadphaseStage` owns and clamps
  the sole `broadphase.cellSize` provenance field.
- `PhysicsStageContexts.h` itself has no config field; the remaining config
  borrows are distributed across the broadphase, terrain, contact-solver,
  force, narrowphase, and tornado stage APIs named above.

## Tasks

- [x] C0 — Field inventory: enumerate every `EngineConfig` read inside
  `Physics/` (world, stages, solver, sleep, buoyancy, tornado-until-extracted,
  spatial grid), grouped by proposed owner sub-value, with the exact
  config-field provenance table. Output: table committed into this plan.
  No validation (documentation).
- [x] C1 — Introduce `PhysicsRuntimeSettings` + sub-values, stamp in
  `ApplyRuntimeConfig`, thread through `RunPhysics`, stage contexts
  (`PhysicsStageContexts.h`), and solver entry points; delete every
  `EngineConfig` include/parameter from `Physics/` fixed-step code. Proof:
  `grep -rn "EngineConfig" SkullbonezSource/Physics` returns only the
  cold `ApplyRuntimeConfig`/authoring stamp edges (target: PhysicsEngine
  only). Validation: `tools\validate_physics.bat` (byte-exact) and
  `tools\validate_perf.bat` (hot-path signature change).
- [x] C2 — Faithfulness tests: unit coverage asserting the stamped snapshot
  equals the source config field-for-field (including defaults and clamp
  behavior such as the existing `max(1, iterations)` / `max(0, slop)`
  guards, which must move or be provably duplicated, not silently doubled).
  Validation: `tools\validate_tests.bat`; add `validate_coverage` if these
  tests are intended to raise the physics-stores/stages floor.
- [ ] C3 — Closure: independent rubber-duck review (bag-shape check: the
  record must not have become a catch-all runtime bag; clamp/verbatim
  semantics preserved), final gates, provenance table reconciled against
  final source. Validation: `tools\validate_physics.bat`,
  `tools\validate_perf.bat`, `tools\validate_full.bat` at closure tip.

## C1 Evidence — 2026-07-20

- `PhysicsRuntimeSettings.h` now owns eight domain sub-values and the exact 27
  inventoried fields. `PhysicsEngine::RuntimeSettingsFromConfig` is the sole
  field-by-field conversion seam, and `ApplyRuntimeConfig` stores that value
  once before applying cold authored-body, broadphase, and sleep policy.
- `PhysicsEngine::Step`, `PhysicsWorld`, broadphase, force, narrowphase,
  terrain, contact-solver, sleep, tornado, live scene, replay prediction, and
  focused test fixtures consume Physics-owned settings or narrower sub-values.
- Exact negative proof:
  `rg -n "EngineConfig|Core/Config\\.h|PhysicsExecutionConfig|PhysicsSleepConfig" SkullbonezSource/Physics`
  returns only `PhysicsEngine.h/.cpp` declarations/definitions and the one
  `Core/Config.h` include needed by the cold converter.
- The touched-file comment-style audit inspected 32 existing source/tool files
  plus the new settings header: 33 checked, 0 deferred. Every file has
  the required learning-header sections; the stamp boundary adds a nearby
  `Concept:` comment and the new value header records ownership/lifetime
  invariants.
- Validation: `tools\validate_physics.bat` passed in 79.83 s with the 44,401-line
  varied-scene CSV byte-exact; `tools\validate_perf.bat` passed in 109.20 s
  with zero allocation violations and no DX12/physics-benchmark regression;
  the one-process replay visual-fidelity generation completed in 391.01 s and
  every documented offline/negative control passed; `tools\validate_full.bat`
  passed in 143.12 s with all CPU, coverage, runtime, DX12, and physics lanes.
- A pre-existing facade-closure allowlist row still named deleted
  `PhysicsScene` files. It was repaired separately in `7cd9ca5f`; self-test,
  repository scan, and `validate_fast` all pass with zero allowlist errors.

## C2 Evidence — 2026-07-20

- `TestPhysicsStageState.cpp` checks all 27 fields twice: once from default
  `EngineConfig` values and once from distinct custom values, including
  deliberately invalid negatives and out-of-range solver values. This proves
  the cold stamp is a verbatim provenance copy and does not normalize early.
- Existing sleep-policy coverage still pins non-negative speed thresholds,
  squaring, and the `[1,255]` frame clamp. A new direct persistent-solver probe
  proves negative slop/Baumgarte/bias, iteration counts below one, and both
  sides of the position-correction clamp produce exactly the same state as
  their normalized values at the use site.
- The touched-file comment-style audit inspected both changed test sources:
  2 checked, 0 deferred. Their learning headers and nearby invariants explain
  provenance, clamp ownership, determinism, and byte-exact baseline risk.
- `tools\validate_tests.bat` passed in 12.84 s: 327/327 cases and
  61,131/61,131 assertions. Coverage was not run separately because C2 pins
  behavior rather than raising a ratified coverage floor.

## Acceptance

- No fixed-step physics code names `EngineConfig`; the C1 grep proof holds
  at closure tip with only the recorded cold stamp edges.
- Physics regression CSV byte-exact; perf gate passes with no regression
  outside normal noise (record the numbers).
- C2 tests pass and pin the field mapping.
- Independent review clear on domain-noun grouping and clamp preservation.

## Validation Summary

C1: `validate_physics` + `validate_perf`. C2: `validate_tests`
(+ `validate_coverage` if floor-raising). C3: `validate_physics` +
`validate_perf` + `validate_full` at final source.
