# DX12-Only Engine Architecture Plan

Status: planning draft  
Created: 2026-06-11  
Scope: render architecture, DX12 device layer, render graph, materials, resources, diagnostics  
Implementation status: plan only, no code changes in this pass

## Goal

Describe a clean DirectX 12-first renderer architecture for SkullbonezCore now that DX12 is the official production graphics API.

The old GL/DX11/DX12 parity model is no longer the product support contract. DX12 is the official production renderer. GL and DX11 are retained as legacy parity/reference backends while they remain in tree so they can catch useful migration bugs, visual drift, and convention mistakes. New renderer architecture should assume DX12 owns the production path and should embrace explicit GPU resource ownership, descriptor heaps, command lists, shader model 6, and PIX-first diagnostics.

Future Vulkan and Metal support should remain possible, but not by keeping the old `IRenderBackend` shape alive indefinitely. The engine should define its own render-pass, resource, material, shader-metadata, and synchronization contracts. DX12 is the first concrete implementation; Vulkan and Metal can map to those contracts later.

Renderer retirement policy:

- Do not build new features around GL or DX11 unless doing so directly helps DX12 migration or parity diagnosis.
- Keep GL and DX11 in renderer validation while they remain available.
- Build DX12-only validation before deleting parity backends: screenshot baselines, zero D3D12 debug-layer errors, WARP sanity where practical, GPU-based validation for representative scenes, DRED/PIX diagnostics, descriptor/upload counters, and repeated DX12 stress runs for barrier/upload changes.
- Retire OpenGL first because it is least aligned with the future architecture.
- Retire DX11 after DX12 diagnostics can replace the simpler DirectX reference path.

## Design Position

A DX12-only engine should not be a thin port of the current `IRenderBackend` shape. It should keep the current high-level frame order, but replace the backend abstraction with a native DX12 render device, a render graph, explicit resource lifetime, and GPU-visible material/resource tables.

Current renderer shape to preserve at the top:

- named frame passes,
- deterministic screenshot validation,
- runtime scene/style data,
- water reflection, cinematic HDR, volumetric, tonemap, UI, and debug overlays,
- zero-warning and zero-DX12-validation-error discipline.

Current renderer shape to retire or narrow:

- global `Gfx()` as the main API boundary,
- per-backend shader abstraction,
- name-based shader setters as the hot-path interface,
- fixed `t0`, `t1`, `t2` texture-slot thinking,
- pass resources scattered across `SkullbonezRunRender.cpp`, helper classes, terrain, water, sky, and UI,
- GL/DX coordinate compatibility constraints inside new DX12 code.

Portability constraint:

- Keep high-level render data API-neutral: passes, resource access, material properties, vertex layouts, and shader reflection metadata should use engine terms.
- Allow DX12 implementation details inside `Dx12RenderDevice`, root signatures, descriptor heaps, PSOs, barriers, and DXR systems.
- Do not add a Vulkan/Metal abstraction before the DX12 render graph and material/resource model are stable.
- When choosing a data model, prefer shapes that can later map to Vulkan descriptors/SPIR-V and Metal argument buffers/MSL without changing scene or material authoring.

## Architecture Overview

```text
EngineRuntime
  |
  +-- RenderSystem
        |
        +-- Dx12RenderDevice
        |     |-- Adapter/device/swap chain
        |     |-- Graphics/copy/compute queues
        |     |-- Fence timelines
        |     |-- Descriptor heaps
        |     |-- Resource allocator
        |     |-- Upload/readback allocators
        |
        +-- ShaderSystem
        |     |-- DXC compile/cache
        |     |-- Shader reflection
        |     |-- Root signature library
        |     |-- PSO cache
        |
        +-- RenderGraph
        |     |-- Pass declarations
        |     |-- Resource declarations
        |     |-- Barrier scheduling
        |     |-- Transient target lifetime
        |     |-- Command list recording
        |
        +-- RenderWorld
        |     |-- Renderable extraction
        |     |-- Mesh/material/texture handles
        |     |-- Per-view render lists
        |
        +-- MaterialSystem
        |     |-- CPU material registry
        |     |-- GPU material buffer
        |     |-- Texture descriptor indices
        |
        +-- Diagnostics
              |-- D3D12 debug layer
              |-- GPU-based validation profile
              |-- DRED breadcrumbs
              |-- PIX markers/captures
              |-- Pass captures and screenshot baselines
```

The important shift is that `RenderGraph` becomes the place where pass order, resource access, and barriers are declared. Individual passes no longer manually remember whether a texture is currently a render target, shader resource, depth buffer, UAV, or presentable backbuffer.

## Frame Flow

The first DX12-only renderer should stay close to the current forward/cinematic path. Do not jump directly to a complex deferred renderer until the resource model is stable.

Recommended frame order:

1. Build `FrameContext`.
2. Extract scene into `RenderWorldSnapshot`.
3. Upload per-frame constants, instance data, material changes, and dirty texture data.
4. Build render lists by pass.
5. Execute render graph:
   - shadow map pass,
   - optional depth prepass,
   - reflection pass,
   - sky pass,
   - opaque object pass,
   - terrain pass,
   - ground shadow or contact cue pass,
   - water/transparent pass,
   - debug overlay pass,
   - volumetric light pass,
   - post stack,
   - UI/text pass,
   - present.
6. Submit command lists.
7. Present.
8. Retire per-frame resources when fences complete.

Target graph:

```text
Backbuffer
  ^
  |
ToneMapPass
  ^
  | scene color, scene depth, volumetric light
  |
VolumetricLightPass
  ^
  | scene color, scene depth
  |
SceneColorTarget + SceneDepthTarget
  |-- SkyPass
  |-- OpaqueObjectPass
  |-- TerrainPass
  |-- GroundContactShadowPass
  |-- WaterPass
  |-- DebugOverlayPass
       ^
       |
ReflectionPassOutput
       ^
       |
ReflectionPass
       ^
       |
ShadowMapPass
```

Optional future passes:

- depth pyramid / hierarchical Z,
- GPU culling,
- clustered light binning,
- real bloom prefilter/downsample/blur,
- color grading LUT,
- temporal volumetric accumulation,
- screen-space reflections,
- deferred or hybrid G-buffer path.

## Core Modules

### 1. Dx12RenderDevice

Owns all platform and D3D12 objects.

Responsibilities:

- adapter selection,
- device creation,
- swap chain creation,
- queue creation,
- debug layer setup,
- info queue filters,
- DRED setup,
- fence timelines,
- frame index management,
- resize and present,
- GPU crash breadcrumbs,
- device-lost handling.

Public API should be explicit and low-level:

```cpp
class Dx12RenderDevice
{
public:
    Dx12CommandQueue& GraphicsQueue();
    Dx12CommandQueue& CopyQueue();
    Dx12CommandQueue& ComputeQueue();

    Dx12DescriptorAllocator& CbvSrvUavAllocator();
    Dx12DescriptorAllocator& RtvAllocator();
    Dx12DescriptorAllocator& DsvAllocator();
    Dx12DescriptorAllocator& SamplerAllocator();

    Dx12ResourceAllocator& Resources();
    Dx12UploadSystem& Uploads();
    Dx12ReadbackSystem& Readbacks();

    uint32_t BeginFrame();
    void EndFrame();
    void Present();
    void WaitIdle();
};
```

Avoid hiding DX12 concepts so deeply that the rest of the renderer cannot reason about hazards. A DX12-only engine should make state, queues, descriptors, and fences visible to renderer infrastructure, while still hiding raw COM lifetime and repetitive setup.

### 2. Frame Resources

Use N buffered frame resources, usually 2 or 3.

Each frame owns:

- command allocators,
- transient upload pages,
- transient descriptor ranges,
- per-frame constant allocations,
- per-frame instance/material upload regions,
- temporary CPU vectors used for render-list building,
- a fence value for safe reclamation.

Rules:

- Never overwrite per-frame upload memory until its fence has completed.
- Never reset a command allocator until its fence has completed.
- Never recycle shader-visible descriptors while the GPU can still read them.
- All transient allocations should report peak usage.

### 3. Resource Allocator

Use one DX12 resource allocator layer, either repository-owned or based on D3D12 Memory Allocator if the project accepts that dependency.

Resource classes:

- buffers,
- textures,
- render targets,
- depth targets,
- UAV targets,
- readback buffers,
- upload buffers,
- swap chain images.

Required metadata per resource:

- debug name,
- dimensions/format,
- heap type,
- initial state,
- current tracked state,
- permanent descriptor handles,
- owning system or render graph allocation,
- last writer pass in debug builds.

The allocator should make committed resources first. Move to placed resources and heap aliasing only after the render graph is stable.

### 4. Descriptor Model

DX12-only should move away from three fixed SRV slots.

Recommended descriptor heaps:

- one large shader-visible CBV/SRV/UAV heap,
- one shader-visible sampler heap,
- CPU-only RTV heap,
- CPU-only DSV heap,
- CPU staging descriptors where needed.

Use stable descriptor indices for persistent resources:

```cpp
struct TextureHandle
{
    uint32_t resourceId;
    uint32_t srvIndex;
};

struct BufferHandle
{
    uint32_t resourceId;
    uint32_t srvIndex;
    uint32_t uavIndex;
};
```

For the first DX12-only version, prefer descriptor indexing through a global texture table:

- `Texture2D gTextures[] : register(t0, space1);`
- material stores texture descriptor indices,
- instance stores material index,
- pass constants store graph texture indices for scene color/depth/post inputs.

Keep explicit bound descriptors for special render graph resources if descriptor indexing creates too much initial risk. The architecture should still be able to migrate to descriptor indexing cleanly.

### 5. Root Signature Strategy

Use a small number of stable root signatures, not one root signature per pass.

Recommended v1 root shape:

```text
Root 0: frame/pass constants CBV
Root 1: draw/object constants CBV or root constants
Root 2: material buffer SRV
Root 3: instance buffer SRV
Root 4: descriptor table for bindless/global textures
Root 5: descriptor table for graph-local SRVs/UAVs if needed
Static samplers: linear wrap, linear clamp, point clamp, shadow comparison
```

Specialized root signatures are acceptable for:

- compute-only post passes,
- DXR raytracing,
- copy/generate-mips compute,
- UI/text if it stays simpler.

Rules:

- Root signatures are declared in C++ and reflected into docs/logs.
- PSO cache keys include root signature identity.
- Shader reflection must fail loudly if shader resource bindings do not match the root contract.
- Do not use root descriptors for anything with complicated lifetime.

### 6. Shader System

DX12-first means HLSL should become the canonical shader source for production. Do not preserve duplicated GLSL/HLSL authoring as a long-term requirement.

Recommended stack:

- DXC compiler,
- Shader Model 6.x,
- checked-in source HLSL,
- optional checked-in DXIL cache only if build speed needs it,
- reflection output cached as engine metadata,
- shader PDBs and names for PIX.

Portability rule:

- Treat shader metadata as engine data, not D3D12-only trivia.
- Keep entry points, vertex layouts, constant buffers, resource bindings, register spaces, and material feature flags explicit enough to cross-compile or translate later.
- If Vulkan support becomes active, prefer a deliberate HLSL-to-SPIR-V pipeline or a shader language toolchain evaluation over reviving manually synchronized shader pairs.
- If Metal support becomes active, assume either a native Metal backend with translated shaders or a Vulkan portability route through MoltenVK; do not depend on every Vulkan feature being available on Apple platforms.

Shader artifact metadata:

```cpp
struct ShaderProgramDesc
{
    const char* name;
    ShaderStageMask stages;
    VertexLayoutId vertexLayout;
    RootSignatureId rootSignature;
    Span<ResourceBindingDecl> resources;
    Span<ConstantBufferDecl> constantBuffers;
    Span<PermutationDecl> permutations;
};
```

Shader conventions:

- common include files under `SkullbonezData/shaders/common/`,
- one HLSL file per shader family where practical,
- shared structs for CPU/GPU constants generated or manually mirrored with static asserts,
- explicit register spaces:
  - space 0 for pass-local resources,
  - space 1 for global descriptor tables,
  - space 2 for debug resources,
  - space 3 for raytracing resources.

Avoid a material graph initially. Use typed materials plus data-driven shader branches. Add shader permutations only for structural differences, not every art style.

### 7. PSO Cache

DX12 needs PSO ownership as a first-class system.

PSO key:

- shader program,
- root signature,
- render target formats,
- depth format,
- blend state,
- raster state,
- depth/stencil state,
- primitive topology,
- sample count,
- vertex layout,
- permutation flags.

The PSO cache should:

- create lazily in development,
- support preload in validation,
- name PSOs for PIX,
- report cache misses,
- optionally serialize pipeline libraries later.

No pass should hand-build a PSO ad hoc. Passes request a known PSO through a descriptor.

### 8. Render Graph

The render graph is the heart of the DX12-only architecture.

Pass declaration should include:

- pass name,
- queue type,
- inputs,
- outputs,
- read/write access type,
- clear/load/store behavior,
- viewport/scissor,
- whether it writes the backbuffer,
- whether it allows async compute later,
- callback to record commands.

Example:

```cpp
graph.AddPass("VolumetricLight")
    .Read(sceneColor, ResourceAccess::PixelShaderResource)
    .Read(sceneDepth, ResourceAccess::PixelShaderResource)
    .Write(volumetricLight, ResourceAccess::RenderTarget)
    .Record([](RenderGraphContext& ctx)
    {
        // Bind PSO, descriptors, constants, fullscreen triangle.
    });
```

The graph compiler should:

- topologically validate pass order,
- create transient resources,
- insert resource barriers,
- track last writers/readers,
- transition the backbuffer from present to render target and back,
- emit debug labels,
- optionally dump a graph text report.

Start simple:

- one graphics queue,
- one command list,
- no transient aliasing,
- explicit graph resources.

Then add:

- parallel command recording,
- transient resource aliasing,
- copy queue uploads,
- async compute for post/light culling only after profiling proves value.

### 9. Render Passes

Passes should be small objects or plain modules. Avoid deep inheritance until repeated behavior proves it is needed.

Common pass shape:

```cpp
struct RenderPassContext
{
    Dx12CommandList& cmd;
    RenderGraphResources& resources;
    ShaderSystem& shaders;
    MaterialSystem& materials;
    const FrameContext& frame;
    const RenderWorldSnapshot& world;
};
```

Core passes:

| Pass | Role |
|------|------|
| `ShadowMapPass` | Render directional shadow depth and shadow receiver data. |
| `DepthPrepass` | Optional; generates stable depth for HZB, water/post, or GPU culling. |
| `ReflectionPass` | Mirror-camera raster reflection or DXR reflection target. |
| `SkyPass` | Procedural or cube-map sky into scene color. |
| `OpaqueObjectPass` | Instanced objects using material table. |
| `TerrainPass` | Terrain material/style rendering. |
| `GroundContactShadowPass` | Contact cue layer if still needed. |
| `WaterPass` | Transparent water using reflection/depth inputs. |
| `DebugOverlayPass` | Physics, broadphase, grid, and diagnostic geometry. |
| `VolumetricLightPass` | Half/quarter-res light shafts and atmospheric effect. |
| `BloomPass` | Optional real bloom chain. |
| `ToneMapPass` | Scene color/depth/post resolve to backbuffer. |
| `UIPass` | UI quads, text, and overlays after scene resolve. |

Each pass owns:

- pass-specific shaders/PSO descriptors,
- pass-local constants layout,
- pass resource declarations,
- debug markers,
- pass capture hooks.

Each pass should not own:

- global descriptor heaps,
- global material buffer,
- swap chain lifetime,
- frame allocator lifetime,
- unrelated state restore work that belongs to graph/device.

### 10. Render World And Data Extraction

Keep game/physics objects separate from renderer submission data.

Recommended extraction types:

```cpp
struct RenderWorldSnapshot
{
    Span<RenderInstance> instances;
    Span<RenderLight> lights;
    TerrainRenderData terrain;
    WaterRenderData water;
    SkyRenderData sky;
    DebugRenderData debug;
};

struct RenderInstance
{
    Matrix4 model;
    MeshHandle mesh;
    MaterialHandle material;
    uint32_t objectId;
    uint32_t flags;
    BoundingSphere bounds;
};
```

Benefits:

- physics `materialId` does not get confused with render material,
- render thread does not walk mutable game state while command lists are recording,
- GPU culling and sorting have one stable input,
- render tests can inspect extracted data before drawing.

Extraction should be deterministic. If render lists are sorted, use stable keys.

### 11. Mesh And Geometry System

DX12-only should promote mesh ownership out of helper functions.

Mesh asset data:

- vertex buffer,
- index buffer,
- vertex layout,
- bounds,
- BLAS handle if raytracing is enabled,
- optional meshlet data later.

Runtime geometry categories:

- static meshes,
- dynamic meshes,
- instanced meshes,
- debug lines,
- fullscreen triangle/quad,
- UI quads/text.

Object rendering should move toward:

- one mesh handle per sphere/box/pine/terrain patch,
- instance buffer stores transform/material/object ID,
- draw calls grouped by PSO + mesh + material class,
- later: indirect draw buffers.

### 12. Material System

DX12-first can use a real GPU material table earlier than the old tri-renderer plan, but the CPU material model should remain backend-neutral.

CPU material:

```cpp
struct RenderMaterial
{
    uint32_t nameHash;
    RenderMaterialKind kind;
    float baseColor[4];
    float emissiveColor[3];
    float emissiveStrength;
    float roughness;
    float metallic;
    float specular;
    float transmission;
    float stylization;
    uint32_t baseColorTexture;
    uint32_t normalTexture;
    uint32_t roughnessMetallicTexture;
    uint32_t emissiveTexture;
    uint32_t flags;
};
```

GPU material:

```hlsl
struct GpuMaterial
{
    float4 baseColor;
    float4 emissiveAndStrength;
    float4 params0; // roughness, metallic, specular, transmission
    float4 params1; // stylization, kind, flags, reserved
    uint baseColorTexture;
    uint normalTexture;
    uint roughnessMetallicTexture;
    uint emissiveTexture;
};
```

Instance data:

```hlsl
struct GpuInstance
{
    float4x4 model;
    uint meshIndex;
    uint materialIndex;
    uint objectId;
    uint flags;
};
```

Material upload policy:

- CPU registry owns stable material handles.
- Dirty materials upload into a structured buffer.
- Texture handles are descriptor indices.
- Material index is the normal path for object rendering.
- Special per-instance overrides should be rare and explicit.

### 13. Texture And Asset System

DX12-only texture system should support:

- immutable loaded textures,
- render graph transient textures,
- dynamic/update textures,
- generated mips,
- texture arrays where useful,
- debug names for every resource and descriptor.

Texture metadata:

- format,
- dimensions,
- mip count,
- usage flags,
- SRV/UAV/RTV/DSV descriptors,
- current resident state,
- source path/hash for reload.

Texture upload flow:

1. Decode/load image on CPU.
2. Allocate default-heap texture.
3. Stage upload into copy queue upload buffer.
4. Record copy and mip generation.
5. Transition to shader resource.
6. Publish descriptor index.

Hot reload should create a new resource and swap handles after GPU safety, rather than mutating a resource the GPU may still use.

### 14. Upload And Readback Systems

Upload system:

- per-frame upload ring,
- large page allocator,
- 256-byte CBV alignment helper,
- vertex/index/structured-buffer upload helpers,
- texture subresource upload helper,
- peak usage counters,
- overflow diagnostics.

Readback system:

- screenshot readback path,
- GPU timer readback,
- debug buffer readback,
- staged readback requests with fence completion,
- no blocking readback in normal frame path unless validation mode asks for it.

Screenshots should remain deterministic and validation-friendly.

### 15. Command Recording And Threading

Start with one graphics command list per frame. Make it boring and correct first.

Phase 1 command model:

- main render thread records all graph passes into one command list,
- copy uploads may use copy queue but can also begin on graphics queue initially,
- one submit to graphics queue,
- present.

Phase 2:

- command list per pass group,
- parallel CPU recording for independent passes,
- bundle or secondary command-list strategy only if it measures well.

Phase 3:

- async compute for:
  - bloom,
  - volumetric,
  - depth pyramid,
  - GPU culling,
  - light clustering.

Rules:

- Worker threads may build CPU render lists freely.
- Worker threads may record command lists only through owned command allocators.
- Device/resource creation should stay serialized unless the allocator is explicitly thread-safe.
- All command list ownership must be visible in diagnostics.

### 16. Raytracing And DXR

DX12-only makes DXR a normal optional feature rather than an awkward backend hook.

DXR systems:

- BLAS cache for meshes,
- TLAS builder for visible instances,
- raytracing pipeline state cache,
- shader table builder,
- reflection target resources,
- denoise/resolve pass if needed.

Use DXR first for bounded features:

- water reflection,
- optional hard/soft ray shadow experiment,
- debug ray queries.

Do not make the whole renderer depend on DXR. The raster path should remain authoritative until DXR quality and performance are proven.

### 17. Coordinate And Depth Conventions

DX12-only should standardize on:

- left-handed or right-handed math, explicitly documented,
- depth range 0..1,
- reversed-Z only if all projections, depth tests, shadow maps, and post depth reconstruction are moved together,
- HLSL matrix packing policy documented and static-asserted,
- one world/view/projection multiplication convention.

If reversed-Z is adopted, do it as a single intentional architecture slice, not mixed into pass extraction.

### 18. Diagnostics And Tooling

DX12-only loses cross-renderer parity, so diagnostics must become stronger.

Mandatory development diagnostics:

- D3D12 debug layer,
- InfoQueue break-on-error in developer runs,
- GPU-based validation profile for slower validation,
- DRED breadcrumb dump on device removal,
- debug names on every resource, descriptor heap, command list, fence, queue, PSO,
- PIX event markers for every pass and major draw group,
- descriptor heap usage counters,
- upload/readback peak counters,
- render graph dump,
- resource barrier dump in debug mode,
- pass capture option.

Suggested commands:

```bat
tools\validate_dx12_fast.bat
tools\validate_dx12_render.bat
tools\validate_dx12_gbv.bat
tools\validate_dx12_warp.bat
tools\capture_pix_frame.bat
```

These scripts do not exist today. They are part of the DX12-only architecture direction.

### 19. Validation Strategy

After GL/DX11 parity is retired, validation shifts to:

- screenshot baselines,
- deterministic replay scenes,
- D3D12 debug layer,
- GPU-based validation,
- WARP runs for device-independent correctness,
- PIX capture review for hard rendering bugs,
- shader reflection/root signature checks,
- render graph barrier validation,
- descriptor lifetime validation,
- three-run stress for upload/barrier/resource lifetime changes.

Validation tiers:

| Tier | Purpose |
|------|---------|
| Fast | Build, shader contract checks, short DX12 scene smoke. |
| Render | Screenshot suite on hardware DX12. |
| GBV | GPU-based validation on representative scenes. |
| WARP | Slow software adapter correctness sanity check. |
| Perf | Hot path counters, GPU timings, upload/descriptor peaks. |
| Full | All of the above plus replay/screenshot/pass capture checks. |

Core gates:

- zero compile warnings,
- zero D3D12 debug errors,
- no device removal,
- deterministic screenshot outputs within thresholds,
- no descriptor/upload overflows,
- no unexpected render graph hazards,
- GPU timer readback does not stall normal frames.

### 20. Migration Strategy From Current Codebase

If this were pursued, do not rewrite the engine in one jump.

#### Phase 0: Decision And Baseline

Tasks:

1. Record that DX12 is the official production renderer.
2. Freeze current visual behavior with screenshot baselines before removing old comparison paths.
3. Capture current DX12 PIX frames for representative scenes.
4. Record current perf numbers.
5. Create `dx12_only` validation scripts.

Validation:

- current `tools\validate_renderers.bat`,
- current `tools\validate_perf.bat`,
- current DX12 validation log zero.

#### Phase 1: Create Dx12RenderDevice Beside Current Backend

Tasks:

1. Extract DX12 device/swap chain/queues/fences from `SkullbonezRenderBackendDX12`.
2. Keep the old backend working.
3. Add debug naming and DRED setup.
4. Add descriptor/upload counters.
5. Keep output identical.

Validation:

- `tools\validate_renderers.bat`,
- inspect `dx12_validation.txt`.

#### Phase 2: Add Render Graph In Front Of Existing Draws

Tasks:

1. Add graph resource declarations for backbuffer, scene color, scene depth, reflection, shadow, volumetric.
2. Keep current pass drawing callbacks.
3. Let graph own target transitions and viewport setup where possible.
4. Dump graph to text in debug builds.

Validation:

- DX12 render validation,
- pass capture for reflection, scene color, tonemap.

#### Phase 3: Replace Name-Based Shader Setters

Tasks:

1. Introduce typed constant structs.
2. Use shader reflection to validate struct/register compatibility.
3. Move pass binders to typed CBV uploads.
4. Keep compatibility wrappers only for old helper paths.

Validation:

- shader contract checks,
- DX12 render validation,
- GPU-based validation on representative scenes.

#### Phase 4: Material And Instance Buffer Rewrite

Tasks:

1. Add CPU render material registry.
2. Add GPU material structured buffer.
3. Add global texture descriptor table.
4. Change instances to carry material index.
5. Replace tint/mode overloading.

Validation:

- DX12 render validation,
- perf validation,
- descriptor peak checks.

#### Phase 5: Resource Ownership Cleanup

Tasks:

1. Move pass-specific shaders and targets into pass modules.
2. Move helper-owned mesh caches into mesh system.
3. Move water/terrain/post resources into their passes.
4. Remove pass target ownership from `SkullbonezRun`.

Validation:

- full DX12 validation,
- renderer-switch validation no longer applies if DX12-only, but resize/device reset validation must replace it.

#### Phase 6: DX12-Native Features

Tasks:

1. Add depth prepass and HZB if useful.
2. Add GPU culling/indirect draw if object counts justify it.
3. Add real bloom chain.
4. Add async compute only after profiling.
5. Improve DXR reflection or add raytraced shadows only as isolated passes.

Validation:

- DX12 full validation,
- PIX review,
- perf validation.

## What Becomes Simpler

DX12-only removes:

- GL clip-space compatibility work,
- DX11 input-layout differences,
- duplicated GLSL/HLSL shader maintenance,
- cross-renderer texture-slot restrictions,
- GL/DX11/DX12 pixel parity debugging,
- backend hot-switch rebuild complexity.

It allows:

- one shader language,
- one depth convention,
- one descriptor model,
- one material buffer model,
- one PSO model,
- one render graph barrier model,
- stronger PIX and D3D12 validation workflows.

## What Becomes Harder

DX12-only adds or exposes:

- barrier correctness as a first-order architecture problem,
- descriptor lifetime hazards,
- upload allocator fence safety,
- PSO cache complexity,
- root signature design pressure,
- device removal/DRED handling,
- less independent validation from other renderers,
- higher cost of simple rendering experiments.

This is not free. DX12-only is cleaner only if the engine accepts the explicit resource model instead of trying to emulate old fixed-function-style state changes.

## Risks

| Risk | Why It Matters | Mitigation |
|------|----------------|------------|
| Losing parity masks visual bugs | GL/DX11 no longer catch convention drift | Strengthen screenshot baselines, WARP, GBV, pass captures, shader reflection checks. |
| Render graph overbuild | Too much abstraction can slow the work | Start with one queue, no aliasing, one command list, explicit graph resources. |
| Descriptor indexing bugs | Invalid indices can render garbage or fault | Add debug material validation, sentinel textures, descriptor bounds checks where possible. |
| Root signature churn | PSO rebuilds and shader incompatibility | Use stable root signatures and explicit PSO keys. |
| Upload lifetime bugs | CPU can overwrite GPU-read data | Fence every frame allocator and expose peak/overflow counters. |
| Barrier mistakes | GPU corruption, hangs, validation errors | Centralize barriers in the graph and keep manual transitions rare. |
| Device removal | Hard to diagnose without breadcrumbs | Enable DRED, debug names, and PIX markers from phase 1. |
| Async compute too early | Adds sync complexity before value is proven | Defer until graph is stable and profiling shows a win. |

## Success Criteria

A successful DX12-only architecture has:

- `SkullbonezRun` coordinating high-level frame work, not owning pass internals.
- A `Dx12RenderDevice` that owns device, queues, swap chain, descriptors, resources, uploads, readbacks, and fences.
- A render graph that declares pass/resource dependencies and owns transitions.
- A material system where objects carry material indices, not overloaded tint floats.
- HLSL-only shader contracts with reflection validation.
- Stable root signatures and PSO cache keys.
- Descriptor and upload lifetime tracked by frame fences.
- PIX markers and DRED breadcrumbs sufficient to diagnose a GPU failure.
- Screenshot baselines and GPU validation replacing cross-renderer parity.
- No D3D12 debug layer errors.
- No routine GPU stalls for upload, readback, descriptor reset, or timer queries.

## Final Recommendation

With DX12 as the canonical graphics API, take the opportunity to become truly DX12-native while keeping future backend portability at the engine-contract level:

1. Keep the current visible frame composition and deterministic validation mindset.
2. Replace backend abstraction with a `Dx12RenderDevice`.
3. Put a render graph in charge of pass order, resources, and barriers.
4. Move materials to GPU tables and descriptor indices.
5. Use HLSL/DXC/reflection as the production shader contract and preserve enough shader metadata for a future SPIR-V/MSL path.
6. Treat diagnostics, PIX markers, DRED, GBV, and screenshot baselines as core architecture, not optional debugging extras.

Do not start with async compute, bindless everything, or a deferred renderer rewrite. Start with explicit ownership and boring correctness. Once barriers, descriptors, materials, and pass resources are stable, the more ambitious DX12 features will have somewhere sane to land.
