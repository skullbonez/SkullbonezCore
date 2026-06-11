# SkullbonezCore Architectural Pass

Date: 2026-06-02

Status update: 2026-06-10

Scope: current `main` worktree review of `SkullbonezSource/`, `Agentic/Reference/`, existing audits/plans, renderer interfaces, scene loading, physics, validation, and runtime architecture. This report is based on source inspection only; it does not include runtime profiling beyond the existing profiler/validation infrastructure.

## Executive Summary

SkullbonezCore has a strong identity already: it is not just an old graphics demo with modern wrappers, but a tri-renderer parity testbed with deterministic scene playback, physics regression hooks, runtime renderer switching, profiling, debug visualization, and an increasingly serious rigid-body solver. Those are rare and valuable foundations.

The biggest architectural pressure is concentration of responsibility. `SkullbonezRun` is the application coordinator, scene loader, input handler, renderer pass scheduler, hot-switch manager, screenshot/perf harness, HUD driver, and water control surface. `GameModelCollection` owns object storage, rendering, shadows, legacy physics, solver physics, persistent contacts, sleep state, visualizer state, and physics CSV logging. The engine works, but these two classes are now carrying most of the system shape.

The highest-leverage improvement is not another feature. It is to carve clear subsystems around the good work already present:

1. Split runtime orchestration into scene, simulation, rendering, capture, and diagnostics services.
2. Split render backend capabilities so the core draw API is not tied to DXR, debug lines, dynamic VBs, GPU timers, and instancing all at once.
3. Promote physics into a `PhysicsWorld` with separate body/collider/render entity data.
4. Replace manual parser/config chains with table-driven schemas that still preserve deterministic text scenes.
5. Add a resource/device lifetime layer so backend hot-switching is robust by construction.

### 2026-06-10 Branch Scope Update

This plan is now being used as the backlog for the `codex/architecture-cleanup` branch. Several items from the original pass have been partly or fully addressed since 2026-06-02, so the current branch should focus on the remaining boundaries rather than re-solving fixed work.

Resolved or mostly resolved:

| Area | Current status |
|------|----------------|
| Terrain/object solver unification | Implemented through shared persistent solver rows; see `Agentic/Plans/physics-terrain-shared-row-pipeline-plan.md`. |
| Scene parser command dispatch | Partially implemented with directive tables in `SkullbonezTestSceneParser.cpp`; UI tab and water reflection value aliases now share a compact typed option helper. Remaining work is diagnostics, more typed helpers, and less prefix/token glue. |
| Config and CLI parsing | Config settings are table-driven, CLI value/flag handling has directive-table footholds, renderer selection and generated-object overrides use option tables, physics debug component flags and ranged float overrides use directive tables, and suite-file scene lists use scoped file IO. Remaining parser pressure is mostly resolved-config reporting and shrinking the specialized physics/scene argument helpers. |
| Runtime file split | `SkullbonezRun` behavior has been split across focused `.cpp` files, with capture serialization, renderer resource phases, scene-queue lookup, and scene reset snapshot/restore now behind small named helper boundaries. The runtime is still one broad facade rather than owned subsystems. |
| Physics commentary | Core physics-facing files now have layman-oriented comments explaining collision, solver, terrain, and visualizer behavior. |
| GL-era render resource names | Remaining runtime/helper/model/skybox/world reset methods now use backend-neutral `ResetRenderResources()` naming. |
| Render backend capabilities | A compatibility-preserving `RenderCapabilities` query now centralizes GPU timer, DXR, debug line, instancing, capture, and dynamic-VB support checks; capture-dependent and debug-line overlay callers now honor the relevant capability boundaries. |
| Source asset scaffold | `Assets::AssetSystem` exists as source-asset/path-resolution scaffolding and is now wired into runtime terrain/core texture path resolution; GPU resource ownership is still separate. |
| Capture subsystem seed | Backbuffer-to-BMP serialization now lives behind `CaptureSystem`, while `SkullbonezRun::SaveScreenshot()` remains the runtime facade. |
| File-handle and temporary-resource RAII | Config loading, GL shader source loading, terrain raw loading, scene parsing, suite-file startup parsing, nudge repro snapshot writing, text atlas IO, backbuffer capture, DX11/DX12 shader compile blobs, DX12 root-signature/generate-mips temporary blobs, and selected DX11 factory/info-queue/backbuffer temporaries now use scoped ownership rather than manual close/release paths. |
| Header namespace cleanup | Complete for `SkullbonezSource/*.h`: broad `using namespace` imports have been removed from source headers, with implementation shorthand kept local to `.cpp` files or internal runtime glue. |
| Renderer switch resource phases | Backend-owned release/rebuild sequences now run through an ordered resource-step table with named hooks for reflection, cinematic targets, text, models, helper caches, collision visualization, UI, textures, terrain, skybox, and world resources. This completes the branch's registry prep while preserving the future `IRenderResource` registry as a deeper render-pipeline step. |

Remaining implementation scope for this branch. Validation commands listed here
are targeted pre-commit/PR gates, not commands to run during normal iteration:

| Item | Implement on this branch? | Validation expectation |
|------|---------------------------|------------------------|
| Neutral render resource lifetime names and registry prep | Complete for current runtime hot-switch paths; full `IRenderResource` ownership is still part of render pipeline/resource registry extraction | `tools\validate_renderers.bat` if future render-resource code changes. |
| `SkullbonezRun` subsystem extraction | Yes, one subsystem at a time; capture facade, renderer resource phase helpers, scene-queue lookup helpers, and reset snapshot/restore helpers are started | `tools\validate_full.bat` for runtime code movement. |
| Render backend capability split | Partly done through `RenderCapabilities`; capture and debug-line callers now use the capability boundary, while deeper interface split remains | `tools\validate_renderers.bat` plus DX12 validation log check. |
| Render pipeline/pass extraction | Yes, incremental facade-first extraction | `tools\validate_renderers.bat`. |
| `PhysicsWorld` boundary | Yes, adapter-first extraction only | `tools\validate_physics.bat`; add `tools\validate_perf.bat` if hot storage changes. |
| CLI parser cleanup | In progress; renderer selection and generated-object overrides are table-driven, value/flag directive tables are in place, physics debug component switches and ranged float overrides are table-driven, suite-file loading uses scoped IO, and specialized physics/scene helpers remain | `tools\validate_fast.bat`; use broader validation if launch behavior changes or `SkullbonezInit*` changes. |
| Scene parser cleanup | In progress; directive tables and shared value-option parsing are in place for selected directives | `tools\validate_fast.bat`; use `tools\validate_full.bat` if scene loading semantics change. |
| Header namespace cleanup | Complete for current source headers; preserve this as a guardrail for future headers | `tools\validate_full.bat` if future header ownership changes cross subsystem boundaries. |
| RAII cleanup | In progress; file handles/source buffers plus selected DX11/DX12 shader/root-signature/backbuffer temporaries are improved, including suite parsing and nudge repro snapshot writing, while broader backend-owned COM resources remain | Renderer-specific validation for COM/resource changes. |
| Asset system | Scaffold started with source records/path resolution and runtime terrain/core texture paths now register through it; cache and GPU lifetime integration remain | `tools\validate_renderers.bat` when renderer assets are touched; `tools\validate_full.bat` if routed through `SkullbonezRun*`. |
| Worker system implementation | No | Design notes only; primary design lives in `Agentic/Plans/worker-system-plan.md`. |
| Replay/debug implementation | No | Design notes only. |
| Standout/stretch feature implementation | No | Design notes only. |

Commit policy for this branch: land one architecture item at a time. Before each
PR-bound feature-branch commit, run the targeted validation for the touched
area, then commit and push without asking for additional permission.

## Current Architectural Shape

### Runtime and App Flow

The app starts in `SkullbonezInit.cpp`, manually parses command-line flags, creates the window/backend, constructs `SkullbonezRun`, and then enters a single-threaded frame loop. `SkullbonezRun` is the central runtime object:

| Responsibility | Current anchor |
|----------------|----------------|
| Renderer hot-switch | `SkullbonezSource/SkullbonezRun.cpp:221` |
| Main frame loop | `SkullbonezSource/SkullbonezRun.cpp:592` |
| Physics timestep policy | `SkullbonezSource/SkullbonezRun.cpp:706` |
| Render pass ordering | `SkullbonezSource/SkullbonezRun.cpp:1329` |
| Scene load/reset state | `SkullbonezSource/SkullbonezRun.cpp:2612` |
| Runtime state storage | `SkullbonezSource/SkullbonezRun.h:41`, `SkullbonezSource/SkullbonezRun.h:195` |

This is understandable for an engine that grew from a demo harness. It is also the main maintainability limiter now.

### Rendering

The render abstraction is centered on a global `IRenderBackend` instance:

| Area | Current anchor |
|------|----------------|
| Backend interface | `SkullbonezSource/SkullbonezIRenderBackend.h:35` |
| Global accessor | `SkullbonezSource/SkullbonezIRenderBackend.cpp:14` |
| DXR methods on backend interface | `SkullbonezSource/SkullbonezIRenderBackend.h:124` |
| DX12 backend | `SkullbonezSource/SkullbonezRenderBackendDX12.h:100` |
| DX12 frame resources | `SkullbonezSource/SkullbonezRenderBackendDX12.h:107`, `SkullbonezSource/SkullbonezRenderBackendDX12.h:137`, `SkullbonezSource/SkullbonezRenderBackendDX12.h:167` |

The current code has already fixed several older audit concerns. GL has a debug callback and resource cleanup. DX11 uses a flip-discard swap chain and broad HRESULT checking. DX12 has frame-indexed command allocators and per-frame upload buffers. These are important strengths.

The pressure point is interface breadth. `IRenderBackend` is a render device, swap chain, texture registry, shader factory, mesh factory, FBO factory, screenshot capture service, state machine, DXR dispatcher, GPU timer provider, dynamic vertex buffer manager, debug line renderer, and instancing API. This makes every backend implement no-op feature methods, and it makes high-level systems know too much about backend mechanics.

### Physics and Game Objects

Physics is the most interesting subsystem, but its boundary is blurry:

| Area | Current anchor |
|------|----------------|
| `GameModelCollection` owner | `SkullbonezSource/SkullbonezGameModelCollection.h:35` |
| Physics dispatch | `SkullbonezSource/SkullbonezGameModelCollection.cpp:301` |
| Legacy solver | `SkullbonezSource/SkullbonezGameModelCollection.cpp:407` |
| Persistent contact solver | `SkullbonezSource/SkullbonezGameModelCollection.cpp:533` |
| Impulse solver path | `SkullbonezSource/SkullbonezGameModelCollection.cpp:1140` |
| Contact rows and warm-start state | `SkullbonezSource/SkullbonezGameModelCollection.h:79`, `SkullbonezSource/SkullbonezGameModelCollection.h:104` |
| Collision shape variant | `SkullbonezSource/SkullbonezCollisionShape.h:22` |
| Game model friend access | `SkullbonezSource/SkullbonezGameModel.h:41`, `SkullbonezSource/SkullbonezGameModel.h:43` |

The variant-backed collision shape is a strong modern choice: adding a shape forces dispatch sites to compile-check completeness. The spatial grid is also a good specialized structure for the fixed object cap. The weak point is that renderable object, rigid body, collider, solver scratch, sleep island, and debug state are all still tied together.

### Scenes, Config, and Validation

The scene system is plain text and deterministic, which is exactly right for regression scenes. The parser, however, is a long chain of `strncmp`/`sscanf_s` cases:

| Area | Current anchor |
|------|----------------|
| Scene data structs | `SkullbonezSource/SkullbonezTestScene.h:65`, `SkullbonezSource/SkullbonezTestScene.h:138` |
| Scene parser entry | `SkullbonezSource/SkullbonezTestScene.h:156` |
| Directive chain starts | `SkullbonezSource/SkullbonezTestScene.cpp:53` |
| Complex object parsing | `SkullbonezSource/SkullbonezTestScene.cpp:386`, `SkullbonezSource/SkullbonezTestScene.cpp:422`, `SkullbonezSource/SkullbonezTestScene.cpp:546` |
| Config singleton | `SkullbonezSource/SkullbonezConfig.h:35` |
| Config parser | `SkullbonezSource/SkullbonezConfig.cpp:20` |

The validation culture is excellent. The `tools/` scripts create a clear contract for render parity, physics determinism, performance, and fast documentation checks. That discipline should be preserved as a first-class architectural feature.

## Strengths To Preserve

### Tri-Renderer Parity

Maintaining GL, DX11, and DX12 output parity is a standout differentiator. Many engines hide backend differences behind abstraction; this engine actively tests and compares them. Keep that expectation central.

### Runtime Renderer Switching

Hot-switching renderers is difficult, especially across GL and DXGI ownership of the same window. The current implementation knows about the real window lifetime hazards and rebuilds render resources carefully. This deserves a more formal lifecycle layer rather than removal.

### Deterministic Test Scenes

The `.scene` and `.suite` system gives agents and humans a reliable way to reproduce visuals, physics, screenshots, and perf captures. This should grow into a scene/replay test contract, not be replaced by a less deterministic editor-first format.

### Physics Debuggability

Physics CSV baselines, nudge snapshots, collision visualization, broadphase visualization, sleep state colors, and profiler markers make the solver inspectable. That is a major advantage over black-box physics middleware.

### Zero-Allocation Hot Path Mindset

Several systems deliberately retain buffers, use fixed caps, and avoid per-frame allocation. Keep that culture. Where dynamic growth is needed, make it explicit and measurable.

## Key Risks And Improvement Areas

### 1. Decompose `SkullbonezRun`

`SkullbonezRun` has become the engine's whole runtime. The state structs in `SkullbonezRun.h` are a good first step, but the behavior remains concentrated.

Recommended split:

| New subsystem | Owns |
|---------------|------|
| `SceneRuntime` | Active scene index, load/reset/advance, scene overrides, RNG seed resolution |
| `SimulationSystem` | Timestep policy, fixed-step accumulator, pause/fly/nudge stepping rules |
| `RenderPipeline` | Pass order: skybox, reflection, models, terrain, shadows, water, debug overlays |
| `CaptureSystem` | Screenshots, screenshot intervals, screenshot-and-exit, auto-cycle |
| `RuntimeDiagnostics` | Perf CSV, profiler overlay selection, validation/repro messages |
| `InputController` | Key edge detection and high-level commands |

Do this as extraction, not rewrite. Move one cohesive chunk at a time and keep `SkullbonezRun` as the facade until the final shape is clear.

Branch progress: `CaptureSystem` owns backbuffer-to-BMP serialization, renderer hot-switch prep is organized through named resource phase tables, scene queue access now goes through helper methods instead of repeated direct bounds checks, and reset-time runtime state capture/restore is isolated from the main `LoadScene` flow. The next `SceneRuntime` slice should move more load/reset/advance policy behind a facade while keeping `SkullbonezRun` as the caller-facing coordinator.

Priority: High. Effort: Medium-high. Risk: Medium, because it touches broad runtime behavior.

Validation: `validate_full` for actual code movement.

### 2. Split Render Backend Capabilities

`IRenderBackend` currently exposes optional capabilities directly on every backend. DXR methods, GPU timers, debug lines, dynamic VBs, instancing, and capture all sit beside core rendering calls.

Recommended shape:

```cpp
struct RenderCapabilities
{
    bool supportsGpuTimers;
    bool supportsDxrReflection;
    bool supportsDebugLines;
    bool supportsInstancing;
};

class IRenderDevice { /* core lifecycle, state, resources, draw */ };
class IGpuTimerBackend { /* GPU timer API */ };
class IDebugDrawBackend { /* line rendering API */ };
class IDxrReflectionBackend { /* TLAS/reflection API */ };
```

Callers can query capabilities and downcast through explicit accessors instead of relying on no-op methods. This will also make non-DX12 backends feel complete rather than partially fake.

Priority: High. Effort: Medium. Risk: Medium-high, because validation must prove all three backends stay visually identical.

Validation: `validate_renderers`, plus DX12 validation log check.

### 3. Introduce A Render Pipeline Layer

`DrawPrimitives()` schedules skybox, reflection, model rendering, terrain, shadows, water, and debug visualization directly. The render pass order is real architecture, but it is represented as one large function.

Recommended shape:

```cpp
struct RenderFrameContext
{
    Matrix4 view;
    Matrix4 projection;
    Vector3 eye;
    float waterY;
    RuntimeRenderFlags flags;
};

class RenderPipeline
{
    void RenderFrame(const SceneView& scene, const RenderFrameContext& ctx);
};
```

Then extract passes:

| Pass | Notes |
|------|-------|
| SkyboxPass | Shared by main and reflection views |
| ReflectionPass | Chooses FBO or DXR based on capabilities and debug flags |
| ModelPass | Uses render instances rather than direct `GameModelCollection` dependency |
| TerrainPass | Plain terrain draw |
| ShadowPass | Instanced shadow decals |
| WaterPass | FBO or DXR reflection texture input |
| DebugOverlayPass | Collision and broadphase visualizers |

This sets the engine up for render graph work later, but does not require a full graph immediately.

Priority: High. Effort: Medium. Risk: Medium.

Validation: `validate_renderers`.

### 4. Formalize Resource Lifetime For Backend Switching

The current `ResetGLResources()` name is doing more than GL now. Terrain uses `ResetRenderResources()`, while helper/skybox/world/model collection still use GL-era naming.

Recommended shape:

```cpp
class IRenderResource
{
    void CreateRenderResources(IRenderDevice& device);
    void ReleaseRenderResources();
};
```

Add a small resource registry owned by the runtime or render pipeline:

1. Before backend destruction: `ReleaseRenderResources()` in dependency order.
2. After backend creation: `CreateRenderResources()` in dependency order.
3. On resize: notify only resources that depend on dimensions.

This would make renderer hot-switching and future device-lost handling less bespoke.

Priority: High. Effort: Medium. Risk: Medium.

Validation: `validate_renderers`, ideally three consecutive DX12/GL switches in a targeted scene.

### 5. Promote Physics To `PhysicsWorld`

`GameModelCollection` should eventually stop being both a render collection and a physics world. A better shape:

```cpp
struct EntityId { uint32_t index; uint32_t generation; };

struct RenderInstance { Matrix4 model; uint32_t mesh; uint32_t material; };
struct RigidBodyState { Vector3 position; Quaternion orientation; Vector3 velocity; Vector3 omega; };
struct Collider { CollisionShape shape; };

class PhysicsWorld
{
    void Step(float dt);
    const PhysicsDebugFrame& GetDebugFrame() const;
};
```

Keep `GameModel` temporarily as an adapter, then phase it into separate body/collider/render data. This will make worker parallelism, island decomposition, new shape types, and replay easier.

Priority: High. Effort: High. Risk: High.

Validation: `validate_physics`, `validate_perf`, and solver-specific scene baselines.

### 6. Isolate Legacy Physics

Legacy physics is useful for comparison and regression, but it should be packaged as a compatibility solver rather than intertwined with the active solver path.

Recommended shape:

```cpp
class IPhysicsSolver
{
    void Step(PhysicsWorldData& world, float dt);
};

class LegacySphereSolver : public IPhysicsSolver {};
class SequentialImpulseSolver : public IPhysicsSolver {};
```

This keeps the side-by-side value while reducing mode checks and comments spread through the collection.

Priority: Medium. Effort: Medium. Risk: Medium.

Validation: `validate_physics`.

### 7. Move Toward Data-Oriented Solver Storage

The solver already creates compact `SolverBodyState` scratch data, which is a good sign. Extend that idea:

| Current | Suggested |
|---------|-----------|
| `std::vector<GameModel>` as main body store | Separate arrays for transforms, velocities, masses, colliders |
| Per-model method calls in hot loops | Batch functions over spans |
| Solver writes through `GameModel` | Solver writes to body arrays, renderer reads a snapshot |

This is especially important if `MAX_GAME_MODELS` grows beyond 512.

Priority: Medium-high. Effort: High. Risk: High.

Validation: `validate_physics` and `validate_perf`.

### 8. Implement The Worker Plan Conservatively

Status for `codex/architecture-cleanup`: design-only. Do not implement worker infrastructure on this branch.

`Agentic/Plans/worker-system-plan.md` is directionally sound. The safest first step is not full island decomposition; it is a deterministic worker pool with parallel stages that have independent per-index writes.

Recommended order:

1. Add worker pool disabled by default.
2. Parallelize force application only.
3. Parallelize integration only.
4. Add deterministic fixed chunking.
5. Add island builder for narrowphase later.

Determinism is the key constraint. Any parallel reduction must preserve stable order.

Priority: Medium-high. Effort: Medium-high. Risk: High.

Validation: `validate_physics`, `validate_perf`, and explicit single-thread vs multi-thread byte-exact comparison in fixed-step scenes.

Design integration notes for the future worker branch:

| Boundary | Future worker contract |
|----------|------------------------|
| Runtime | `SimulationSystem` submits frame-critical work and waits before render reads transforms. |
| Physics | `PhysicsWorld` owns all worker-touched body arrays; render-facing snapshots are read only after the physics fence. |
| Profiler | Worker timing must use per-thread marker buffers that merge on the main thread after a fence. |
| Determinism | Fixed chunk ranges, stable island ordering, and ordered reductions are required. |
| Configuration | `worker_threads`, `physics_parallel`, and any amortized-task toggles belong in config metadata and CLI tables. |
| Validation | Every worker implementation step must compare single-thread and multi-thread deterministic physics output. |

The worker branch should not start until `PhysicsWorld` and runtime simulation boundaries exist, because those boundaries define which data workers may touch.

### 9. Make Scene Parsing Table-Driven

The current parser is readable in the small, but it is becoming a long command interpreter. Keep the plain text format, but move directive definitions into a table:

```cpp
struct SceneDirective
{
    const char* name;
    bool (*parse)(SceneParseContext&, std::string_view args);
};
```

Benefits:

| Benefit | Why it matters |
|---------|----------------|
| Better diagnostics | Each directive can report exact expected fields |
| Easier extension | New directives do not grow a single giant chain |
| Testability | Each directive parser can get focused tests |
| Serialization | The same schema can write scenes back out |

Priority: Medium. Effort: Medium. Risk: Low.

Validation: `validate_fast` if parser tests are not changed; `validate_full` if scene behavior changes.

### 10. Strengthen Config And CLI Parsing

The config loader and command-line parser use manual string scanning. This is fine for a small harness, but engine configuration will keep expanding.

Recommended:

1. Parse command-line tokens once, respecting quotes.
2. Use a directive table for CLI flags, just like scenes.
3. Give config keys typed metadata: name, type, default, min/max, target field.
4. Add `--dump-config` to print the resolved config after file, scene, and CLI overrides.

Priority: Medium. Effort: Medium. Risk: Low.

Validation: `validate_fast`, plus a few launch smoke tests.

### 11. Reduce Global Singleton Coupling

Current singletons include config, render backend, window, camera collection, texture collection, skybox, profiler, and static helper render caches.

Globals are not inherently bad for a single-window renderer testbed, but they make tests, backend switching, and future multi-scene tools harder. Move toward an `EngineContext` passed into major systems:

```cpp
struct EngineContext
{
    IRenderDevice* renderDevice;
    SkullbonezConfig* config;
    Profiler* profiler;
    AssetSystem* assets;
};
```

Do not eliminate all globals at once. Start by passing context into new extracted subsystems so new code does not deepen the global dependency web.

Priority: Medium. Effort: Medium-high. Risk: Medium.

Validation: depends on touched area.

### 12. Clean Namespace Pollution In Headers

Many headers use `using namespace`, including `using namespace std` in `SkullbonezGameModelCollection.h`. That leaks names into every translation unit that includes them.

Recommended:

1. Remove `using namespace std` from headers first.
2. Prefer fully-qualified names in headers.
3. Keep local `using` declarations in `.cpp` files only.

Priority: Medium. Effort: Medium, but mechanical. Risk: Medium due to broad compile impact.

Validation: `validate_full` if many headers change.

### 13. Adopt RAII For COM And File Handles

DX11 and DX12 have many manual `Release()` paths. Current code is careful, but this is fragile during refactors.

Recommended:

| Resource | Replacement |
|----------|-------------|
| `ID3D11*` / `ID3D12*` raw pointers | `Microsoft::WRL::ComPtr<T>` |
| `FILE*` with manual close | Small `FileHandle` RAII wrapper or `std::ofstream` where binary compatibility allows |
| Manual `new[]` shader source | `std::vector<char>` or `std::string` |

Priority: Medium. Effort: Medium-high. Risk: Medium.

Validation: renderer-specific scripts for COM refactors.

### 14. Add A Small Asset System

Assets are currently loaded through texture collection, shader creation by path, terrain raw loading, font atlas handling, and scene strings. A small asset system would centralize path resolution, caching, invalidation on backend switch, and eventually hot reload.

Start with:

```cpp
class AssetSystem
{
    TextureHandle LoadTexture(std::string_view name);
    ShaderHandle LoadShader(std::string_view baseName);
    MeshHandle LoadMesh(std::string_view name);
};
```

Keep backend resources separate from source assets. On renderer switch, source asset records survive; GPU resources rebuild.

Priority: Medium. Effort: Medium-high. Risk: Medium.

Validation: `validate_renderers`.

### 15. Add A Render/Physics Replay Recorder

Status for `codex/architecture-cleanup`: design-only. Do not implement replay recording, time-travel debugging, or new debug tooling on this branch.

Expanded standalone plan: `Agentic/Plans/replay-system-plan.md`.

The engine already has the ingredients: scene files, seed overrides, fixed-step mode, screenshots, profiler CSVs, and nudge snapshots. Make that a formal replay artifact so bugs are easier to preserve, scrub, branch, and compare across renderers and hardware.

Priority: Medium. Effort: Medium. Risk: Low.

Validation: `validate_fast` for artifact format; `validate_full` once integrated.

## Suggested Roadmap

### Phase 1: Stabilize The Boundaries

1. Rename GL-era render resource methods to backend-neutral names. Done for the visible runtime/helper/model/skybox/world reset paths; renderer-switch resource prep now uses an ordered table of named release/rebuild steps.
2. Extract `CaptureSystem` from `SkullbonezRun`. Seeded through the backbuffer capture helper; screenshot trigger policy still lives in runtime.
3. Extract `SceneRuntime` load/reset/advance state from `SkullbonezRun`. Scene-queue lookup and reset snapshot/restore are now centralized as helper footholds; the owned runtime subsystem still needs to be extracted.
4. Make scene/config parsing table-driven. Config and CLI have table-driven footholds, including renderer-option parsing, generated-object override parsing, physics debug component switch parsing, and ranged physics debug float parsing; scene parser has directive tables plus typed value-option helpers for selected aliases, and still needs richer directive diagnostics and serializer-friendly schemas.
5. Remove `using namespace std` and broad namespace imports from headers. Done for current source headers; keep it from regressing.

Why first: these changes reduce future blast radius without changing the engine's output.

### Phase 2: Render Architecture

1. Split `IRenderBackend` into core device plus capabilities.
2. Extract `RenderPipeline` and pass classes.
3. Add render resource registry for backend switch/rebuild.
4. Add shader bytecode/cache layer for DX11/DX12 and source-change detection for GL.
5. Add explicit resource state tracking helpers for DX12 resources.

Why second: rendering is the engine's public identity, and these changes make tri-renderer parity easier to preserve as features grow.

### Phase 3: Physics World

1. Introduce `PhysicsWorld` behind `GameModelCollection`.
2. Move solver scratch/contact/sleep data into `PhysicsWorld`.
3. Split render instances from rigid-body/collider state.
4. Isolate legacy solver as a solver strategy.
5. Prepare worker-safe data boundaries, but do not implement workers on this branch.

Why third: this is the highest-risk work and should happen after runtime/render boundaries are cleaner.

### Phase 4: Deferred Tooling And Standout Features

These are design-only for `codex/architecture-cleanup`. They should become separate implementation branches after the lower-level runtime, render, asset, and physics boundaries are stable.

1. Formal replay artifacts.
2. Side-by-side cross-renderer diff view.
3. Physics contact/manifold timeline debugger.
4. Asset hot reload.
5. Scene authoring helpers that generate deterministic tests automatically.

Why fourth: these features become easier once the engine has clear systems to observe.

## Validation Guidance For Future Work

| Change | Suggested validation |
|--------|----------------------|
| Docs or architecture notes | No validation required when documentation-only |
| Runtime extraction from `SkullbonezRun` | `tools\validate_full.bat` |
| Render backend interface or render pass changes | `tools\validate_renderers.bat` |
| DX12 resource lifetime/barriers | `tools\validate_renderers.bat` plus `dx12_validation.txt` equals zero errors |
| Physics world or solver storage | `tools\validate_physics.bat` |
| Spatial grid, worker pool, hot path storage | `tools\validate_physics.bat` plus `tools\validate_perf.bat` |
| Scene parser/config behavior | `tools\validate_fast.bat` for pure parser cleanup, `tools\validate_full.bat` if scenes can load differently |

## Creative Directions That Could Make Skullbonez Stand Out

Status for `codex/architecture-cleanup`: design-only. Do not implement these stretch systems on this branch.

These ideas are intentionally parked until the engine has clearer system boundaries. Treat each one as a future product slice that consumes stable runtime, render, asset, replay, and physics APIs rather than adding more logic to `SkullbonezRun` or `GameModelCollection`.

Shared architecture gates before any stretch implementation:

| Gate | Why it matters |
|------|----------------|
| `RenderPipeline` pass extraction | Stretch render tools need named passes and capture points instead of one large draw function. |
| Backend capability queries | Cross-API tools need to know which renderer supports DXR, timers, debug lines, and capture paths. |
| `PhysicsWorld` debug snapshots | Physics tools need compact frame/contact/island data without scraping render objects. |
| `AssetSystem` source/GPU split | Scene forge and hot reload need source assets that survive backend switches. |
| Replay artifact format | Standout debugging tools should export reproducible cases, not one-off screenshots. |

### 1. The Cross-API Truth Engine

Lean fully into the tri-renderer identity. Make SkullbonezCore the engine that can show GL, DX11, and DX12 rendering the same scene, then produce an in-engine difference heatmap when they diverge. Add a "parity microscope" mode:

| Feature | Standout value |
|---------|----------------|
| Split-screen GL/DX11/DX12 | Makes backend differences visible instantly |
| Pixel-diff heatmap overlay | Turns validation into a visual tool |
| Click a divergent pixel | Shows pass, material, depth, normal, texture, and backend state |
| Auto-save repro scene | Captures camera, seed, renderer state, and screenshot artifacts |

This would make the engine genuinely unusual: part renderer, part graphics-debug laboratory.

Architecture sketch:

| Component | Future role |
|-----------|-------------|
| `RendererComparisonSession` | Owns the active scene/camera seed and runs the same frame through selected backends. |
| `FrameCaptureStore` | Keeps color/depth/pass captures with renderer and frame metadata. |
| `DiffAnalyzer` | Produces average/max pixel diff, heatmaps, and selected-pixel reports. |
| `ParityInspectorOverlay` | Lets the user inspect divergent pixels and jump to pass/material/state context. |
| `ReplayExporter` | Saves the exact camera, renderer set, scene, seed, and diff thresholds as a replay or suite case. |

### 2. A Time-Travel Physics Debugger

Expanded standalone plan: `Agentic/Plans/replay-system-plan.md`.

The deterministic scene system and fixed-step physics are perfect for a rewind-style debugger backed by checkpoints, compact per-frame samples, and deterministic forward replay. Keep the architecture pass as a concept index; the standalone replay plan owns the implementation details for scrubbing, contact inspection, branch-from-frame, and regression export.

### 3. Water As The Signature System

The engine already has terrain, rigid bodies, buoyancy, water rendering, reflection FBOs, and a DXR reflection path. Push that into a recognizable signature:

| Feature | Standout value |
|---------|----------------|
| Rigid bodies disturb the water surface | Visual feedback links physics and rendering |
| Buoyancy debug overlay | Shows displaced volume, drag, and force arrows |
| DXR reflection compared against FBO reflection | A teaching and validation tool |
| Terrain-water interaction | Flood basins, reveal slopes, float debris |

The engine could become known for tactile rigid-body water scenes rather than generic terrain balls.

Architecture sketch:

| Component | Future role |
|-----------|-------------|
| `WaterInteractionSystem` | Converts body-water overlap into forces, ripples, and debug arrows. |
| `WaterSurfaceState` | Owns simulation parameters and render-facing wave/ripple data. |
| `ReflectionModeController` | Switches between FBO, DXR, and debug reflection modes through render capabilities. |
| `BuoyancyDebugView` | Shows displaced volume, drag, buoyancy force, and object sleep state. |
| `WaterSceneSuite` | Generates deterministic comparison cases for mass, density, terrain basin, and renderer mode. |

### 4. A Deterministic Scene Forge

Turn `.scene` from a test format into a creative format. Add a scene generator that can produce reproducible stress tests and visual compositions:

| Generator | Example |
|-----------|---------|
| `stack_test boxes=128 height=8 seed=...` | Physics stacks with known expected settling |
| `reflection_gallery renderer=all` | Camera paths designed to expose backend differences |
| `water_lab buoyancy_sweep` | Same object masses at different fluid densities |
| `terrain_roll_suite` | Slopes, ramps, edge cases, and expected rest states |

The twist: every generated scene is also a validation asset. Creativity and regression coverage grow together.

Architecture sketch:

| Component | Future role |
|-----------|-------------|
| `SceneRecipe` | Typed generator description with seed, parameters, and expected outputs. |
| `SceneForge` | Expands recipes into `.scene` files and optional `.suite` entries. |
| `ExpectationBuilder` | Records screenshot, physics hash, perf budget, and renderer parity thresholds. |
| `RecipeLibrary` | Stores named generators such as stack tests, reflection galleries, and terrain roll suites. |
| `ArtifactPublisher` | Writes generated scenes and baselines into predictable validation folders. |

### 5. The Living Profiler Overlay

The profiler is already strong. Make it more spatial and explanatory:

| Feature | Standout value |
|---------|----------------|
| Click a profiler bar to highlight objects/passes responsible | Connects time cost to scene content |
| Per-pass GPU thumbnails | Shows what each pass rendered |
| Frame capture bookmarks | Save "slow frame" with scene/replay metadata |
| Backend comparison timeline | Shows where DX11, DX12, and GL spend time differently |

This would make performance tuning feel like navigating the engine, not reading CSVs.

Architecture sketch:

| Component | Future role |
|-----------|-------------|
| `ProfilerFrameModel` | Aggregates CPU, GPU, pass, worker, and scene-object timing for a frame. |
| `ProfilerSelectionBridge` | Maps a profiler bar back to render pass, object IDs, physics island, or asset names. |
| `PassThumbnailCache` | Stores tiny per-pass captures for the selected frame. |
| `BackendTimelineCompare` | Aligns GL, DX11, and DX12 timings for the same replay frame. |
| `SlowFrameBookmark` | Saves scene, replay event, profiler slice, and artifacts for future investigation. |

### 6. A Retro-Modern Engine Aesthetic

SkullbonezCore has 2005 roots and modern backend ambition. That can become an aesthetic rather than a liability: crisp shader-based rendering, old-school immediacy, visible debug geometry, and a bold testbed personality. Build a few showcase scenes that celebrate that:

| Scene | Identity |
|-------|----------|
| "The Renderer Gauntlet" | Same scene through three APIs with live diff |
| "The Contact Cathedral" | Stacked boxes, sleeping islands, impulse lines |
| "The Flooded Quarry" | Terrain, water, buoyancy, reflections, rolling bodies |
| "The Replay Wall" | Dozens of deterministic micro-scenes running as a living dashboard |

That would make the engine memorable because it is honest about being a system you can inspect, not just a black-box renderer trying to look like everyone else.
