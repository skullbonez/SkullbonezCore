# Downward Domain Bleed DB2 — Physics-Owned Terrain Boundary

Date: 2026-07-26

Plan: `Agentic/Plans/TODO/downward-domain-bleed-remediation.md`

Result: Complete (DB2 of 6; portfolio ledger 4/19, 21%)

## Outcome

Physics now consumes terrain through `PhysicsTerrainView`, a Physics-owned
value/borrow contract over immutable analytic-slope values or a scene-lifetime
span of precomputed `PhysicsTerrainCell` planes. The view owns bounds checks,
cell/triangle selection, height sampling, and maximum-height access without a
World type, owner pointer, callback, or virtual interface.

`PhysicsWorld` retains the one active terrain view. `SceneWorld::ReplaceTerrain`
revokes that view before replacing `SceneTerrain`, then publishes the new view
afterward. Body descriptors, body records, hot fields, and contact records no
longer carry a `Geometry::Terrain*`; fixed-step stages receive the same detached
view explicitly. The collision arithmetic and historic swapped post mapping
remain in their original operation order.

The physics-only `TerrainSupportClassifier` moved from World into Physics.
All nine unused `Assets/AssetKeys.h` rows registered by DB0 were removed, so
Physics has zero World, Scene, or Assets includes and zero
`Geometry::Terrain` spellings.

## Boundary And Lifetime Contract

- `PhysicsTerrainCell` contains the two precomputed split planes for one
  terrain quad.
- `PhysicsTerrainView` contains only spans and scalar sampling values.
- `Terrain::PhysicsView()` publishes either the analytic flat-slope values or
  the row-major collision-cell span.
- `PhysicsEngine::SetTerrainView` and `ClearTerrainView` forward the value to
  the solver owner.
- `SceneWorld::ReplaceTerrain` is the single replacement transaction and
  prevents a retained span from outliving its backing cell storage.
- Force, integration, narrowphase, terrain-contact, wake, and sleep-island
  stages consume the scene view without storing a per-body terrain field.
- `TerrainContactBodyView` is a per-call value snapshot; the manifold layer
  retains neither the view nor the collision shape.

## Determinism Proof

`tools\validate_physics.bat` passed on the final source. The generated
`Debug/physics_regression_varied.csv` contains two complete 44,401-line runs
(88,802 lines total). `check_physics_regression.py` accepted them only after
proving the two runs byte-identical, canonicalizing one complete run, and
comparing it byte-for-byte with the committed baseline.

No baseline, golden, replay artifact, screenshot, shader, scene, config, or
physics CSV reference was refreshed.

The focused test
`Physics terrain view: analytic and cached-cell sampling stay detached from World`
constructs analytic and cell-grid views directly, checks exact samples and
bounds, and exercises swept terrain contact without linking a World terrain
implementation.

## Static Proofs

```text
tools\validate_dependency_graph.bat
PASS: 27 include rules, 43 negative edge fixtures, 1 project fixture,
      and repository scan with zero findings

rg -n '^#include[[:space:]]+.*(World|Scene|Assets)/' SkullbonezSource/Physics
PASS: no rows

rg -n 'Geometry::Terrain' SkullbonezSource/Physics
PASS: no rows

rg -n '(Geometry::)?Terrain[[:space:]]*\*' SkullbonezSource/Physics
PASS: no rows

rg -n '(desc|record|cold)(->|\.)terrain\b' \
  SkullbonezSource/Physics SkullbonezSource/Runtime SkullbonezTests
PASS: no rows

rg -n '^#include[[:space:]]+.*Runtime/Replay/' \
  SkullbonezSource/Physics SkullbonezSource/Rendering \
  SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
PASS: no rows
```

## Validation

| Gate | Result |
|---|---|
| Focused Physics terrain-view test | PASS; 1 case / 12 assertions |
| `tools\validate_tests.bat` | PASS; 392 cases / 2,403,298 assertions |
| `tools\validate_physics.bat` | PASS on final source; lifecycle smoke, byte-exact 44,401-line oracle, ready builds |
| `tools\validate_perf.bat` | PASS; allocation policy, runtime perf probes, and physics benchmark report no regression |
| `tools\validate_dependency_graph.bat` | PASS; zero repository findings |
| `python tools/check_allocation_policy.py` | PASS; zero allowlist errors |
| `git diff --check` | PASS |

The perf gate exposed one DB1 representation defect: Prediction's fixed
513,000-float retained-record arena was declared as a growing-vector type.
DB2 corrected it to one startup-allocated fixed-capacity array behind
`std::unique_ptr<float[]>`, with no capacity-changing API and an explicit
2,052,000-byte startup allowlist cap. This adds no replay growth privilege.
The wider Profile build also found one stale Automation fingerprint read of
the deleted per-body terrain field; it now reads the same availability bit
from `SceneWorld`'s terrain owner.

## Comment Audit Checklist

The repository comment-audit skill inspected every hand-authored
source-bearing path in the final DB2 diff. It verified learning headers,
resolved every repository-relative `Related:` entry, corrected stale terrain
pointer ownership claims, and checked the terrain-view lifetime and
byte-sensitive arithmetic comments against the final call path.

- [x] `SkullbonezSource/Physics/ObjectContactManifold.cpp`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- [x] `SkullbonezSource/Physics/PhysicsApi.h`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h`
- [x] `SkullbonezSource/Physics/PhysicsEngine.cpp`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.cpp`
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/PhysicsWorld.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStageContexts.h`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.cpp`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.h`
- [x] `SkullbonezSource/Physics/TerrainSupportClassifier.h`
- [x] `SkullbonezSource/Runtime/App/Run.cpp`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorHistory.cpp`
- [x] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp`
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h`
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp`
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp`
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp`
- [x] `SkullbonezSource/World/Terrain.cpp`
- [x] `SkullbonezSource/World/Terrain.h`
- [x] `SkullbonezSource/World/TerrainSupportClassifier.h` — moved to Physics
- [x] `SkullbonezTests/TestCoverageFloorContracts.cpp`
- [x] `SkullbonezTests/TestDeterminism.cpp`
- [x] `SkullbonezTests/TestPhysicsHandles.cpp`
- [x] `SkullbonezTests/TestPhysicsStageState.cpp`
- [x] `SkullbonezTests/TestTerrain.cpp`

Checklist path: this report. Checked: 46. Deferred: 0. Unchecked: 0.
