# Game Model Data Boundary Phase 0 Inventory

Date: 2026-06-25  
Branch: `nightrunner-25th-june`  
Plan: `Agentic/Plans/game-model-data-boundary-plan.md`  
Status: Phase 0 inventory complete; implementation phases remain open.

## Scope

This inventory classifies `GameModel` and `GameModelCollection` authority before
stable handles and authoritative body/collider/render/entity stores are moved
out of the compatibility facade.

Documentation-only validation: none required.

## GameModel Authority Table

`SkullbonezSource/GameObjects/GameModel.h` currently mixes all major world-data
authority categories in one object.

| Authority | Current State |
|-----------|---------------|
| Physics body | `m_physicsInfo`, `m_isFixed`, and `BallPhysicsCache` mass, inverse-mass, inertia, velocity, and damping data |
| Collider | `m_boundingVolume`, cached radius, volume, projected-area, and drag scalars |
| Terrain/contact transient | `m_terrain`, `m_responseInformation`, `m_isResponseRequired` |
| World-force dependency | Borrowed `m_worldEnvironment` pointer |
| Render instance | Tint channels, color override, `m_renderMaterial`, fixed-contact highlight data |
| Fixed/contact behavior | Contact-release flag and impulse threshold |
| Replay identity | `m_replayBodyId` |
| Scene/entity metadata | `m_collectionKind`, root/part indices, and `m_name` |
| Compatibility/cache | Duplicated projection/drag data and the legacy terrain-response mailbox |

## GameModelCollection Facade

`SkullbonezSource/GameObjects/GameModelCollection.h` currently owns:

- `std::vector<GameModel>`
- `GameModelSoACache`
- `Physics::PhysicsEngine`
- replay body-id allocation
- render scene view implementation
- physics body, collider, render-instance, and stream cache synchronization

The existing `PhysicsBodyStore`, `ColliderStore`, `RenderInstanceStore`, and
stream cache are useful migration footholds, but they still mirror
`GameModelCollection` order rather than owning authoritative state.

## Direct Dependency Inventory

| Area | Representative Dependencies |
|------|-----------------------------|
| Physics | `PhysicsEngine.h`, `PhysicsScene.h`, `PhysicsWorld.h`, `PersistentContactSolver.h`, `SleepIslandSystem.h`, `Ragdoll.h`, `SimulationSystem.h` |
| Rendering | `GameModelRenderer.h`, `RuntimeRenderInputs.h`, `RuntimeRenderHost.h`, `Run.h` |
| Scene/tools | `SceneGeneratedSetup.h`, `SceneAuthoredSetup.h`, `SceneSnapshotWriter.h`, editor and launcher mutation clusters in runtime tool code |
| Replay | `ReplayRecorder.h`, `ReplayRuntime.h` render pose and sample restore APIs |
| Diagnostics | `DiagnosticsRuntime.h`, `RuntimeDiagnostics.h`, `SkullScope.h`, `PhysicsDiagnosticsSink.h` |

## Command Seams

Future migration should prefer explicit commands for:

- body, collider, render-instance, and entity creation
- pose, velocity, shape, material, and name mutation
- wake, sleep, apply impulse, and pending impulse operations
- physics stepping
- joint and ragdoll creation
- replay restore and render-pose override
- diagnostics path/suppression changes

## Query Seams

Future migration should prefer read-only views for:

- body/collider/render instance data
- replay-id to body/entity mapping
- scene metadata, names, groups, and collection roots
- raycast and broadphase queries
- diagnostics snapshots and memory stats
- kinetic energy and shadow bounds

`SkullbonezSource/Physics/PhysicsApi.h` already declares much of the desired
physics command/query contract and should be reused instead of inventing a
parallel facade.

## Guardrail Candidates

- Add a `check_game_model_boundaries.py` style validator once handles exist.
- Initially report, then fail on new `GameModelCollection&` or
  `GameModelCollection*` dependencies in physics-layer APIs.
- Block new `PhysicsModels()` usage outside approved compatibility zones.
- Block renderer entry points that take `GameModelCollection&` after render
  instances become authoritative.
- Block replay/tool APIs that expose raw model indices after handles land.
- Add debug mapping assertions for model index, replay id, body handle, collider
  handle, render instance, and entity id.

## Validation Impact

- This report is documentation-only: no validation required.
- Stable handle scaffolding: `tools\validate_fast.bat` and
  `tools\validate_physics.bat`.
- Physics storage movement: `tools\validate_physics.bat`; add
  `tools\validate_perf.bat` when hot-loop iteration changes.
- Render projection movement: `tools\validate_dx12_renderer.bat`.
- Broad scene/tool/replay migration: `tools\validate_full.bat`.
- Validator tooling: `tools\validate_fast.bat` plus the changed script.
