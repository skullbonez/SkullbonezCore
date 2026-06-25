# Global Service Context Plan

Date: 2026-06-25
Status: Draft follow-up plan
Impact area: engine context, renderer services, assets, textures, window/input, config, diagnostics
Validation for this document-only change: none required

## Goal

Replace normal-path global service access with explicit engine, world, render,
asset, and configuration contexts.

The current code still treats global services as ordinary infrastructure:
`Gfx()` returns the active renderer, `Cfg()` exposes mutable process config,
window/camera/texture systems are singletons, `AssetSystem` has an active global
bridge, and `RenderHelper` stores static GPU resources and per-frame batch data.
That shape is convenient for a single demo application, but it blocks a
professional engine shape: multiple worlds, headless tests, renderer teardown,
hot reload, deterministic replay tests, tool embedding, and future platform
layers all become harder than they need to be.

Target outcome:

```text
EngineServices
  Owns process-level platform services and immutable startup configuration.

RenderContext
  Owns the active renderer backend, frame allocators, helper resources, text
  resources, and render diagnostics for one renderer instance.

WorldContext
  Owns scene/world-local services such as cameras, terrain, environment,
  physics, render instances, and runtime tools.

AssetContext
  Owns source asset registry, texture residency, shader lookup, and material
  source records without relying on active global bridges.

ConfigSnapshot
  Captures the runtime knobs a subsystem needs instead of reading mutable
  `Cfg()` state from deep code.
```

## Current Evidence

- `SkullbonezSource/Rendering/IRenderBackend.cpp` stores the active renderer in
  `static std::unique_ptr<IRenderBackend> s_gfxBackend`; deep systems call
  `Gfx()`.
- `SkullbonezSource/Core/Config.h` documents `EngineConfig::Instance()` as a
  singleton available anywhere through `Cfg()`.
- `SkullbonezSource/Runtime/Window.h` exposes a `Window::Instance()` singleton
  with public native handles.
- `SkullbonezSource/Assets/TextureCollection.h` exposes
  `TextureCollection::Instance()`.
- `SkullbonezSource/Assets/AssetSystem.h` declares the active asset-system
  bridge for legacy singleton-style render helpers.
- `SkullbonezSource/Rendering/Helper.h` stores static shader pointers, mesh
  handles, batch vectors, and clip state.
- `SkullbonezSource/Runtime/EngineContext.h` is a useful start, but it is still
  a borrowed view over `Run`-owned systems rather than the primary context model
  for engine services.

## Design Rules

1. Do not remove every global in one branch. Make new code explicit first, then
   migrate old call sites by subsystem.
2. Separate process globals from world-local and renderer-local state.
3. Prefer passing `RenderContext&`, `AssetContext&`, or config snapshots over
   adding more singleton accessors.
4. Do not introduce a service locator with a different name.
5. Static data is allowed for compile-time constants, not mutable runtime state.
6. Keep teardown order observable and covered by validation before deleting
   old global paths.
7. Any config migration must preserve command-line and scene override behavior.

## Non-Goals

- Do not change renderer behavior while replacing access paths.
- Do not redesign the asset file format.
- Do not rewrite the scene parser.
- Do not make the engine cross-platform in this plan; this only prepares the
  service boundaries.
- Do not change physics defaults or baselines except through explicit,
  validated behavior changes.

## Phase 0: Service Inventory And Classification

Purpose: stop treating all globals as one category.

Tasks:

1. Inventory all calls to:
   - `Gfx()`
   - `Cfg()`
   - `Window::Instance()`
   - `CameraCollection::Instance()`
   - `TextureCollection::Instance()`
   - `WorkerPool::Instance()`
   - `ActiveAssetSystem()`
   - `CreateShaderFromActiveAssets()`
   - static mutable `RenderHelper` and `Text2d` state.
2. Classify each dependency as:
   - process-level service,
   - renderer-local service,
   - world-local service,
   - asset-local service,
   - config snapshot,
   - compatibility-only.
3. Add a small report under `Agentic/Reports/` before implementation starts.

Validation:

- Documentation-only phase: no validation required.

## Phase 1: Define Explicit Context Types

Purpose: create the destination before migrating call sites.

Tasks:

1. Add narrow context structs/classes:
   - `EngineServices`
   - `RenderContext`
   - `WorldContext`
   - `AssetContext`
   - `ConfigSnapshot`
2. Move existing `EngineContext` toward these names or split it if that produces
   clearer boundaries.
3. Bind contexts during runtime initialization and scene activation.
4. Require new extracted systems to accept explicit context inputs.
5. Add boundary validation for new direct global access in selected folders
   once the replacement exists.

Validation:

- `tools\validate_fast.bat`

## Phase 2: Move Renderer Access Out Of Globals

Purpose: make renderer-dependent systems work through a renderer instance.

Tasks:

1. Change render passes to receive `RenderContext&` or `IRenderBackend&` through
   their existing runtime-render input path.
2. Convert `RenderHelper` static GPU state into a `RenderPrimitiveResources`
   object owned by `RenderContext`.
3. Convert `Text2d` mutable static GPU resources into renderer-owned text
   resources.
4. Update terrain, skybox, water, launcher laser, debug overlays, UI text, and
   capture code to use explicit renderer access.
5. Keep `Gfx()` as a temporary compatibility accessor only for unmigrated code.

Validation:

- `tools\validate_dx12_renderer.bat`
- Use `tools\validate_full.bat` if window/capture/runtime launch behavior is
  touched.

## Phase 3: Move Asset And Texture Access Into `AssetContext`

Purpose: remove the active asset-system bridge and texture singleton from normal
render paths.

Tasks:

1. Make `AssetContext` own `AssetSystem` and texture residency.
2. Replace `CreateShaderFromActiveAssets()` with shader creation through
   `AssetContext` plus `RenderContext`.
3. Replace `TextureCollection::Instance()` call sites with injected texture
   service references.
4. Preserve legacy texture hashes as compatibility ids, but stop making them
   the only bridge between assets and renderer.
5. Add teardown tests or assertions that texture resources release before the
   renderer backend dies.

Validation:

- `tools\validate_dx12_renderer.bat`
- `tools\validate_full.bat`

## Phase 4: Replace Deep `Cfg()` Reads With Snapshots

Purpose: make configuration flow explicit and testable.

Tasks:

1. Define snapshots for subsystems that currently read `Cfg()` directly:
   - render settings,
   - physics settings,
   - terrain settings,
   - camera settings,
   - worker/thread settings,
   - UI/debug settings.
2. Build snapshots during startup, scene load, and scene override application.
3. Pass snapshots into terrain, world environment, physics, camera, renderer,
   and UI paths instead of reading `Cfg()` from deep code.
4. Keep command-line and scene overrides as explicit snapshot updates.
5. Remove direct `Cfg()` access from hot-loop physics and render code first.

Validation:

- `tools\validate_fast.bat` for narrow non-behavioral moves.
- `tools\validate_physics.bat` for physics defaults or solver-affecting config.
- `tools\validate_dx12_renderer.bat` for render defaults or screenshot-affecting
  config.
- `tools\validate_full.bat` for broad config flow changes.

## Phase 5: Retire Singleton Normal Paths

Purpose: make global access exceptional and guarded.

Tasks:

1. Remove normal-path use of:
   - `Gfx()`
   - `Cfg()`
   - `Window::Instance()`
   - `CameraCollection::Instance()`
   - `TextureCollection::Instance()`
   - `ActiveAssetSystem()`
2. Keep only process-bootstrap or compatibility shims where there is a named
   reason and a removal issue.
3. Add a validation script that fails when banned global access appears in
   runtime, rendering, physics, scene, or assets code.
4. Document the remaining allowed global access points in one short reference.

Validation:

- `tools\validate_fast.bat`
- `tools\validate_full.bat`

## Success Criteria

- New runtime/render/physics/scene code does not call global service accessors.
- Renderer resources are owned by a renderer context and can be released without
  relying on static helper state.
- Asset and texture lookup use an explicit asset/texture service.
- Deep systems consume config snapshots instead of mutable singleton config.
- Boundary validation prevents easy regression to global access.

## Risks

| Risk | Mitigation |
|------|------------|
| A context object becomes a disguised service locator | Keep contexts split by ownership and ban unrelated fields. |
| Renderer teardown order regresses | Move one resource family at a time and run DX12 validation. |
| Config snapshots accidentally freeze live scene/UI overrides | Preserve override application order and validate with full gate for broad moves. |
| Asset migration breaks legacy texture hashes | Keep hashes as compatibility ids while moving ownership to `AssetContext`. |

## Handoff Notes

Implement this through the repo-local orchestrator skill. Start with new-code
guardrails and renderer context movement; the value comes from preventing new
global use while old compatibility paths shrink.
