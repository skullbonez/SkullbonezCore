# SkullbonezCore Architectural Pass

Date: 2026-06-02

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

The engine already has the ingredients: scene files, seed overrides, fixed-step mode, screenshots, profiler CSVs, and nudge snapshots. Make that a formal replay artifact:

```text
replay {
  engine_commit
  renderer
  scene
  seed
  fixed_step
  inputs[]
  camera_events[]
  expected_screenshots[]
  expected_physics_hashes[]
}
```

This would make bugs easier to preserve and compare across renderers and hardware.

Priority: Medium. Effort: Medium. Risk: Low.

Validation: `validate_fast` for artifact format; `validate_full` once integrated.

## Suggested Roadmap

### Phase 1: Stabilize The Boundaries

1. Rename GL-era render resource methods to backend-neutral names.
2. Extract `CaptureSystem` from `SkullbonezRun`.
3. Extract `SceneRuntime` load/reset/advance state from `SkullbonezRun`.
4. Make scene/config parsing table-driven.
5. Remove `using namespace std` and broad namespace imports from headers.

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
5. Implement deterministic worker pool stages.

Why third: this is the highest-risk work and should happen after runtime/render boundaries are cleaner.

### Phase 4: Tooling And Standout Features

1. Formal replay artifacts.
2. Side-by-side cross-renderer diff view.
3. Physics contact/manifold timeline debugger.
4. Asset hot reload.
5. Scene authoring helpers that generate deterministic tests automatically.

Why fourth: these features become easier once the engine has clear systems to observe.

## Validation Guidance For Future Work

| Change | Suggested validation |
|--------|----------------------|
| Docs or architecture notes | `tools\validate_fast.bat` |
| Runtime extraction from `SkullbonezRun` | `tools\validate_full.bat` |
| Render backend interface or render pass changes | `tools\validate_renderers.bat` |
| DX12 resource lifetime/barriers | `tools\validate_renderers.bat` plus `dx12_validation.txt` equals zero errors |
| Physics world or solver storage | `tools\validate_physics.bat` |
| Spatial grid, worker pool, hot path storage | `tools\validate_physics.bat` plus `tools\validate_perf.bat` |
| Scene parser/config behavior | `tools\validate_fast.bat` for pure parser cleanup, `tools\validate_full.bat` if scenes can load differently |

## Creative Directions That Could Make Skullbonez Stand Out

### 1. The Cross-API Truth Engine

Lean fully into the tri-renderer identity. Make SkullbonezCore the engine that can show GL, DX11, and DX12 rendering the same scene, then produce an in-engine difference heatmap when they diverge. Add a "parity microscope" mode:

| Feature | Standout value |
|---------|----------------|
| Split-screen GL/DX11/DX12 | Makes backend differences visible instantly |
| Pixel-diff heatmap overlay | Turns validation into a visual tool |
| Click a divergent pixel | Shows pass, material, depth, normal, texture, and backend state |
| Auto-save repro scene | Captures camera, seed, renderer state, and screenshot artifacts |

This would make the engine genuinely unusual: part renderer, part graphics-debug laboratory.

### 2. A Time-Travel Physics Debugger

The deterministic scene system and fixed-step physics are perfect for rewind. Store compact per-frame snapshots or periodic checkpoints plus input deltas, then allow:

| Feature | Standout value |
|---------|----------------|
| Scrub frame-by-frame through contact events | Makes solver behavior inspectable |
| Show contact normals, accumulated impulses, sleep islands | Turns the solver into an explorable system |
| Branch from any frame | Try alternate impulses, water height, or object nudges |
| Export branch as `.scene` | Every interesting moment becomes a regression test |

This would give Skullbonez a "physics lab" flavor rather than just a physics demo.

### 3. Water As The Signature System

The engine already has terrain, rigid bodies, buoyancy, water rendering, reflection FBOs, and a DXR reflection path. Push that into a recognizable signature:

| Feature | Standout value |
|---------|----------------|
| Rigid bodies disturb the water surface | Visual feedback links physics and rendering |
| Buoyancy debug overlay | Shows displaced volume, drag, and force arrows |
| DXR reflection compared against FBO reflection | A teaching and validation tool |
| Terrain-water interaction | Flood basins, reveal slopes, float debris |

The engine could become known for tactile rigid-body water scenes rather than generic terrain balls.

### 4. A Deterministic Scene Forge

Turn `.scene` from a test format into a creative format. Add a scene generator that can produce reproducible stress tests and visual compositions:

| Generator | Example |
|-----------|---------|
| `stack_test boxes=128 height=8 seed=...` | Physics stacks with known expected settling |
| `reflection_gallery renderer=all` | Camera paths designed to expose backend differences |
| `water_lab buoyancy_sweep` | Same object masses at different fluid densities |
| `terrain_roll_suite` | Slopes, ramps, edge cases, and expected rest states |

The twist: every generated scene is also a validation asset. Creativity and regression coverage grow together.

### 5. The Living Profiler Overlay

The profiler is already strong. Make it more spatial and explanatory:

| Feature | Standout value |
|---------|----------------|
| Click a profiler bar to highlight objects/passes responsible | Connects time cost to scene content |
| Per-pass GPU thumbnails | Shows what each pass rendered |
| Frame capture bookmarks | Save "slow frame" with scene/replay metadata |
| Backend comparison timeline | Shows where DX11, DX12, and GL spend time differently |

This would make performance tuning feel like navigating the engine, not reading CSVs.

### 6. A Retro-Modern Engine Aesthetic

SkullbonezCore has 2005 roots and modern backend ambition. That can become an aesthetic rather than a liability: crisp shader-based rendering, old-school immediacy, visible debug geometry, and a bold testbed personality. Build a few showcase scenes that celebrate that:

| Scene | Identity |
|-------|----------|
| "The Renderer Gauntlet" | Same scene through three APIs with live diff |
| "The Contact Cathedral" | Stacked boxes, sleeping islands, impulse lines |
| "The Flooded Quarry" | Terrain, water, buoyancy, reflections, rolling bodies |
| "The Replay Wall" | Dozens of deterministic micro-scenes running as a living dashboard |

That would make the engine memorable because it is honest about being a system you can inspect, not just a black-box renderer trying to look like everyone else.
