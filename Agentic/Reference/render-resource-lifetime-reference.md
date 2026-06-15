# Render Resource Lifetime Reference

Purpose: map the current DX12 renderer resource lifetime so future reset, resize, shader, and material work has one source of truth.

DX12 is explicit: GPU objects can outlive the CPU call that submitted work against them. Any code that releases a texture, vertex buffer, framebuffer, descriptor heap, acceleration structure, command allocator, or upload arena must know whether the GPU can still read it. This reference names the current owners, rebuild triggers, and reset order.

## Glossary

| Term | Meaning |
|------|---------|
| CPU source asset | Stable engine data such as a texture path, shader base name, terrain raw path, or scene-authored material/style value. It survives resize and future device reset. |
| GPU resource | Backend-owned D3D12 object such as a texture, buffer, acceleration structure, descriptor heap row, framebuffer target, PSO, or root signature. |
| Descriptor | Small D3D12 binding record that tells shaders or output-merger stages how to interpret a resource. RTV, DSV, SRV, UAV, and sampler descriptors are examples. |
| RTV | Render Target View. Descriptor used when the GPU writes color pixels into a texture or swap-chain back buffer. |
| DSV | Depth Stencil View. Descriptor used for depth/stencil testing and depth writes. |
| SRV | Shader Resource View. Descriptor used when a shader reads a texture, buffer, or depth texture. |
| UAV | Unordered Access View. Descriptor used when a compute or raytracing shader reads and writes a resource out of normal render-target order. |
| FBO | Engine shorthand for an off-screen framebuffer. In DX12 it is a color/depth resource pair plus RTV, DSV, and SRV descriptors. |
| PSO | Pipeline State Object. Immutable DX12 draw-state package containing shaders, blend/depth/raster state, target formats, and input layout. |
| DRED | Device Removed Extended Data. D3D12 diagnostics that help explain GPU device removal or hangs. |
| In flight | Work has been submitted to the GPU, but the fence proving completion has not yet passed. |

## Current Startup Order

The active startup path lives in `SkullbonezRun::Initialise`.

1. Acquire the window singleton and write the loading title.
2. Create/bind `TextureCollection` to `RunSubsystemState::assets`.
3. Register built-in texture and shader source records.
4. Call `RebuildRegisteredRenderResources`.
5. Create terrain from the CPU terrain source path.
6. Create the skybox singleton and reset its render resources.
7. Create `WorldEnvironment` CPU state and align it to terrain bounds.
8. Ensure the reflection FBO at 2x the current back-buffer size.
9. Lazily create cinematic render resources if cinematic rendering is enabled.
10. Build the text font atlas and text shaders.
11. Acquire the camera singleton.
12. Load the first scene.

Important split:

- Source records are registered in `AssetSystem`.
- GPU texture handles are rebuilt by `TextureCollection::RebuildTexturesFromSourceAssets`.
- Shader source records are resolved through the active `AssetSystem` bridge, while GPU shader handles remain owned by their current systems.
- Pass-specific frame targets remain owned by `SkullbonezRun::RunSubsystemState`.

## Current Shutdown Order

The active shutdown path lives in `SkullbonezRun::~SkullbonezRun`.

1. End debug physics diagnostics.
2. Close perf logging.
3. Flush GPU work if the renderer is ready.
4. Reset world-environment render resources, then flush again because fluid mesh rebuild/reset can touch the command list.
5. Reset helper, game-model, collision-visualizer, UI, cinematic, shadow, and reflection resources while the backend still exists.
6. Invalidate GPU profiler queries.
7. Delete the text font resources.
8. Destroy textures, cameras, and skybox.
9. The backend shutdown releases DX12-owned resources.

The two key rules are:

- Pass/system GPU handles must be dropped before the backend device is destroyed.
- GPU work must be flushed before releasing resources that may still be referenced by submitted command lists.

## Current Resize Order

Window resize currently enters through `SkullbonezWindow::HandleScreenResize`.

1. Ignore minimized or pre-backend resize events.
2. Call `Gfx().Resize(w, h)`.
3. Rebuild the 2D text orthographic projection.
4. Rebuild the window projection matrix.

`RenderBackendDX12::Resize` then:

1. Waits for all GPU work.
2. Releases swap-chain back-buffer resources and the main depth texture.
3. Calls `ResizeBuffers`.
4. Re-acquires back buffers and rewrites their RTV rows.
5. Recreates the main depth texture and rewrites the main DSV row.
6. Updates viewport, scissor, current RTV, and current DSV.

Current lazy resize behavior:

- Cinematic scene and volumetric FBOs check size in `EnsureCinematicRenderResources`.
- Shadow FBOs check configured shadow map size in `EnsureShadowRenderResources`.

Phase 3 update:

- Reflection FBO now uses `EnsureReflectionRenderResources`, the same lazy size-check pattern used by cinematic and shadow targets.
- Pure resize does not rebuild shaders, meshes, textures, terrain, or physics state.

## Current Resource Ownership

| Resource group | Source owner | GPU owner | Rebuild trigger today | Notes |
|----------------|--------------|-----------|-----------------------|-------|
| Built-in texture paths | `AssetSystem` texture source records | `TextureCollection` plus `RenderBackendDX12` texture entries | Startup, `RebuildRegisteredRenderResources`, lazy `EnsureTexture` | Good source/GPU split already exists. |
| Built-in shader base names | `AssetSystem` shader source records | Per-system `unique_ptr<IShader>` or backend PSO cache | Lazy creation after reset | GPU handles are still scattered, but shader creation now resolves logical source records first. |
| Reflection FBO | Window dimensions and water reflection mode | `RunSubsystemState::reflectionFBO` | Startup, lazy ensure, window-size check, shutdown/reset | Uses a 2x back-buffer target and recreates only when dimensions or color format differ. |
| Cinematic scene FBO | Window dimensions, cinematic enabled state | `RunSubsystemState::sceneFBO` | Lazy ensure, size/format check | Correctly resize-aware. |
| Volumetric light FBO | Window dimensions, cinematic enabled state | `RunSubsystemState::volumetricLightFBO` | Lazy ensure, half-size/format check | Correctly resize-aware. |
| Shadow FBOs | Cinematic shadow config | `RunSubsystemState::shadowFBO`, `objectShadowFBO` | Lazy ensure, shadow-map size check | Reset clears frame payloads so receivers cannot sample stale depth. |
| Post-processing quad | Static full-screen quad layout | `RunSubsystemState::postQuadVB` | Lazy ensure, reset destroys dynamic VB handle | Dynamic VB is handle-only; backend has no per-handle resource. |
| Terrain mesh/shaders | Terrain CPU height data, shader source records | `Terrain` | Terrain creation, terrain reset, backend reset | Mesh should not rebuild for pure window resize. |
| Skybox meshes/shader | Sky bounds and cube texture source records | `SkyBox` | Startup/reset | Mesh should not rebuild for pure window resize. |
| Water meshes/shaders | World/fluid config, shader source records | `WorldEnvironment` | Startup/reset/style changes | Reset can record GPU commands, so shutdown flushes after touching it. |
| Object helper meshes/shaders | Built-in primitive mesh data, shader source records | `SkullbonezHelper` | Startup/reset | Shared across object rendering. |
| Game-model shadow resources | Shadow shader source and mesh data | `GameModelRenderer` | Startup/reset | Distinct from frame target lifetime. |
| Text font atlas/shaders | Font name, generated glyph atlas | `Text2d` | Startup, shutdown, projection resize | Projection changes on resize; atlas does not need resize. |
| UI blur resources | UI backdrop state and blur shader source | `UIBackdropBlur` | UI reset/resource reset | Cache reset is content/layout invalidation, not backend reset. |
| DXR reflection resources | Terrain/sphere BLAS source VAs and frame transforms | `RenderBackendDX12` DXR members | `InitDXR`, `ShutdownDXR`, reflection dispatch resize checks | Device-owned; release before shared backend resources. |
| Swap-chain back buffers | Window size | `RenderBackendDX12` | Backend init, resize, shutdown | Resize rewrites RTV descriptors in stable rows. |
| Main depth target | Window size | `RenderBackendDX12` | Backend init, resize, shutdown | Resize rewrites the stable DSV row. |
| Descriptor heaps | Backend configuration | `RenderBackendDX12` | Backend init/shutdown | Off-screen FBO descriptor rows are currently long-lived allocations. |
| PSO/root signature cache | Shader bytecode and draw-state descriptors | `RenderBackendDX12` | Lazy draw, shutdown | Cache is backend-owned and should not survive device reset. |

## Invalidation Reasons

Use these names in comments, logs, and future code:

| Reason | What should change | What should survive |
|--------|--------------------|---------------------|
| `BackendShutdown` | All GPU handles and backend caches release. | CPU scene, physics, source asset records. |
| `BackendRebuild` | GPU textures, shaders, meshes, frame targets, descriptors, PSOs rebuild from source records. | CPU scene, physics, source asset records. |
| `WindowResize` | Swap-chain buffers, main depth target, size-dependent FBOs, viewport/projection, UI/layout caches. | Shaders, meshes, textures, terrain CPU data, physics. |
| `SceneLoaded` | Scene CPU state, terrain source if changed, object instances, material/style values, some GPU streams. | Backend device and swap-chain resources. |
| `StyleReloaded` | Cinematic/material/style values and dependent shader constants or textures. | Backend device, physics bodies, terrain physics, most meshes. |
| `DeviceLost` | Same as backend rebuild, with stronger diagnostics and failure reporting. | CPU source records and scene state if recovery succeeds. |

## Rules For Future Edits

1. Keep source records outside backend resources.
2. Flush or prove the fence before releasing resources the GPU may still read.
3. Do not rebuild shaders or meshes for pure window resize.
4. Make size-dependent FBOs check their required size at the point of use.
5. Reset frame payloads when their underlying GPU texture is released.
6. Log named lifecycle phases before broad reset/rebuild work.
7. Treat device loss as a backend rebuild from source records, not as a scene reload.
8. Use logical shader names such as `shader.water_ocean` at system call sites; let `AssetSystem` translate them to backend shader base paths.
