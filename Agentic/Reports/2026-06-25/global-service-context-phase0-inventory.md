# Global Service Context Phase 0 Inventory

Date: 2026-06-25  
Branch: `nightrunner-25th-june`  
Plan: `Agentic/Plans/global-service-context-plan.md`  
Status: Phase 0 inventory complete; implementation phases remain open.

## Scope

This inventory classifies normal-path global service access before migrating to
explicit engine, render, world, asset, and configuration contexts.

Method: raw search over `SkullbonezSource` for:

- `Gfx()`
- `Cfg()`
- `Window::Instance()`
- `CameraCollection::Instance()`
- `TextureCollection::Instance()`
- `WorkerPool::Instance()`
- `ActiveAssetSystem()`
- `CreateShaderFromActiveAssets()`
- `Text2d::`
- `RenderHelper::`

Documentation-only validation: none required.

## Usage Hotspots

| Surface | Count / Files | Main Hotspots |
|---------|---------------|---------------|
| `Gfx()` | 291 / 29 | `Runtime/RunPasses.cpp` 101, `Rendering/Helper.cpp` 34, `Rendering/Text.cpp` 33 |
| `Cfg()` | 215 / 25 | `Physics/PersistentContactSolver.cpp` 26, `World/Terrain.cpp` 25, `Runtime/Camera.cpp` 22 |
| `Text2d::` | 186 / 16 | `Rendering/Text.cpp` 63, `Runtime/RunUiTextPass.cpp` 53, `Core/Profiler.cpp` 40 |
| `RenderHelper::` | 81 / 7 | `Rendering/Helper.cpp` 49, `Rendering/GameModelRenderer.cpp` 20 |
| `WorkerPool::Instance()` | 18 / 8 | `Physics/PhysicsWorld.cpp` 6, `Runtime/Init.cpp` 3 |
| `CreateShaderFromActiveAssets()` | 15 / 10 | Renderer, world, and UI shader setup paths |
| `Window::Instance()` | 8 / 4 | Runtime bootstrap, input, and window code |
| `CameraCollection::Instance()` | 4 / 4 | Runtime and scene setup |
| `ActiveAssetSystem()` | 4 / 4 | Asset bridge, test parser, editor tools |
| `TextureCollection::Instance()` | 3 / 3 | Skybox, texture singleton, runtime |

## Ownership Categories

| Category | Current Surfaces | Notes |
|----------|------------------|-------|
| Renderer-local | `Gfx()`, `RenderHelper`, `Text2d`, shader creation | Normal rendering, UI, terrain, water, launcher laser, and debug drawing still depend on renderer globals. |
| Config snapshot | `Cfg()` | Physics, terrain, camera, scene setup, and runtime UI read mutable config directly. |
| Process-level | `Window::Instance()`, `WorkerPool::Instance()` | Some usage is legitimate bootstrap/process ownership; deep physics worker access should eventually receive explicit services. |
| World-local | `CameraCollection::Instance()` | Scene setup still reaches the global camera collection instead of a world context. |
| Asset-local | `TextureCollection::Instance()`, `ActiveAssetSystem()`, `CreateShaderFromActiveAssets()` | Shader and texture lookup still use active process bridges. |
| Compatibility-only | Global accessor definitions and active-bridge implementation files | Keep as temporary shims while explicit contexts land. |

## Guardrail Candidates

- Ban new `Gfx()` calls outside renderer/context-owned code, with a temporary
  allowlist for current migration hotspots.
- Ban new `Cfg()` calls outside config loading, command-line overrides, scene
  override application, and future snapshot builders.
- Ban new singleton `Instance()` calls outside bootstrap/composition roots and
  singleton implementation files.
- Ban new `ActiveAssetSystem()` and `CreateShaderFromActiveAssets()` calls
  outside legacy bridge code once `AssetContext` exists.
- Track `RenderHelper` and `Text2d` static mutable state as renderer-owned
  migration debt before turning the rules into failures.

## Suggested Next Slices

1. Add explicit report-only global-access inventory output to a validation
   script so counts stop drifting unnoticed.
2. Introduce destination context types before banning existing paths:
   `EngineServices`, `RenderContext`, `WorldContext`, `AssetContext`, and
   config snapshots.
3. Migrate one renderer-local resource family first, likely `Text2d` or
   `RenderHelper` GPU resources, then run DX12 renderer validation.
4. Convert high-volume `Cfg()` reads only after subsystem-specific snapshots are
   defined and override order is documented.

## Validation Impact

- This report is documentation-only: no validation required.
- Future guardrail tooling under `tools/`: `tools\validate_fast.bat` plus the
  changed script.
- Renderer/context migration: `tools\validate_dx12_renderer.bat`.
- Physics config snapshots: `tools\validate_physics.bat`.
- Window/runtime flow or broad config migration: `tools\validate_full.bat`.
