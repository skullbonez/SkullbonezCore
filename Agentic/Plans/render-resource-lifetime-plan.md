# Render Resource Lifetime Plan

Status: active implementation
Created: 2026-06-11  
Scope: renderer-owned resources, resize, shader/mesh/FBO lifetime, future device loss
Implementation status: Phase 1 current-lifetime reference is now captured in `Agentic/Reference/render-resource-lifetime-reference.md`.

## Goal

Formalize how renderer-owned resources are created, invalidated, rebuilt, and destroyed across:

- startup,
- scene load,
- window resize,
- fullscreen target resize,
- style/material reload,
- future device loss,
- future Vulkan/Metal backend migration paths.

The current code already has careful reset phases. This plan turns that careful procedure into an explicit resource-lifetime architecture so future shader/material work does not add more ad hoc reset paths. DX12 is now the only runtime renderer, so device-loss, resize, descriptor, upload, and pass-target lifetimes are the active concerns.

## Current Read

Renderer-owned resources are spread across many systems:

- `SkullbonezRun` systems:
  - reflection FBO,
  - cinematic scene FBO,
  - volumetric FBO,
  - post quad dynamic VB,
  - sky/post shaders.
- `SkullbonezHelper`:
  - shared object shader,
  - instanced sphere/box/pine meshes,
  - instance data buffers.
- `GameModelCollection`:
  - shadow shader,
  - shadow instanced mesh.
- `Terrain`:
  - terrain mesh,
  - terrain shader.
- `SkyBox`:
  - face meshes,
  - unlit shader.
- `WorldEnvironment`:
  - water meshes,
  - calm/ocean water shaders.
- `Text2d`:
  - text shader,
  - solid shaders,
  - dynamic VBs,
  - font texture.
- UI:
  - backdrop blur shader,
  - blur texture,
  - dynamic buffers.
- Backends:
  - textures,
  - SRVs,
  - framebuffers,
  - dynamic VBs,
  - instanced meshes,
  - DXR resources,
  - PSOs and root signatures.

The retired renderer hot-switching path proved that CPU source data must survive while GPU resources are rebuilt. That separation remains valuable: DX12 device loss, resize, shader/material reloads, and future Vulkan/Metal backend work all need clean source-vs-GPU ownership.

## Main Problems

### 1. Source Assets And GPU Resources Are Blended

Example distinction:

- Source asset: `sky1.jpg`, terrain raw data, shader base name, scene style material name.
- GPU resource: DX12 resource/descriptor, future Vulkan image/view, future Metal texture, mesh VB, FBO color texture.

Some systems know both sides. That makes device reset and future backend migration harder because only the GPU side should be invalidated.

### 2. Reset Order Is Important But Not Fully Encoded

Some resources depend on others:

- Water samples reflection texture.
- Post passes sample scene/depth/volumetric textures.
- Text uses font atlas texture and dynamic VBs.
- DX12 descriptors reference resources that must not be released while in flight.
- Renderer switch or DX12 device reset must flush/finish GPU before destroying backend resources.

The order exists in runtime code, but the dependency model is not explicit enough.

### 3. Resize And Backend/Device Reset Are Different Events

Not every resource should rebuild on every event:

- Shader programs rebuild on backend/device reset, not resize.
- Swapchain-dependent FBOs rebuild on resize.
- Terrain mesh rebuilds on backend/device reset, not resize.
- Font atlas texture rebuilds on backend/device reset, maybe not resize.
- UI blur cache rebuilds on resize/content change.

The architecture should encode invalidation reasons.

## Target Model

Introduce a renderer resource registry eventually. Start with explicit resource traits and reset phases.

```cpp
enum class RenderResourceInvalidation
{
    BackendDestroyed,
    BackendCreated,
    WindowResized,
    SceneLoaded,
    StyleReloaded,
    MaterialTableChanged,
    DeviceLost
};

class IRenderResource
{
public:
    virtual const char* Name() const = 0;
    virtual void ReleaseGpuResources() = 0;
    virtual void RebuildGpuResources(RenderDeviceContext& context) = 0;
    virtual void OnResize(int width, int height) {}
};
```

Do not introduce this interface before pass/resource ownership is ready. In the first implementation slice, a table of named callbacks is enough.

## Resource Categories

### Backend Core Resources

Owned by backend:

- device/context/command queue,
- swapchain/backbuffer,
- root signatures,
- descriptor heaps,
- common samplers/states,
- depth target,
- GPU timer resources,
- PSO cache.

Lifetime:

- Created during backend `Init`.
- Destroyed during backend `Shutdown`.
- Recreated during renderer switch or DX12 device reset.
- Backbuffer/depth resized during `Resize`.

### Source Asset Records

Owned by future asset registry:

- texture path,
- shader base name,
- mesh source id,
- material preset name,
- style file path.

Lifetime:

- Survive renderer switch and DX12 device reset.
- Survive resize.
- May reload on source file change or scene/style load.

### GPU Asset Instances

Examples:

- texture handles,
- shader programs,
- mesh VBs,
- dynamic VBs,
- instanced mesh handles.

Lifetime:

- Backend-specific.
- Invalid on renderer switch or DX12 device reset.
- Usually valid across resize.
- Rebuilt from source asset records.

### Frame Targets

Examples:

- reflection FBO,
- HDR scene FBO,
- half-res volumetric FBO,
- depth textures.

Lifetime:

- Backend-specific.
- Invalid on renderer switch or DX12 device reset.
- Resize-dependent.
- Format-dependent.

### Pass Resources

Examples:

- post fullscreen quad dynamic VB,
- pass shaders,
- shadow mesh,
- water meshes.

Lifetime:

- Backend-specific.
- Often invalid on renderer switch or DX12 device reset.
- Some invalid on style/material mode changes only if shader variants are introduced.

## Recommended Ownership

| Resource | Near-Term Owner | Long-Term Owner |
|----------|-----------------|-----------------|
| Reflection FBO | `SkullbonezRun::Systems` | `ReflectionPass` or `RenderTargetManager` |
| HDR scene FBO | `SkullbonezRun::Systems` | `SceneColorPass` or `RenderTargetManager` |
| Volumetric FBO | `SkullbonezRun::Systems` | `VolumetricPass` |
| Tonemap shader | `SkullbonezRun::Systems` | `TonemapPass` |
| Sky atmosphere shader | `SkullbonezRun::Systems` | `SkyPass` |
| Object shader/meshes | `SkullbonezHelper` | `ObjectRenderResources` |
| Shadow shader/mesh | `GameModelCollection` | `ShadowPass` |
| Terrain shader/mesh | `Terrain` | `TerrainRenderComponent` |
| Water shaders/meshes | `WorldEnvironment` | `WaterPass` |
| Text shaders/font atlas | `Text2d` | `TextRenderer` |
| UI blur resources | UI blur helper | `UIBlurPass` |

Do not move all ownership at once. Stabilize reset contracts first.

## Lifecycle Events

### Startup

Expected order:

1. Load config/source paths.
2. Initialize backend.
3. Initialize source asset records.
4. Build GPU resources on demand or during preload.
5. Load scene and create scene CPU state.
6. Build render resources needed by scene.

### Device Reset Or Backend Migration

Required high-level order:

1. Stop using old backend this frame.
2. Finish/flush GPU for old backend.
3. Release pass/system GPU resources.
4. Shutdown old backend.
5. Initialize new backend.
6. Rebuild pass/system GPU resources from CPU source data.
7. Rebind scene textures/materials.
8. Resume rendering.

No source asset or physics state should be destroyed by DX12 device reset or future backend migration.

DX12-only production has retired runtime hot-switching. Preserve the underlying release/rebuild discipline for DX12 device reset and future Vulkan/Metal backend bring-up.

### Resize

Required high-level order:

1. Finish or synchronize backend resize as required.
2. Resize swapchain/backbuffer/depth.
3. Invalidate size-dependent FBOs.
4. Recreate reflection/HDR/volumetric targets on next use.
5. Update viewport.
6. Rebuild UI/layout caches as needed.

Shaders and meshes should not rebuild for pure resize unless a backend requires it.

### Scene Load

Scene load can change:

- object count,
- terrain data,
- texture paths,
- material assignments,
- water mode,
- style config,
- screenshot/validation settings.

Scene load should not require backend recreation. It may require:

- terrain mesh rebuild,
- texture load/unload,
- material table rebuild,
- pass target resize if scene changes output dimensions,
- object instance stream rebuild.

### Style Reload

Style reload should change:

- style params,
- material assignments,
- maybe texture references.

Style reload should not rebuild terrain physics, object physics, or backend core resources.

## Phase Plan

### Phase 1: Document Current Reset Phases

Tasks:

1. Add or update a reference doc listing current backend/device reset order.
2. For each resource group, mark:
   - source data owner,
   - GPU data owner,
   - rebuild trigger.
3. Do not change code.

Validation:

- Documentation only: no validation required.

Status:

- Done on branch `codex/render-resource-lifetime-dx12`.
- Reference: `Agentic/Reference/render-resource-lifetime-reference.md`.
- Noted concrete follow-up: reflection FBO needs an explicit resize invalidation/check.

### Phase 2: Add Named Resource Phase Table

Tasks:

1. Convert backend/device release/rebuild calls into a table of named steps if not already complete.
2. Ensure every step logs enough context in debug/dev mode.
3. Keep the same order.

Validation:

- `tools\validate_dx12_renderer.bat`.
- `tools\validate_full.bat` if runtime switching behavior changes broadly.

### Phase 3: Split Resize Invalidations

Tasks:

1. Separate backend/device invalidation from resize invalidation.
2. Add target-size checks inside FBO owners.
3. Avoid shader/mesh rebuilds on resize.

Validation:

- `tools\validate_dx12_renderer.bat`.

Status:

- Done on branch `codex/render-resource-lifetime-dx12`.
- Reflection FBO now has an explicit `EnsureReflectionRenderResources` size check and `ResetReflectionRenderResources` teardown path.
- Startup and render-time reflection target creation now share the same lazy owner check.
- Pure resize remains limited to swap-chain/depth/projection plus size-dependent FBO recreation; shaders and meshes are not rebuilt for resize.

### Phase 4: Add Source Asset Records

Tasks:

1. Use or extend existing `Assets::AssetSystem` scaffold.
2. Store shader base names, texture paths, and material source names separately from GPU handles.
3. Rebuild GPU resources from source records after backend switch.

Validation:

- `tools\validate_dx12_renderer.bat`.

Status:

- Done on branch `codex/render-resource-lifetime-dx12`.
- The existing `Assets::AssetSystem` remains the source owner for texture paths and shader base names.
- Added an active asset-system bridge so legacy render helpers can resolve logical shader names without taking ownership of the run-level asset registry.
- Routed helper, terrain, skybox, water, text, collision visualizer, and UI blur shader creation through source records while leaving GPU shader handles in their existing owners.
- Material/style GPU records are not introduced yet because there is no material GPU table in this plan slice; style data still remains CPU-side scene/config state.

### Phase 5: Pass-Owned Resources

Tasks:

1. As render passes are extracted, let each pass own its resources.
2. Register pass reset hooks.
3. Keep construction lazy if startup time matters.

Validation:

- `tools\validate_full.bat` if pass ownership changes runtime lifecycle.

Status:

- Done for the current non-extracted renderer shape on branch `codex/render-resource-lifetime-dx12`.
- The ordered release hook table now lives in `SkullbonezRun::ReleaseBackendOwnedRenderResources` instead of being destructor-local.
- Future pass modules can move from table rows to pass-owned hooks without changing the high-level release/rebuild order.
- No render pass ownership move was attempted here because pass extraction was tracked separately in `Agentic/Plans/Done/render-pipeline-extraction-plan.md`.

### Phase 6: Future Device-Lost Path

Tasks:

1. Treat device lost as backend/device reset without a renderer type change.
2. Reuse the release/rebuild machinery.
3. Add clear diagnostics for failed rebuild step.

Validation:

- Manual fault injection if possible.
- `tools\validate_full.bat`.

Status:

- Done as a diagnostic/recovery-prep slice on branch `codex/render-resource-lifetime-dx12`.
- `Present` and `ResizeBuffers` now check device-removal HRESULTs, write `dx12_device_lost.txt`, emit event-log rows, and capture DRED breadcrumb/page-fault heads when available.
- Device-lost live hot recovery is not enabled yet; the release/rebuild machinery is now reusable, and real in-frame recovery should be added after pass extraction gives each pass explicit rebuild hooks.

## Validation Matrix

| Change | Validation |
|--------|------------|
| Resource lifetime docs | No validation required |
| Backend/device phase table refactor | `tools\validate_dx12_renderer.bat` |
| Resize invalidation changes | `tools\validate_dx12_renderer.bat` |
| Backend resource destruction/rebuild changes | `tools\validate_dx12_renderer.bat` and DX12 validation log check |
| Broad runtime lifecycle changes | `tools\validate_full.bat` |
| Hot-path resource cache changes | `tools\validate_perf.bat` in addition to render validation |

## Risks

| Risk | Mitigation |
|------|------------|
| Releasing resource while GPU still uses it | Always finish/flush before backend switch or device-reset destruction; be strict in DX12. |
| Rebuilding resources in wrong order | Use named ordered tables and logs. |
| Resize triggers unnecessary rebuild work | Separate resize invalidation from backend/device invalidation. |
| Source data lost during renderer switch or reset | Store source asset records outside backend resources. |
| New pass resources miss reset hooks | Require every pass to implement or register reset behavior before extraction is accepted. |

## Success Criteria

- Backend/device reset and resize are explicit lifecycle events.
- Each renderer-owned resource has a documented owner and rebuild trigger.
- Future material/shader resources can be added without searching the whole runtime for reset paths.
- Source data survives backend switching, DX12 device reset, and future backend migration.
- DX12 resources are never released or reused while GPU work is in flight.
