# EngineConfig Decomposition

Date: 2026-07-11
Status: E3 complete — 3/5 phases checked (60%)
Impact area: `Core/Config.h`/`Config.cpp`, every config consumer, engine.cfg
Origin: 2026-07-11 architecture gap review. `EngineConfig` is the largest
remaining catch-all bag: window setup, asset paths, frustum, physics
defaults, audio, and art direction in one flat class. Partial decomposition
already exists (`WindowConfig`, `ContactAudioConfig`, `SceneLightConfig`,
`OrdinaryRenderConfig`, `CinematicRenderConfig`); this plan finishes the job
so every field lives in a domain struct with a named owner, per the repo's
own domain-nouns-over-bags migration rule.

## Scope decisions (binding)

- **Critical-path position.** Start after `dx12-post-final-cleanup.md` phase 5
  establishes the final cinematic/shadow config shape. Move that surviving
  shape directly into domain structs rather than reorganizing duplicate fields
  twice. Complete before shader modernization begins.

- **Same file format.** `engine.cfg` keys keep parsing exactly as today;
  this is an in-memory structure change plus parser table cleanup, not a
  config file migration (that belongs to `TODO/data-format-versioning.md`
  if cfg versioning is wanted).
- **Physics-default fields move in their own isolated commits** gated by
  `validate_physics` (byte-exact CSV) — gravity/fluid/drag/friction/sleep/
  solver/broadphase values are determinism-sensitive per `AGENTS.md`.
- **No global re-plumbing.** Consumers that already receive `EngineConfig&`
  keep receiving it; the win is that they read `config.physicsDefaults.x`
  instead of a flat field, and new systems can accept only their domain
  struct. Narrowing constructor signatures to domain structs is encouraged
  per touched owner but not a bulk sweep.
- **Table-driven parsing.** Key → field bindings become a declarative table
  per domain struct, replacing long if/else key matching, so adding a field
  is one row. `std::string` asset-path members stay (loaded pre-gameplay,
  cold path) unless the allocation checker objects.
- `TODO/dx12-post-final-cleanup.md` Phase 5 is a hard prerequisite. Fold its
  surviving shadow block and renamed sun fields into this inventory; do not
  execute this plan first or create an interim config decomposition.

## Phases

- [x] E1. Inventory: table in this plan listing every remaining flat
      `EngineConfig` field → target domain struct → owner → validation gate.
      Expected domains: `AssetPathsConfig`, `FrustumConfig` (or fold into
      camera), `PhysicsDefaultsConfig`, `RuntimeFlagsConfig`; extend the
      existing structs where a domain already exists. Documentation-only.
- [x] E2. Non-physics domains move (asset paths, frustum, runtime flags,
      remaining render odds and ends), one commit per domain, parser table
      updated in the same commit. Gate: `validate_fast`; `validate_dx12_renderer`
      if any render-consumed field moves.
- [x] E3. Physics defaults move, isolated commit(s). Gate:
      `validate_physics` byte-exact per commit.
- [ ] E4. Parser cleanup: single declarative key-binding table per domain;
      unknown-key handling unchanged; `Dump()` regenerated from the same
      table so dump and parse cannot drift. Gate: `validate_fast` +
      `validate_full` (Config.h is broad scope in the validation map).
- [ ] E5. Closure: no flat data field remains directly on `EngineConfig`
      (accessors/structs only); comment audit; rubber-duck review;
      `validate_full`; MASTER-PLAN/SessionState update; delete plan.

## E1 direct-field inventory (2026-07-12)

Reconciliation source: the declarations directly inside `EngineConfig` after
commit `e2bc8c8e`, every direct-field row returned by `ConfigSettings()`, and
targeted reads of current consumers. The result is exactly **81 direct data
fields matched one-to-one to 81 parser/dump rows**, with no unbound declaration
and no parser-only direct field. The six already-composed members below are not
part of that 81-field migration inventory.

The target structures deliberately use domain nouns. The 25 physics defaults
are split by policy owner rather than collected into a new catch-all
`PhysicsDefaultsConfig`. The four old `shadow_*` keys are isolated as
parser-only blob-shadow presentation settings rather than confused with the
live `ShadowQualityConfig` blocks.

| Current key | Direct field | Target domain struct | Concrete owner / consumer rationale | Physics-sensitive? | Required move gate |
|---|---|---|---|---|---|
| `sky_front` | `skyFront` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the front cube texture. | No | `validate_dx12_renderer` |
| `sky_left` | `skyLeft` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the left cube texture. | No | `validate_dx12_renderer` |
| `sky_back` | `skyBack` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the back cube texture. | No | `validate_dx12_renderer` |
| `sky_right` | `skyRight` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the right cube texture. | No | `validate_dx12_renderer` |
| `sky_up` | `skyUp` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the upper cube texture. | No | `validate_dx12_renderer` |
| `sky_down` | `skyDown` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` and `SkyBox` resolve the lower cube texture. | No | `validate_dx12_renderer` |
| `terrain_texture` | `terrainTexture` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` registers the terrain material texture. | No | `validate_dx12_renderer` |
| `sphere_texture` | `sphereTexture` | `AssetPathsConfig` | `AssetSystem::RegisterBuiltInSourceAssets` registers the generated-sphere texture. | No | `validate_dx12_renderer` |
| `terrain_raw` | `terrainRaw` | `AssetPathsConfig` | `Run`/`RunScene` load the heightfield used by both terrain rendering and collision queries. | **Yes**: selects collision geometry | `validate_dx12_renderer` + `validate_physics` |
| `frustum_near` | `frustumNear` | `CameraConfig` | `Init` establishes projection depth and `RuntimeRenderPasses` reconstructs depth with it. | No | `validate_dx12_renderer` |
| `frustum_far` | `frustumFar` | `CameraConfig` | Projection, depth post-processing, and `WorldEnvironment` water-mesh extent share this limit. | No | `validate_dx12_renderer` |
| `mouse_sensitivity` | `mouseSensitivity` | `CameraConfig` | `RunCameraState` converts mouse deltas into camera rotation. | No | `validate_dx12_renderer` |
| `key_speed` | `keySpeed` | `CameraConfig` | `RunCameraState` converts keyboard input into camera translation. | No | `validate_dx12_renderer` |
| `camera_tween_rate` | `cameraTweenRate` | `CameraConfig` | `RunCameraState` sets `CameraCollection` tween speed. | No | `validate_dx12_renderer` |
| `camera_collision_threshold` | `cameraCollisionThreshold` | `CameraConfig` | `Camera` clamps pitch against the collision threshold. | No | `validate_dx12_renderer` |
| `min_camera_height` | `minCameraHeight` | `CameraConfig` | `Camera`, `CameraCollection`, and `InputController` enforce the lower camera boundary. | No | `validate_dx12_renderer` |
| `max_camera_height` | `maxCameraHeight` | `CameraConfig` | `InputController` enforces the upper camera boundary. | No | `validate_dx12_renderer` |
| `min_view_mag` | `minViewMag` | `CameraConfig` | `Camera` clamps minimum eye-to-view distance. | No | `validate_dx12_renderer` |
| `max_view_mag` | `maxViewMag` | `CameraConfig` | `Camera` clamps maximum eye-to-view distance. | No | `validate_dx12_renderer` |
| `terrain_scale` | `terrainScale` | `TerrainGeometryConfig` | `Terrain` uses it for mesh positions, bounds, height lookup, normals, and collision-space sampling. | **Yes**: changes collision geometry | `validate_dx12_renderer` + `validate_physics` |
| `terrain_height_scale` | `terrainHeightScale` | `TerrainGeometryConfig` | `Terrain` applies it to render vertices and queried terrain heights. | **Yes**: changes collision heights | `validate_dx12_renderer` + `validate_physics` |
| `terrain_render_step_size` | `terrainRenderStepSize` | `TerrainGeometryConfig` | `Terrain` chooses its sampled grid step before building/querying the heightfield. | **Yes**: changes sampled terrain topology | `validate_dx12_renderer` + `validate_physics` |
| `skybox_render_height` | `skyboxRenderHeight` | `SkyboxConfig` | `RuntimeRenderPasses` translates the skybox around the camera. | No | `validate_dx12_renderer` |
| `skybox_overflow` | `skyboxOverflow` | `SkyboxConfig` | `SkyBox` expands cube-face geometry to prevent seams. | No | `validate_dx12_renderer` |
| `skybox_scale` | `skyboxScale` | `SkyboxConfig` | `RuntimeRenderPasses` scales skybox presentation. | No | `validate_dx12_renderer` |
| `game_model_capacity` | `gameModelCapacity` | `RuntimeCapacityConfig` | `Run` clamps startup capacity; scene, input, stress, and replay owners use the resulting topology bound. | **Yes**: bounds simulated topology | `validate_full` |
| `worker_threads` | `workerThreads` | `RuntimeCapacityConfig` | `Init`, scene overrides, and runtime tuning configure the shared `WorkerPool` capacity. | **Yes**: changes physics scheduling | `validate_full` |
| `physics_parallel` | `physicsParallel` | `PhysicsExecutionConfig` | `PhysicsWorld` master-gates parallel force, contact, terrain, and integration lanes. | **Yes**: changes solver scheduling | `validate_physics` |
| `physics_parallel_apply_forces` | `physicsParallelApplyForces` | `PhysicsExecutionConfig` | `PhysicsWorld` selects parallel body-force application. | **Yes** | `validate_physics` |
| `physics_parallel_tornado_field` | `physicsParallelTornadoField` | `PhysicsExecutionConfig` | `TornadoGameplay` selects parallel tornado force sampling. | **Yes** | `validate_physics` |
| `physics_parallel_narrowphase` | `physicsParallelNarrowphase` | `PhysicsExecutionConfig` | `PhysicsWorld` selects the parallel narrowphase/island lane. | **Yes** | `validate_physics` |
| `physics_parallel_terrain_detect` | `physicsParallelTerrainDetect` | `PhysicsExecutionConfig` | `PhysicsWorld` selects parallel terrain contact detection. | **Yes** | `validate_physics` |
| `physics_parallel_integrate` | `physicsParallelIntegrate` | `PhysicsExecutionConfig` | `PhysicsWorld` selects parallel body integration. | **Yes** | `validate_physics` |
| `shadow_parallel_prep` | `shadowParallelPrep` | existing `RuntimeRenderFlags` | `RuntimeRenderer`/`ShadowPass` select parallel shadow draw preparation; this extends the existing render-runtime flag owner. | No | `validate_dx12_renderer` |
| `replay_prediction_instant_budget_ms` | `replayPredictionInstantBudgetMs` | `ReplayPredictionConfig` | `RunReplayTools` chooses instant versus amortized prediction from the measured budget. | No: private prediction scheduling only | `validate_fast` |
| `replay_prediction_probe_ticks` | `replayPredictionProbeTicks` | `ReplayPredictionConfig` | `RunReplayTools` bounds the private throughput probe. | No: private prediction scheduling only | `validate_fast` |
| `gravity` | `gravity` | `WorldForceConfig` | `WorldEnvironment`, `PhysicsWorld`, and the persistent solver apply/report world gravity. | **Yes** | `validate_physics` |
| `fluid_height` | `fluidHeight` | `WorldForceConfig` | `WorldEnvironment` and `Terrain` define the fluid surface used by buoyancy/submersion. | **Yes** | `validate_physics` |
| `fluid_density` | `fluidDensity` | `WorldForceConfig` | `WorldEnvironment` supplies liquid density to body buoyancy/drag. | **Yes** | `validate_physics` |
| `gas_density` | `gasDensity` | `WorldForceConfig` | `WorldEnvironment` supplies atmospheric density to body drag. | **Yes** | `validate_physics` |
| `fluid_angular_drag_multiplier` | `fluidAngularDragMultiplier` | `WorldForceConfig` | `WorldEnvironment` supplies the angular drag multiplier to `PhysicsBodyStore`. | **Yes** | `validate_physics` |
| `velocity_limit` | `velocityLimit` | `BodySimulationPolicyConfig` | `PhysicsObjectPolicy` stamps the angular-velocity limit onto authored bodies. | **Yes** | `validate_physics` |
| `sphere_drag_coeff` | `sphereDragCoeff` | `PhysicsMaterialConfig` | `PhysicsObjectPolicy` stamps sphere drag onto authored physics material. | **Yes** | `validate_physics` |
| `friction_coeff` | `frictionCoeff` | `PhysicsMaterialConfig` | `PhysicsObjectPolicy`, terrain contact solving, live tuning, and diagnostics share terrain friction. | **Yes** | `validate_physics` |
| `object_friction_coeff` | `objectFrictionCoeff` | `PhysicsMaterialConfig` | Persistent object/object contacts and live tuning consume object friction. | **Yes** | `validate_physics` |
| `rolling_friction_coeff` | `rollingFrictionCoeff` | `PhysicsMaterialConfig` | Persistent contact rolling resistance and live tuning consume it. | **Yes** | `validate_physics` |
| `spin_friction_coeff` | `spinFrictionCoeff` | `PhysicsMaterialConfig` | Physics diagnostics retain spin friction even though no current solver read exists. | **Yes**: retained solver policy key | `validate_physics` |
| `contact_restitution_threshold` | `contactRestitutionThreshold` | `BodySimulationPolicyConfig` | `PhysicsObjectPolicy`, persistent contacts, terrain bodies, and diagnostics share the bounce threshold. | **Yes** | `validate_physics` |
| `contact_epsilon` | `contactEpsilon` | `BodySimulationPolicyConfig` | Authored bodies, terrain manifold generation, editor probes, and contact refresh use the same skin width. | **Yes** | `validate_physics` |
| `broadphase_cell` | `broadphaseCell` | `BroadphaseConfig` | `PhysicsWorld` sizes and rebuilds the spatial grid from this cell width. | **Yes** | `validate_physics` + `validate_perf` |
| `persistent_contact_slop` | `persistentContactSlop` | `PersistentContactSolverConfig` | `PersistentContactSolver` uses the object/object penetration allowance. | **Yes** | `validate_physics` |
| `persistent_contact_baumgarte_beta` | `persistentContactBaumgarteBeta` | `PersistentContactSolverConfig` | `PersistentContactSolver` uses the object/object stabilization rate. | **Yes** | `validate_physics` |
| `persistent_contact_position_correction_percent` | `persistentContactPositionCorrectionPercent` | `PersistentContactSolverConfig` | `PersistentContactSolver` caps object/object position correction. | **Yes** | `validate_physics` |
| `persistent_contact_solver_iterations` | `persistentContactSolverIterations` | `PersistentContactSolverConfig` | `PersistentContactSolver` controls deterministic PGS iteration count. | **Yes** | `validate_physics` |
| `terrain_contact_threshold` | `terrainContactThreshold` | `TerrainContactConfig` | `PhysicsObjectPolicy`, `PhysicsWorld`, and `TerrainContactManifold` share terrain acceptance distance. | **Yes** | `validate_physics` |
| `terrain_contact_slop` | `terrainContactSlop` | `TerrainContactConfig` | `PersistentContactSolver` applies terrain-specific penetration slop. | **Yes** | `validate_physics` |
| `terrain_contact_baumgarte_beta` | `terrainContactBaumgarteBeta` | `TerrainContactConfig` | `PersistentContactSolver` applies terrain-specific stabilization. | **Yes** | `validate_physics` |
| `terrain_max_baumgarte_bias` | `terrainMaxBaumgarteBias` | `TerrainContactConfig` | `PersistentContactSolver` caps terrain correction bias. | **Yes** | `validate_physics` |
| `physics_sleep_linear_speed` | `physicsSleepLinearSpeed` | `PhysicsSleepConfig` | `PhysicsWorld` and `PersistentContactSolver` classify linear rest state. | **Yes** | `validate_physics` |
| `physics_sleep_angular_speed` | `physicsSleepAngularSpeed` | `PhysicsSleepConfig` | `PhysicsWorld` and `PersistentContactSolver` classify angular rest state. | **Yes** | `validate_physics` |
| `physics_sleep_frames` | `physicsSleepFrames` | `PhysicsSleepConfig` | `PhysicsWorld` controls deterministic frames-to-sleep and seed state. | **Yes** | `validate_physics` |
| `shadow_max_height` | `shadowMaxHeight` | `BlobShadowConfig` | No runtime consumer remains; preserve the legacy parser/dump key separately from live shadow-map quality settings. | No | `validate_fast` |
| `shadow_max_alpha` | `shadowMaxAlpha` | `BlobShadowConfig` | No runtime consumer remains; preserve the legacy parser/dump key separately from live shadow-map quality settings. | No | `validate_fast` |
| `shadow_offset` | `shadowOffset` | `BlobShadowConfig` | No runtime consumer remains; preserve the legacy parser/dump key separately from live shadow-map quality settings. | No | `validate_fast` |
| `shadow_scale` | `shadowScale` | `BlobShadowConfig` | No runtime consumer remains; preserve the legacy parser/dump key separately from live shadow-map quality settings. | No | `validate_fast` |
| `spawn_x_base` | `spawnXBase` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body X positions. | **Yes** | `validate_physics` |
| `spawn_x_range` | `spawnXRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body X ranges. | **Yes** | `validate_physics` |
| `spawn_y_base` | `spawnYBase` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body Y positions. | **Yes** | `validate_physics` |
| `spawn_y_range` | `spawnYRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body Y ranges. | **Yes** | `validate_physics` |
| `spawn_z_base` | `spawnZBase` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body Z positions. | **Yes** | `validate_physics` |
| `spawn_z_range` | `spawnZRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses deterministic generated-body Z ranges. | **Yes** | `validate_physics` |
| `ball_mass_min` | `ballMassMin` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body mass. | **Yes** | `validate_physics` |
| `ball_mass_range` | `ballMassRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body mass range. | **Yes** | `validate_physics` |
| `ball_moment_min` | `ballMomentMin` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body rotational inertia. | **Yes** | `validate_physics` |
| `ball_moment_range` | `ballMomentRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body rotational-inertia range. | **Yes** | `validate_physics` |
| `ball_restitution_min` | `ballRestitutionMin` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body restitution. | **Yes** | `validate_physics` |
| `ball_restitution_range` | `ballRestitutionRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-body restitution range. | **Yes** | `validate_physics` |
| `ball_radius_range` | `ballRadiusRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses generated-sphere radius. | **Yes** | `validate_physics` |
| `ball_force_range` | `ballForceRange` | `GeneratedSceneConfig` | `SceneGeneratedSetup` chooses initial generated-body force. | **Yes** | `validate_physics` |
| `ocean_wave_height` | `oceanWaveHeight` | `WaterRenderStyleSettings` | `WorldEnvironment` passes visual wave amplitude to the ocean shader. | No: rendering only | `validate_dx12_renderer` |
| `ocean_perturb_strength` | `oceanPerturbStrength` | `WaterRenderStyleSettings` | `WorldEnvironment` passes visual normal perturbation to the ocean shader. | No: rendering only | `validate_dx12_renderer` |

### Target-domain counts

| Target domain | Flat fields |
|---|---:|
| `AssetPathsConfig` | 9 |
| `CameraConfig` | 10 |
| `TerrainGeometryConfig` | 3 |
| `SkyboxConfig` | 3 |
| `RuntimeCapacityConfig` | 2 |
| `PhysicsExecutionConfig` | 6 |
| existing `RuntimeRenderFlags` extension | 1 |
| `ReplayPredictionConfig` | 2 |
| `WorldForceConfig` | 5 |
| `BodySimulationPolicyConfig` | 3 |
| `PhysicsMaterialConfig` | 5 |
| `BroadphaseConfig` | 1 |
| `PersistentContactSolverConfig` | 4 |
| `TerrainContactConfig` | 4 |
| `PhysicsSleepConfig` | 3 |
| `BlobShadowConfig` | 4 |
| `GeneratedSceneConfig` | 14 |
| `WaterRenderStyleSettings` | 2 |
| **Total** | **81** |

There are **18 target domains** (17 new structs plus the existing
`RuntimeRenderFlags` extension). Fifty-one fields are physics-sensitive. The
one-commit-per-domain E2 rule groups all fields of each domain into one move;
it does not require field-by-field commits. The cohesive E2 units are
`AssetPathsConfig` (9, including the physics-gated heightfield path),
`CameraConfig` (10), `SkyboxConfig` (3), `RuntimeCapacityConfig` (2), the
`RuntimeRenderFlags` extension (1), `ReplayPredictionConfig` (2),
`BlobShadowConfig` (4), and `WaterRenderStyleSettings` (2). Keep
`TerrainGeometryConfig` with the physics-sensitive work. E3's explicit
isolated-commit(s) wording permits larger physics-gated chunks: world/body
policy (`WorldForceConfig`, `BodySimulationPolicyConfig`,
`PhysicsMaterialConfig`), solver policy (`BroadphaseConfig`,
`PersistentContactSolverConfig`, `TerrainContactConfig`, `PhysicsSleepConfig`),
and execution/generated topology (`PhysicsExecutionConfig`,
`TerrainGeometryConfig`, `GeneratedSceneConfig`). This keeps domain structs
narrow while avoiding one commit per tiny physics policy.

### Existing composed members (not flat inventory)

| Existing member | Existing domain type | Owner / reason to retain |
|---|---|---|
| `window` | `WindowConfig` | Window creation and display-mode settings are already composed. |
| `runtimeRender` | `RuntimeRenderFlags` | Live render toggles already have a render-runtime owner; add `shadowParallelPrep` here. |
| `contactAudio` | `ContactAudioConfig` | Contact-audio policy is already isolated from physics material policy. |
| `sceneLight` | `SceneLightConfig` | Scene light color is already an explicit presentation value. |
| `ordinaryRender` | `OrdinaryRenderConfig` | Ordinary lighting, material, water, and `ShadowQualityConfig` are already composed. |
| `cinematicRender` | `CinematicRenderConfig` | Final post-cleanup cinematic and `ShadowQualityConfig` shape is already composed. |

E1 evidence: header declarations and `ConfigSettings()` were mechanically
reconciled by identifier (81/81), then each group was checked against current
source consumers. The only direct fields with no current runtime consumer are
the four legacy `shadow_*` blob-shadow values; their keys remain parse/dump
compatible under the binding same-file-format decision. Documentation-only;
no repository validation required.

## E2 implementation evidence

- [x] Moved all eight E2 units into their narrow owners: `AssetPathsConfig`,
      `CameraConfig`, `SkyboxConfig`, `RuntimeCapacityConfig`, the
      `RuntimeRenderFlags::shadowParallelPrep` extension,
      `ReplayPredictionConfig`, `BlobShadowConfig`, and
      `WaterRenderStyleSettings`.
- [x] Updated every `ConfigSettings()` destination and every direct
      `EngineConfig` consumer in lockstep. Historical engine.cfg key spellings,
      accepted ranges, unknown-key behavior, and table order are unchanged.
- [x] Structural reconciliation: 218 parser/dump keys before and after in
      identical order; all 33 moved default literals identical; no old direct
      E2 access remains; exactly 48 direct fields remain, matching the complete
      E3 inventory.
- [x] Focused build: `tools\validate_build.bat Profile` passed on 2026-07-12
      in 17.75s with 0 warnings and 0 errors.
- [x] Touched-source comment audit: 14/14 checked, 0 deferred —
      `AssetSystem.cpp`, `Config.cpp`, `Config.h`, `Init.cpp`,
      `RuntimeRenderPasses.cpp`, `RuntimeRenderer.cpp`, `RunReplayTools.cpp`,
      `Run.cpp`, `RunCameraState.cpp`, `RuntimeTuning.cpp`, `RunScene.cpp`,
      `SkyBox.cpp`, `WorldEnvironment.cpp`, and `WorldEnvironment.h`. Config
      key/order and terrain-heightfield validation invariants are documented;
      the water runtime snapshot is distinguished from its config value.
- [x] Exact pre/post `Dump()` contract: the `[config]` block contains 219
      lines before and after, `Compare-Object` reports 0 rows, and both blocks
      hash to SHA-256
      `3bcc5f6247d6266b8f01dba7a7ebcf1963998e998713c80b14dc0deb3a78ab74`.
- [x] Formal grouped gate: `tools\validate_full.bat` passed on 2026-07-12 in
      134.940s. It subsumes the planned fast, renderer, and physics gates:
      Profile and Debug built with 0 warnings/errors; 136 doctest cases and
      2,853 assertions plus every standalone CPU lane passed; DX12 InfoQueue
      reported 0 validation errors and all three screenshot comparisons
      passed; physics standalone smoke passed and
      `physics_regression_varied.csv` matched 44,401 lines byte-exactly.
- [x] Grouped coordinator commit/push and E2 phase checkbox.

Grouping rationale: the owner explicitly prioritizes speed and quality over
tiny commits. These eight domain moves are therefore one coordinated,
uncommitted working slice: they are mechanical destination-path changes under
the same parser/default/order invariant and benefit from one consumer sweep.
The value structs remain narrow and independently owned; this grouping does not
create a replacement config bag or cross the E3/E4 scope barriers. Final gate,
commit grouping, and the E2 phase checkbox were completed by the coordinator.

## E3 implementation evidence

- [x] Moved all 48 remaining direct fields as the three approved policy
      families: world/body material policy (`WorldForceConfig`,
      `BodySimulationPolicyConfig`, `PhysicsMaterialConfig`); solver policy
      (`BroadphaseConfig`, `PersistentContactSolverConfig`,
      `TerrainContactConfig`, `PhysicsSleepConfig`); and execution/generated
      topology (`PhysicsExecutionConfig`, `TerrainGeometryConfig`,
      `GeneratedSceneConfig`).
- [x] Updated every `ConfigSettings()` destination and every source/test
      consumer in lockstep without changing `EngineConfig&` plumbing or random
      draw/solver execution order.
- [x] Structural reconciliation: all 48 moved default literals are identical;
      all 218 parser/dump rows preserve key, type, accepted range, and relative
      order; no old direct E3 access remains; `EngineConfig` now has exactly
      zero direct data fields.
- [x] Focused build: `tools\validate_build.bat Profile` passed on 2026-07-12
      in 17.28s with 0 warnings and 0 errors.
- [x] Touched-source comment audit: 20/20 checked, 0 deferred — `Config.cpp`,
      `Config.h`, `PersistentContactSolver.cpp`, `PhysicsObjectPolicy.cpp`,
      `PhysicsWorld.cpp`, `TornadoGameplay.cpp`, `Init.cpp`,
      `InputFrameExecution.cpp`, `Run.cpp`, `RunFrame.cpp`,
      `RunUiTextPass.cpp`, `RuntimeDiagnostics.cpp`, `RuntimeTuning.cpp`,
      `RunScene.cpp`, `SceneGeneratedSetup.cpp`, `Terrain.cpp`,
      `WorldEnvironment.cpp`, `TestDeterminism.cpp`,
      `TestPersistentContactSolver.cpp`, and `TestTerrain.cpp`. New config
      structs document owner, units/policy meaning, determinism, broadphase
      perf sensitivity, terrain render/collision coupling, and generated-scene
      random-order invariants.
- [x] Exact pre/post `Dump()` contract: the `[config]` block contains 219
      lines before and after, `Compare-Object` reports 0 rows, and both blocks
      retain SHA-256
      `3bcc5f6247d6266b8f01dba7a7ebcf1963998e998713c80b14dc0deb3a78ab74`.
- [x] Broadphase-sensitive performance gate: `tools\validate_perf.bat` passed
      on 2026-07-12 in 65.847s with both absolute budgets passing and clean
      Profile/Debug builds. The measured physics broadphase average improved
      14.6% versus the committed comparison baseline in this run.
- [x] Formal grouped gate: `tools\validate_full.bat` passed on 2026-07-12 in
      104.114s. Profile and Debug built with 0 warnings/errors; 136 doctest
      cases and 2,853 assertions plus every standalone CPU lane passed; DX12
      InfoQueue reported 0 validation errors and all three screenshot
      comparisons passed; physics standalone smoke passed and
      `physics_regression_varied.csv` matched 44,401 lines byte-exactly.
- [x] Grouped coordinator commit/push and E3 phase checkbox.

Grouping rationale: the owner prioritizes speed and quality over tiny commits,
and E1 explicitly approved these three E3 families. The 48 moves therefore
remain one coordinated, uncommitted working slice so a single consumer sweep
can prove the parser/default/determinism boundary. Each value struct remains a
narrow domain policy; no replacement physics bag or E4 table architecture was
introduced. Final gates, commit grouping, and the E3 checkbox were completed
by the coordinator.

## Acceptance

- [ ] Every `EngineConfig` field lives in a named domain struct with an
      owner comment; the class body is composition only.
- [ ] `engine.cfg` from before the plan parses identically (prove with a
      `Dump()` diff before/after on the committed cfg).
- [ ] `physics_regression_solver.csv` byte-exact after every
      physics-adjacent commit.
- [ ] Parse and dump are generated from one table per domain.

## Validation map

| Slice | Gate |
|-------|------|
| Non-physics domain moves | `validate_fast` (+ `validate_dx12_renderer` for render fields) |
| Physics-default moves | `validate_physics` (byte-exact CSV) |
| Parser table rework / final | `validate_full` (Config.h broad-scope row) |
