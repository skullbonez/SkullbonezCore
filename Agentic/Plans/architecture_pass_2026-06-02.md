# SkullbonezCore Architectural Pass

Date: 2026-06-02

Status update: 2026-06-16

Scope: current `main` worktree review of `SkullbonezSource/`, `Agentic/Reference/`, existing audits/plans, renderer interfaces, scene loading, physics, validation, and runtime architecture. This report is based on source inspection only; it does not include runtime profiling beyond the existing profiler/validation infrastructure.

## Executive Summary

SkullbonezCore has a strong identity already: it is not just an old graphics demo with modern wrappers, but a DX12 production renderer with deterministic scene playback, physics regression hooks, profiling, debug visualization, and an increasingly serious rigid-body solver. Those are rare and valuable foundations.

The biggest architectural pressure is concentration of responsibility. `SkullbonezRun` is the application coordinator, scene loader, input handler, renderer pass scheduler, screenshot/perf harness, HUD driver, and water control surface. `GameModelCollection` owns object storage, rendering, shadows, legacy physics, solver physics, persistent contacts, sleep state, visualizer state, and physics CSV logging. The engine works, but these two classes are now carrying most of the system shape.

The highest-leverage improvement is not another feature. It is to carve clear subsystems around the good work already present:

1. Split runtime orchestration into scene, simulation, rendering, capture, and diagnostics services.
2. Split render backend capabilities so the core draw API is not tied to DXR, debug lines, dynamic VBs, GPU timers, and instancing all at once.
3. Promote physics into a `PhysicsWorld` with separate body/collider/render entity data.
4. Replace manual parser/config chains with table-driven schemas that still preserve deterministic text scenes.
5. Add a resource/device lifetime layer so DX12 reset and future backend bring-up are robust by construction.

### 2026-06-16 Current Branch Assessment

Assessment target: `codex/engine-cleanup` worktree in `C:\SkullbonezCore`.
This pass is based on source inspection plus the active shader/material branch
work. `tools\validate_shaders.bat` and `tools\validate_full.bat` passed for the
implementation branch on 2026-06-16.

Several items from the original architecture pass are now complete enough to
stop treating them as open backlog:

| Area | Current status on `main` |
|------|--------------------------|
| DX12-only renderer retirement | Done. Runtime GL/DX11 choices and legacy shader families are gone; DX12 is the production renderer and validation target. |
| Render resource lifetime prep | Done for the current architecture. Backend-owned release/rebuild uses named lifecycle steps, pass resources have explicit release hooks, source asset records rebuild textures/shaders, and DX12 device-lost diagnostics write actionable reports. Live in-frame recovery is still deferred. |
| Render pipeline/pass extraction | Done. `DrawPrimitives()` is now a short frame-order orchestrator; `SkullbonezRunPasses.cpp` and `SkullbonezRunUiTextPass.cpp` own named pass bodies, pass inputs/outputs, and pass GPU resource lifetimes. |
| `PhysicsWorld` adapter boundary | Done. `PhysicsWorld` owns broadphase, sleep, persistent contact, solver scratch, debug trace, and diagnostics state while `GameModelCollection` delegates physics to it. |
| Terrain/object solver unification | Done through shared persistent solver rows and the Catto-style terrain row pipeline. |
| Config and CLI parsing footholds | Mostly done. Config settings are metadata-driven, CLI flags/values use directive tables, and `--dump-config`/`--dump-assets` exist. |
| Scene parser dispatch | Mostly done. Scene files use directive tables and UI subdirective tables, but many directive bodies still use bespoke token parsing. |
| Header namespace cleanup | Done for current source headers. Keep `.cpp`-local shorthand local. |
| Shader/material architecture cleanup | Done for the object-material v1 slice. Runtime contracts, Debug reflection diagnostics, object/fullscreen binders, CPU `RenderMaterial`, expanded material instance payloads, typed object/shadow CBV uploads, shader contract checking, and the `t4` object material table are implemented. Terrain/water/post style-material cleanup remains separate future work. |
| DX12 descriptor/upload foundation | Done for the current object-material contract. `Dx12DescriptorAllocator`, CPU descriptor allocators, and `Dx12FrameUploadSystem` exist with descriptor/upload accounting; the ordinary raster ABI is now `b0` plus fixed SRV slots `t0..t4`, where `t4` is the object material table. Descriptor indexing and structured-buffer material tables remain future work. |
| Asset system scaffold | Partly done. Source asset and shader source records exist, with a transitional active-asset bridge for legacy helpers. Runtime GPU cache/material/mesh integration is not complete. |

Current remaining implementation scope. Validation commands listed here are
targeted pre-commit/PR gates, not commands to run during normal iteration:

| Priority | Area | What remains | Validation expectation |
|----------|------|--------------|------------------------|
| High | Runtime subsystem extraction | `SkullbonezRun` is split across focused files and has pass objects, but it still owns scene loading, simulation policy, input command handling, diagnostics, UI command application, and capture orchestration. Next slices should extract `SceneRuntime`, then `SimulationSystem`, then `RuntimeDiagnostics`/`InputController` as facades first. | `tools\validate_full.bat` for runtime movement. |
| High | Physics data boundary | `PhysicsWorld` exists, but the real body store is still `std::vector<GameModel>` inside `GameModelCollection`, and render/collider/rigid-body data are still coupled through `GameModel`. Next slices should split body/collider/render snapshots, isolate legacy solver behavior behind a solver strategy, and move toward data-oriented body arrays only after adapter behavior is stable. | `tools\validate_physics.bat`; add `tools\validate_perf.bat` for storage/hot-loop changes. |
| High | Render graph/resource-state ownership | `RenderGraph` is still diagnostic, but transitions now carry native resource pointers so graph/live barrier comparison can match real resources instead of relying only on names. The live DX12 backend still records barriers manually in clears, framebuffer bind/unbind, DXR reflection, mip generation, readback, and present paths. The next render-architecture step is to move from diagnostic matching toward graph-owned transition validation/execution. | `tools\validate_dx12_renderer.bat`; verify `dx12_validation.txt` stays zero-error. |
| Medium-high | Render backend interface split | `RenderCapabilities` exists, but `IRenderBackend` still exposes DXR, GPU timers, dynamic VBs, debug lines, capture, instancing, textures, meshes, framebuffers, and state control in one interface. Split only when a future backend, render graph, or tooling slice needs the sharper boundary. | `tools\validate_dx12_renderer.bat`. |
| Medium | Asset system maturation | Source records and logical shader names exist. Remaining work is cache/invalidation policy, material/mesh records, hot reload hooks, and explicit source-vs-GPU lifetime integration with pass/resource ownership. | `tools\validate_dx12_renderer.bat` for renderer assets; `tools\validate_full.bat` if routed through runtime lifecycle. |
| Medium | Scene/config schema cleanup | CLI/config are much better, and scene dispatch is table-driven, but object, physics, cinematic, and style directives still rely on specialized token helpers and fixed arrays. Add richer diagnostics, typed directive schemas, and serializer-friendly metadata only in focused parser slices. | `tools\validate_fast.bat`; use `tools\validate_full.bat` if launch or scene-load semantics can change. |
| Medium | RAII and DX12 ownership | Many file handles now have scoped wrappers and shader blobs use `ComPtr`, but broad backend-owned DX12 resources, BLAS/TLAS/SBT resources, framebuffers, PSO caches, and several log/live-style file paths still use manual lifetime. Convert one ownership family at a time. | Renderer-specific changes need `tools\validate_dx12_renderer.bat`; broad lifetime changes may need `tools\validate_full.bat`. |
| Medium | Shader style/material cleanup outside objects | Object materials are typed and GPU-visible, but terrain, water, sky, and post still carry larger style-specific shader parameter sets. Future cleanup should split those style params deliberately without creating one-off concept shaders. | `tools\validate_dx12_renderer.bat`; add focused visual review for touched scenes. |
| Medium | DX12 binding/root-signature cleanup beyond `t4` | The ordinary raster root signature is intentionally small at `b0 + t0..t4`. Future work should not expand it again until material/post/water requirements prove a concrete need for descriptor indexing, a single descriptor range, or structured-buffer material data. | `tools\validate_dx12_renderer.bat`; add `tools\validate_perf.bat` for hot binding changes. |
| Medium | Water cleanup and known bug | `WaterPass` is extracted, but `WorldEnvironment` still owns most water shader setup and water style binding. The known bug where water renders through intersecting sphere back faces remains open. | `tools\validate_dx12_renderer.bat`; use focused water scenes before PR validation. |
| Medium | Global coupling / `EngineContext` | New pass/runtime code still reaches through `Gfx()`, `Cfg()`, window, camera, texture, skybox, profiler, and active asset globals. Do not try to remove all globals at once; pass explicit context into new extracted systems so the web stops growing. | Depends on touched subsystem. |
| Deferred | Worker system | Still design-only. Do not implement workers until `SimulationSystem` and the next `PhysicsWorld` data boundary exist. | Future worker work needs `tools\validate_physics.bat`, `tools\validate_perf.bat`, and explicit single-thread vs multi-thread deterministic comparison. |
| Deferred | Replay/debug tooling and standout features | Still design-only. Replay, render truth tools, profiler spatialization, scene forge, and water signature work should consume stable runtime/render/physics/asset boundaries rather than adding more logic to `SkullbonezRun` or `GameModelCollection`. | Depends on feature; start with narrow docs or `validate_fast`, then broaden when integrated. |

The short version: the old Phase 1 boundary work, render resource lifetime prep,
render pipeline extraction, adapter-first `PhysicsWorld`, and the object-side
shader/material architecture are no longer the main backlog. The remaining core
architecture work is now runtime scene/simulation ownership, physics data
separation, terrain/water/post style-material cleanup, asset maturation, and
turning the diagnostic render graph into real DX12 resource-state ownership.

## Current Architectural Shape

### Runtime and App Flow

The app starts in `SkullbonezInit.cpp`, tokenizes and parses command-line
flags, creates the window/backend, constructs `SkullbonezRun`, and then enters
a single-threaded frame loop. `SkullbonezRun` is still the central runtime
object, but its behavior is now split across focused implementation files and
named render pass objects:

| Responsibility | Current anchor |
|----------------|----------------|
| Command-line parse | `SkullbonezSource/SkullbonezInit.cpp:1463` |
| Backend-owned resource release order | `SkullbonezSource/SkullbonezRun.cpp:96` |
| Runtime initialization | `SkullbonezSource/SkullbonezRun.cpp:459` |
| Main frame loop | `SkullbonezSource/SkullbonezRunFrame.cpp:28` |
| Render pass ordering | `SkullbonezSource/SkullbonezRunRender.cpp:142` |
| Scene load/reset state | `SkullbonezSource/SkullbonezRunScene.cpp:624` |
| Per-frame render contract | `SkullbonezSource/SkullbonezRun.h:418` |

This is a healthier shape than the 2026-06-02 baseline, but `SkullbonezRun` is
still the facade for scene lifecycle, simulation timing, UI commands,
diagnostics, capture behavior, and render orchestration. The next runtime work
is owned subsystem extraction, not more file splitting.

### Rendering

The render abstraction is centered on a global `IRenderBackend` instance, with
DX12 as the only active implementation. Pass extraction is complete enough for
day-to-day work, while backend binding and resource-state ownership are still
the next deep renderer boundaries.

| Area | Current anchor |
|------|----------------|
| Capabilities query | `SkullbonezSource/SkullbonezIRenderBackend.h:55` |
| Backend interface | `SkullbonezSource/SkullbonezIRenderBackend.h:74` |
| Global accessor | `SkullbonezSource/SkullbonezIRenderBackend.cpp:14` |
| Pass objects and pass inputs | `SkullbonezSource/SkullbonezRun.h:402` |
| Pass implementations | `SkullbonezSource/SkullbonezRunPasses.cpp:118` |
| UI/text pass | `SkullbonezSource/SkullbonezRunUiTextPass.cpp:28` |
| DX12 descriptor/upload helpers | `SkullbonezSource/SkullbonezRenderDeviceDX12.h:337`, `SkullbonezSource/SkullbonezRenderDeviceDX12.h:541` |
| Diagnostic render graph | `SkullbonezSource/SkullbonezRenderGraph.h:217`, `SkullbonezSource/SkullbonezRenderBackendDX12.cpp:482` |

The current code has already fixed several older audit concerns. DX12 has
frame-indexed command allocators, descriptor allocators, per-frame upload
arenas, named pass objects, and pass-owned GPU resource release hooks. The
pressure point is now the live backend contract: `IRenderBackend` is still a
render device, swap chain, texture registry, shader factory, mesh factory, FBO
factory, screenshot capture service, state machine, DXR dispatcher, GPU timer
provider, dynamic vertex buffer manager, debug line renderer, and instancing
API. The render graph is also diagnostic-only; hand-written DX12 barriers remain
the production path.

### Physics and Game Objects

Physics now has a real `PhysicsWorld`, but the entity/data split is not done:

| Area | Current anchor |
|------|----------------|
| `GameModelCollection` facade/store | `SkullbonezSource/SkullbonezGameModelCollection.h:55` |
| `PhysicsWorld` owner | `SkullbonezSource/SkullbonezPhysicsWorld.h:54` |
| Physics step entry | `SkullbonezSource/SkullbonezPhysicsWorld.cpp:177` |
| Persistent contact/sleep/solver state | `SkullbonezSource/SkullbonezPhysicsWorld.h:79`, `SkullbonezSource/SkullbonezPhysicsWorld.h:182` |
| Collision shape variant | `SkullbonezSource/SkullbonezCollisionShape.h:22` |
| Game model friend access | `SkullbonezSource/SkullbonezGameModel.h:41`, `SkullbonezSource/SkullbonezGameModel.h:43` |

The variant-backed collision shape remains a strong modern choice, and
`PhysicsWorld` now owns the solver working set. The weak point is that the
actual object store, render-facing model data, collider access, and rigid-body
state still flow through `GameModel`/`GameModelCollection`. The next physics
architecture pass should separate body/collider/render snapshots without
changing solver behavior in the same slice.

### Scenes, Config, and Validation

The scene system is plain text and deterministic, which is exactly right for
regression scenes. Dispatch is now table-driven, but directive bodies still use
manual token and field parsing:

| Area | Current anchor |
|------|----------------|
| Scene data structs | `SkullbonezSource/SkullbonezTestScene.h:65`, `SkullbonezSource/SkullbonezTestScene.h:138` |
| Scene directive table | `SkullbonezSource/SkullbonezTestSceneParser.cpp:130` |
| Scene parser load loop | `SkullbonezSource/SkullbonezTestSceneParser.cpp:1699` |
| Config singleton | `SkullbonezSource/SkullbonezConfig.h:35` |
| Config metadata | `SkullbonezSource/SkullbonezConfig.cpp:46` |
| Asset source registry | `SkullbonezSource/SkullbonezAssetSystem.h:114` |

The validation culture is excellent. The `tools/` scripts create a clear contract for DX12 renderer regression, physics determinism, performance, and fast documentation checks. That discipline should be preserved as a first-class architectural feature.

## Strengths To Preserve

### DX12 Validation Confidence

Maintaining DX12 screenshot baselines, zero DX12 validation errors, and focused manifests is now the renderer confidence model. Keep that expectation central.

### DX12 Resource Reset Discipline

The retired renderer hot-switch path taught the code how to release and rebuild render resources carefully. Preserve that lifecycle discipline for DX12 device reset, resize, shader reloads, and future backend bring-up.

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

Current status: render-pass extraction and backend resource lifetime cleanup are
done, but runtime ownership is still concentrated. The next `SceneRuntime` slice
should move more load/reset/advance policy behind a facade while keeping
`SkullbonezRun` as the caller-facing coordinator. After that, extract
`SimulationSystem` for timestep and fixed-step policy.

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

Priority: High. Effort: Medium. Risk: Medium-high, because validation must prove DX12 stays visually stable.

Validation: `validate_dx12_renderer`, plus DX12 validation log check.

### 3. Introduce A Render Pipeline Layer

Current status: the first render-pipeline layer is implemented. `DrawPrimitives()`
is now a short ordered list of named pass objects with explicit input/output
bundles. Do not reopen this as another mechanical pass extraction task.

The remaining render architecture work is to move from named pass order to
explicit resource-state ownership. `RenderGraph` currently describes pass and
resource intent, but the live DX12 backend still owns command recording and
barriers manually.

The longer-term shape is still:

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

The first pass extraction produced the practical versions of these passes:

| Pass | Notes |
|------|-------|
| SkyboxPass | Shared by main and reflection views |
| ReflectionPass | Chooses FBO or DXR based on capabilities and debug flags |
| ModelPass | Uses render instances rather than direct `GameModelCollection` dependency |
| TerrainPass | Plain terrain draw |
| ShadowPass | Instanced shadow decals |
| WaterPass | FBO or DXR reflection texture input |
| DebugOverlayPass | Collision and broadphase visualizers |

This now sets the engine up for live render graph work. The next slice should
compare graph transitions against the current hand-written DX12 barriers before
moving barriers or command recording into the graph.

Priority: High for graph/resource-state ownership. Effort: Medium-high. Risk:
High, because barrier mistakes are GPU correctness bugs.

Validation: `validate_dx12_renderer`, plus `dx12_validation.txt` zero-error.

### 4. Formalize Resource Lifetime For Backend Switching

Current status: backend-neutral reset naming and ordered release/rebuild phases
are done for the active DX12 architecture. Pass resources have explicit
`EnsureGpuResources()` / `ReleaseGpuResources()` hooks, and backend-owned
release order is table-driven.

The deeper optional shape remains useful if render resources move out of the
runtime facade:

```cpp
class IRenderResource
{
    void CreateRenderResources(IRenderDevice& device);
    void ReleaseRenderResources();
};
```

If needed, add a small resource registry owned by the runtime or render graph:

1. Before backend destruction: `ReleaseRenderResources()` in dependency order.
2. After backend creation: `CreateRenderResources()` in dependency order.
3. On resize: notify only resources that depend on dimensions.

DX12 device-lost diagnostics are present, but live in-frame recovery is not
enabled. Real recovery should wait until pass/resource graph ownership is clear.

Priority: High. Effort: Medium. Risk: Medium.

Validation: `validate_dx12_renderer`, ideally three consecutive DX12-heavy runs for reset/lifetime-sensitive changes.

### 5. Promote Physics To `PhysicsWorld`

Current status: adapter-first `PhysicsWorld` exists and owns solver/contact/sleep
working state. `GameModelCollection` should still eventually stop being both a
render collection and the owner of the model/body store. A better final shape:

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

Keep `GameModel` temporarily as an adapter, then phase it into separate
body/collider/render data. This will make worker parallelism, island
decomposition, new shape types, and replay easier.

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

Status: design-only. Do not implement worker infrastructure until
`SimulationSystem` and the next `PhysicsWorld` data boundary exist.

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

The worker branch should not start until `PhysicsWorld` data ownership and
runtime simulation boundaries exist, because those boundaries define which data
workers may touch.

### 9. Make Scene Parsing Table-Driven

Current status: directive dispatch tables exist for scene commands and UI
subcommands. Keep the plain text format, but move the remaining directive bodies
toward typed parsing/schema metadata:

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

Current status: config settings are metadata-driven, command-line tokens are
parsed once, flag/value directive tables exist, and `--dump-config` is present.
Remaining work is mostly resolved-config reporting quality, shrinking specialized
physics/scene helpers, and keeping new options in the tables.

Recommended:

1. Keep command-line aliases in directive tables.
2. Keep config keys typed with metadata: name, type, default, min/max, target field.
3. Improve resolved config reporting after file, scene, and CLI overrides.
4. Avoid adding new one-off parser helpers unless a focused directive needs them.

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

Current status: complete for current source headers. Broad `using namespace`
imports are `.cpp`-local or internal glue only. Preserve this as a guardrail for
future headers.

Recommended:

1. Prefer fully-qualified names in headers.
2. Keep local `using` declarations in `.cpp` files only.
3. Treat any new header-level broad namespace import as a regression.

Priority: Guardrail. Effort: Low unless new broad header work regresses it.
Risk: Medium if it regresses across subsystem boundaries.

Validation: `validate_full` if many headers change.

### 13. Adopt RAII For COM And File Handles

DX12 still has many manual COM lifetime paths. Current code is careful, but this is fragile during refactors.

Recommended:

| Resource | Replacement |
|----------|-------------|
| `ID3D12*` raw owners | `Microsoft::WRL::ComPtr<T>` or a focused owner type |
| `FILE*` with manual close | Small `FileHandle` RAII wrapper or `std::ofstream` where binary compatibility allows |
| Manual `new[]` shader source | `std::vector<char>` or `std::string` |

Priority: Medium. Effort: Medium-high. Risk: Medium.

Validation: renderer-specific scripts for COM refactors.

### 14. Mature The Asset System

Current status: `AssetSystem` exists and owns source records for logical
textures/shaders plus path resolution. Runtime initialization registers built-in
assets and rebuilds texture/shader resources from those records. Remaining work
is to grow this from a source-record registry into cache/invalidation, mesh and
material records, hot reload, and clearer source-vs-GPU lifetime ownership.

The longer-term shape can still look like:

```cpp
class AssetSystem
{
    TextureHandle LoadTexture(std::string_view name);
    ShaderHandle LoadShader(std::string_view baseName);
    MeshHandle LoadMesh(std::string_view name);
};
```

Keep backend resources separate from source assets. On renderer or device
rebuild, source asset records should survive while GPU resources rebuild.

Priority: Medium. Effort: Medium-high. Risk: Medium.

Validation: `validate_dx12_renderer`.

### 15. Add A Render/Physics Replay Recorder

Status: design-only. Do not implement replay recording, time-travel debugging,
or new debug tooling until scene, simulation, physics snapshot, and render graph
capture boundaries are stable enough to consume.

Expanded standalone plan: `Agentic/Plans/replay-system-plan.md`.

The engine already has the ingredients: scene files, seed overrides, fixed-step mode, screenshots, profiler CSVs, and nudge snapshots. Make that a formal replay artifact so bugs are easier to preserve, scrub, branch, and compare across renderers and hardware.

Priority: Medium. Effort: Medium. Risk: Low.

Validation: `validate_fast` for artifact format; `validate_full` once integrated.

## Suggested Roadmap

### Phase 1: Finish Shader/Material Contracts

This is active parallel work, so do not mix code changes from this architecture
note into that branch. The pass extraction now gives shader cleanup a better
home: object, terrain, water, sky, reflection, volumetric, tonemap, and UI/text
passes have named binding points. Use that to make shader contracts, material
intent, texture slots, and style data explicit before changing DX12 binding
shape.

Why first: shader/material contracts affect water, object rendering, style data,
asset records, and future root-signature choices.

### Phase 2: Extract Runtime Ownership

1. Extract `SceneRuntime` as a facade for active scene index, scene queue,
   generated-scene selection, load/reset policy, and scene override application.
2. Extract `SimulationSystem` for timestep policy, fixed-step accumulator,
   pause/fly/nudge stepping, and simulation-to-render handoff.
3. Extract `RuntimeDiagnostics` and `InputController` only after scene and
   simulation ownership are stable.

Why second: `SkullbonezRun` is now readable around rendering, so the remaining
runtime risk is scene/simulation/control ownership.

### Phase 3: Split Physics Data From Render Entities

1. Keep the current `PhysicsWorld` adapter stable.
2. Split rigid-body state and collider state from `GameModel`.
3. Give rendering a read-only render snapshot or instance stream.
4. Isolate legacy solver behavior behind a solver strategy only after the data
   boundary is explicit.
5. Prepare worker-safe body arrays, but do not implement workers yet.

Why third: this is the highest-risk behavior area because deterministic physics
baselines can diverge from small storage/order changes.

### Phase 4: Make Render Resource State Explicit

1. Use the existing `RenderGraph` declarations to compare expected pass/resource
   transitions with the hand-written DX12 barriers.
2. Move barrier ownership into the graph only after diagnostics prove parity.
3. Split `IRenderBackend` capability interfaces when a graph/backend/tooling
   slice needs that narrower contract.
4. Revisit descriptor/root-signature layout only when material/post/water work
   proves the existing fixed SRV slots are insufficient.

Why fourth: pass ownership is now clear enough to move from "named frame story"
to actual resource-state ownership.

### Phase 5: Mature Assets, Water, Replay, And Workers

1. Extend `AssetSystem` into cache/invalidation, material/mesh records, and hot
   reload only after shader/material contracts settle.
2. Finish water style binding and investigate the sphere/water intersection bug
   from the extracted `WaterPass`.
3. Start replay/debug tooling once `SceneRuntime`, `SimulationSystem`,
   `PhysicsWorld` snapshots, and render graph capture points are stable.
4. Start deterministic worker work only after physics data ownership is clear.

Why fifth: these features should consume stable boundaries rather than adding
more responsibility to `SkullbonezRun` or `GameModelCollection`.

## Validation Guidance For Future Work

| Change | Suggested validation |
|--------|----------------------|
| Docs or architecture notes | No validation required when documentation-only |
| Shader, material, water, or pass binding behavior | `tools\validate_dx12_renderer.bat`; add `tools\validate_perf.bat` for hot object/material paths |
| Runtime extraction from `SkullbonezRun` | `tools\validate_full.bat` |
| Render backend interface, render graph, root signature, descriptors, uploads, or barriers | `tools\validate_dx12_renderer.bat` plus `dx12_validation.txt` equals zero errors |
| Pass-owned resource lifetime or device/reset behavior | `tools\validate_full.bat` if runtime lifecycle is touched; otherwise `tools\validate_dx12_renderer.bat` |
| Physics world data ownership or solver storage | `tools\validate_physics.bat` |
| Spatial grid, worker pool, hot path storage | `tools\validate_physics.bat` plus `tools\validate_perf.bat` |
| Scene parser/config behavior | `tools\validate_fast.bat` for pure parser cleanup, `tools\validate_full.bat` if scenes can load differently |
| Asset cache/source-record changes | `tools\validate_dx12_renderer.bat`; use `tools\validate_full.bat` if runtime lifecycle or scene loading changes |

## Creative Directions That Could Make Skullbonez Stand Out

Status: design-only. Do not implement these stretch systems until the lower-level
runtime, render, asset, replay, and physics boundaries are stable.

These ideas are intentionally parked until the engine has clearer system boundaries. Treat each one as a future product slice that consumes stable runtime, render, asset, replay, and physics APIs rather than adding more logic to `SkullbonezRun` or `GameModelCollection`.

Shared architecture gates before any stretch implementation:

| Gate | Why it matters |
|------|----------------|
| `RenderPipeline` pass extraction | Stretch render tools need named passes and capture points instead of one large draw function. |
| Backend capability queries | Cross-API tools need to know which renderer supports DXR, timers, debug lines, and capture paths. |
| `PhysicsWorld` debug snapshots | Physics tools need compact frame/contact/island data without scraping render objects. |
| `AssetSystem` source/GPU split | Scene forge and hot reload need source assets that survive backend switches. |
| Replay artifact format | Standout debugging tools should export reproducible cases, not one-off screenshots. |

### 1. The Render Truth Engine

Lean fully into deterministic DX12 visual evidence. Make SkullbonezCore the engine that can show current and baseline captures for the same scene, then produce an in-engine difference heatmap when they diverge. Add a "regression microscope" mode:

| Feature | Standout value |
|---------|----------------|
| Split-screen baseline/current DX12 | Makes visual regressions visible instantly |
| Pixel-diff heatmap overlay | Turns validation into a visual tool |
| Click a divergent pixel | Shows pass, material, depth, normal, texture, and backend state |
| Auto-save repro scene | Captures camera, seed, render state, and screenshot artifacts |

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
| `ExpectationBuilder` | Records screenshot, physics hash, perf budget, and DX12 regression thresholds. |
| `RecipeLibrary` | Stores named generators such as stack tests, reflection galleries, and terrain roll suites. |
| `ArtifactPublisher` | Writes generated scenes and baselines into predictable validation folders. |

### 5. The Living Profiler Overlay

The profiler is already strong. Make it more spatial and explanatory:

| Feature | Standout value |
|---------|----------------|
| Click a profiler bar to highlight objects/passes responsible | Connects time cost to scene content |
| Per-pass GPU thumbnails | Shows what each pass rendered |
| Frame capture bookmarks | Save "slow frame" with scene/replay metadata |
| DX12 validation timeline | Shows where current DX12 frame cost diverges from baselines |

This would make performance tuning feel like navigating the engine, not reading CSVs.

Architecture sketch:

| Component | Future role |
|-----------|-------------|
| `ProfilerFrameModel` | Aggregates CPU, GPU, pass, worker, and scene-object timing for a frame. |
| `ProfilerSelectionBridge` | Maps a profiler bar back to render pass, object IDs, physics island, or asset names. |
| `PassThumbnailCache` | Stores tiny per-pass captures for the selected frame. |
| `Dx12ValidationTimelineCompare` | Aligns current and baseline DX12 timings for the same replay frame. |
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
