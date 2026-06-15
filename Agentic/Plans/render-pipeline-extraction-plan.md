# Render Pipeline Extraction Plan

Status: planning draft  
Created: 2026-06-11  
Scope: render pass scheduling, shader binding ownership, renderer-neutral pass structure  
Implementation status: plan only, no code changes in this pass

Retirement dependency: keep render-pass extraction behind the DX12-only renderer
validation gate unless a small preparatory slice directly supports that gate.
Do not add new OpenGL or DX11 pass behavior during retirement.

## Goal

Extract the current frame rendering flow into named render passes without changing output. The first objective is not a new renderer. It is to make the existing order explicit, move shader setup into pass-owned binders, and reduce `SkullbonezRun::DrawPrimitives()` from detailed rendering logic to orchestration.

This plan pairs with:

- `Agentic/Plans/shader-architecture-cleanup-plan.md`
- `Agentic/Plans/material-system-v1-implementation-plan.md`
- `Agentic/Plans/render-resource-lifetime-plan.md`
- `Agentic/Plans/post-cinematic-stack-plan.md`

## Current Read

The current renderer is functionally a forward renderer with optional HDR/post processing:

1. Prepare object render streams.
2. Render the skybox or generated cinematic sky.
3. Render water reflection through FBO mirror camera or DXR.
4. Optionally bind HDR scene framebuffer.
5. Render sky into the scene target.
6. Render objects.
7. Render terrain.
8. Render projected shadows.
9. Render water.
10. Render debug overlays.
11. Generate volumetric light texture.
12. Tonemap/resolve HDR scene target to backbuffer.
13. Draw UI and text later.

The ordering is reasonable. The problem is ownership:

- `SkullbonezRunRender.cpp` decides pass order, render targets, view matrices, reflection mode, cinematic mode, shader creation, dynamic fullscreen quad creation, texture binding, and final resolve behavior.
- `SkullbonezHelper.cpp` owns object shader setup and instanced mesh caches.
- `SkullbonezTerrain.cpp` owns terrain shader setup.
- `SkullbonezWorldEnvironment.cpp` owns water shader setup.
- UI and text own their own renderer-adjacent shaders.

This is workable, but adding material and style data will spread even more shader setup unless pass boundaries are named.

## Non-Goals

- Do not rewrite the backend interface first.
- Do not change rendering output in the extraction phase.
- Do not introduce a deferred renderer.
- Do not add a frame graph scheduler yet.
- Do not move UI/text into the world render pipeline.
- Do not combine this with material instance payload changes.

## Target Shape

Keep `SkullbonezRun` as the coordinator, but introduce pass objects or pass helper modules:

```cpp
struct RenderFrameContext
{
    Matrix4 view;
    Matrix4 projection;
    Matrix4 reflectionView;
    Matrix4 reflectionVP;
    Vector3 eye;
    float lightPosition[4];
    float waterY;
    float time;
    bool cinematicEnabled;
};

class IRenderPass
{
public:
    virtual void ResetRenderResources() = 0;
    virtual void Render(const RenderFrameContext& frame) = 0;
};
```

This does not need to be a polymorphic interface in phase 1. Plain structs with named functions are enough:

- `ReflectionPass_Render(...)`
- `SkyPass_Render(...)`
- `ObjectPass_Render(...)`
- `TerrainPass_Render(...)`
- `ShadowPass_Render(...)`
- `WaterPass_Render(...)`
- `VolumetricPass_Render(...)`
- `TonemapPass_Render(...)`

Use the simplest form that reduces ownership pressure.

## Proposed Passes

### Reflection Pass

Responsibilities:

- Decide FBO mirror path vs DXR reflection path.
- Compute reflected camera matrices from the render camera, not selected destination camera.
- Bind reflection FBO and viewport for the FBO path.
- Render reflected sky and objects.
- Own the clip-plane enable/disable sequence for reflected object rendering.
- Produce a `reflectionTextureHandle` and `reflectionSampleVP`.

Inputs:

- frame camera data,
- water height,
- renderer capabilities,
- water debug flags,
- collision visualizer state,
- game model collection,
- sky pass hook for reflected sky.

Outputs:

```cpp
struct ReflectionPassOutput
{
    uint32_t reflectionTexture;
    Matrix4 reflectionSampleVP;
    bool usedDXR;
};
```

Validation risk:

- High. Reflection is cross-renderer and water-sensitive.

### Sky Pass

Responsibilities:

- Render cube-map skybox in normal mode.
- Render procedural sky in cinematic/style mode.
- Support reflection pass reuse.
- Own depth/blend state restore for fullscreen sky.

Inputs:

- view/projection,
- eye position,
- active style/cinematic config.

Outputs:

- Draws into current render target.

Validation risk:

- Medium. Sky affects baselines broadly.

### Object Pass

Responsibilities:

- Prepare and draw normal objects through production material shader.
- Route collision visualizer mode separately.
- Bind object lighting and material/style params.
- Keep batching for spheres, boxes, and pines.

Inputs:

- game model collection,
- view/projection,
- light,
- material/style data,
- collision debug state.

Outputs:

- Draws production objects or collision visualization.

Validation risk:

- High. Object path affects most screenshots and performance.

### Terrain Pass

Responsibilities:

- Bind terrain texture/material/style params.
- Render terrain unless hidden.
- Keep terrain relief visual-only and separate from physics terrain.

Inputs:

- terrain,
- view/projection,
- light,
- terrain style config,
- texture handles.

Validation risk:

- High for renderer baselines.

### Shadow Pass

Responsibilities:

- Render projected ground shadows after terrain and before water.
- Own shadow instanced mesh/shader resources.
- Respect terrain-hidden state.

Validation risk:

- Medium. Alpha blending and depth state can drift by backend.

### Water Pass

Responsibilities:

- Decide calm/ocean/water-hidden modes.
- Bind reflection texture and sample matrix from `ReflectionPassOutput`.
- Bind water style/material params.
- Render calm basin and/or ocean plane.

Validation risk:

- High. Existing known bug around water/back faces belongs near this pass.

### Debug Overlay Pass

Responsibilities:

- Broadphase visualizer.
- Physics debug visualizer.
- Optional renderer debug line/grid overlays.

Keep this separate from production object materials.

Validation risk:

- Medium. Debug overlays may not be in normal renderer validation scenes but can affect UI/manual workflows.

### Volumetric Pass

Responsibilities:

- Unbind scene target as needed.
- Bind half-res volumetric framebuffer.
- Bind scene color/depth.
- Render fullscreen light shaft texture.
- Return whether volumetric texture is valid.

Validation risk:

- High in cinematic scenes and DX12 resource state handling.

### Tonemap Pass

Responsibilities:

- Resolve scene HDR target to backbuffer.
- Bind scene color/depth/volumetric texture.
- Apply exposure/gamma/fog/bloom/grade.
- Restore depth/blend state.

Validation risk:

- High. Final pass affects whole image.

## Shared Frame Context

Add a `RenderFrameContext` built once near the top of `DrawPrimitives()`:

```cpp
struct RenderFrameContext
{
    Matrix4 baseView;
    Matrix4 projection;
    Matrix4 reflectionView;
    Matrix4 reflectionVP;
    Matrix4 viewProjection;
    Vector3 eye;
    Vector3 viewCenter;
    Vector3 up;
    float lightPosition[4];
    float waterY;
    float simulationTime;
    bool cinematicEnabled;
    const CinematicRenderConfig* cinematic;
};
```

Benefits:

- Reduces repeated camera queries.
- Makes reflection and main pass use the same render camera.
- Makes pass extraction safer because data dependencies are explicit.

## Resource Ownership

Do not move resources all at once. Start with a pass facade over existing resources.

Initial ownership:

- `SkullbonezRun::Systems` may still hold FBOs and shaders.
- Pass binders receive references to existing resources.

Future ownership:

- `ReflectionPass` owns `reflectionFBO` or receives it from a shared target manager.
- `SkyPass` owns `skyAtmosphereShader`.
- `VolumetricPass` owns `volumetricLightFBO` and shader.
- `TonemapPass` owns `tonemapShader` and fullscreen quad handle.

Tie final ownership into `render-resource-lifetime-plan.md`.

## Phase Plan

### Phase 1: Extract Frame Context

Tasks:

1. Build `RenderFrameContext` at the top of `DrawPrimitives()`.
2. Replace local repeated camera/light/water values with frame context fields.
3. Do not move pass code yet.

Validation:

- `tools\validate_renderers.bat`.

### Phase 2: Extract Bind Helpers

Tasks:

1. Add binder functions for sky, object, terrain, water, volumetric, and tonemap params.
2. Keep them in the current `.cpp` or a new renderer-adjacent file.
3. Replace repeated `Set*` blocks.
4. Keep all draw order unchanged.

Validation:

- `tools\validate_renderers.bat`.

### Phase 3: Extract Reflection Pass Function

Tasks:

1. Move reflection FBO/DXR logic into one function.
2. Return `ReflectionPassOutput`.
3. Keep sky/object reflected draw callbacks simple.

Validation:

- `tools\validate_renderers.bat`.
- Manually check water reflection scene if validation does not cover enough.

### Phase 4: Extract Scene Pass Functions

Tasks:

1. Extract sky pass.
2. Extract object pass.
3. Extract terrain pass.
4. Extract shadow pass.
5. Extract water pass.

Validation:

- `tools\validate_renderers.bat` before committing each slice or tightly scoped pair.

### Phase 5: Extract Post Pass Functions

Tasks:

1. Extract volumetric pass.
2. Extract tonemap pass.
3. Centralize fullscreen quad handling.

Validation:

- `tools\validate_renderers.bat`.
- DX12 validation log must remain zero-error.

### Phase 6: Introduce Pass Resource Objects

Tasks:

1. Move pass-specific shader/FBO handles into pass structs.
2. Add reset/rebuild hooks.
3. Integrate with renderer-switch resource reset order.

Validation:

- `tools\validate_full.bat` if renderer switching or runtime resource phases are touched.

## Validation Matrix

These commands are targeted pre-commit/PR gates, not as-you-go validation.

| Change | Validation |
|--------|------------|
| Documentation only | No validation required |
| Frame context only | `tools\validate_renderers.bat` |
| Shader bind helper extraction | `tools\validate_renderers.bat` |
| Reflection pass extraction | `tools\validate_renderers.bat` |
| Post pass extraction | `tools\validate_renderers.bat` |
| Pass-owned resource lifecycle | `tools\validate_full.bat` |
| DX12 target/barrier changes | `tools\validate_renderers.bat` and verify `dx12_validation.txt` is zero |

## Risks

| Risk | Mitigation |
|------|------------|
| Changing pass order accidentally | Keep extraction mechanical; compare screenshots after every phase. |
| State restore bugs | Add pass-level state save/restore helpers for depth, blend, viewport, and targets. |
| Reflection uses wrong camera | Build reflection matrices from `RenderFrameContext` render camera fields only. |
| DX12 resource transitions regress | Avoid backend resource changes during pass extraction; validate separately. |
| Runtime renderer switching breaks pass resources | Delay resource ownership move until reset hooks are planned. |

## Success Criteria

- `DrawPrimitives()` reads as a short ordered list of passes.
- Each pass owns its shader binding logic.
- Existing output is unchanged within renderer validation thresholds.
- New material/style data has clear pass insertion points.
- Future agents can work on water, post, objects, or terrain without reading the entire frame renderer.
