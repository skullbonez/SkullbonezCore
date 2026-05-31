# DXR Water Reflections — Implementation Plan

## Goal
Replace the current mirror-camera FBO reflection pass with DXR (DirectX Raytracing)
ray-traced reflections on the DX12 backend only. GL and DX11 keep the existing FBO
path unchanged. The water shader continues to sample a 2D reflection texture — the
only change is how that texture is produced.

## Current system (what we're replacing)

`SkullbonezRun.cpp` lines 674–711:
- Mirrors eye/lookat about water Y plane
- Binds `m_cReflectionFBO`, renders skybox + balls from mirrored camera with clip plane
- Unbinds FBO; water shader samples `uReflectionTex` at `t1`
- FBO is created at 2× viewport resolution

Problems with this approach:
- Clip plane is a no-op on DX12 (`RenderBackendDX12.cpp:860`) so balls below water
  bleed into the reflection
- Mirror camera only reflects what's directly above water — no glancing-angle reflections
- Costs a full second render pass (skybox + all balls) every frame

## What DXR replaces it with

A `raygen` shader fires one reflection ray per water-surface pixel. The ray travels in
the mirror direction of the camera ray reflected about the water normal. It hits terrain
or a sphere → runs a `closest_hit` shader to shade that point → writes colour into a
UAV texture. That UAV is bound as `uReflectionTex` in the water shader. No second
render pass, no clip plane, correct glancing-angle reflections.

---

## Phase 1 — Foundation (no visible change yet)

### Task 1.1 — Expose mesh GPU addresses for BLAS
DXR BLAS needs `D3D12_GPU_VIRTUAL_ADDRESS` of the vertex buffer (and optionally index
buffer). `MeshDX12` already has a VB on the DEFAULT heap.

**Key insight:** DXR `D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC` supports vertex-only
geometry (`IndexBuffer = 0`, `IndexCount = 0`, `IndexFormat = DXGI_FORMAT_UNKNOWN`).
Index buffers are NOT required. Since all meshes in this engine are triangle lists
with contiguous vertices, we can pass them directly to BLAS without fabricating indices.

- Add `D3D12_GPU_VIRTUAL_ADDRESS GetVertexBufferGPUVA() const` to `MeshDX12`
- Add `int GetStride() const` to `MeshDX12`
- Add these to `IMesh` interface (return 0 on GL/DX11 — never called there)
- Do NOT add index buffers unless a future need arises

Files: `SkullbonezMeshDX12.h/.cpp`, `SkullbonezIMesh.h`

### Task 1.2 — DXR capability check + Device5/CmdList4 QI
The current device is created at `D3D_FEATURE_LEVEL_11_0`. DXR is not tied to feature
level — it's a separate capability query.

- In `RenderBackendDX12::Init()`, after device creation:
  1. `CheckFeatureSupport(D3D12_FEATURE_DATA_D3D12_OPTIONS5)` — check
     `RaytracingTier >= D3D12_RAYTRACING_TIER_1_0`
  2. If supported, `QueryInterface` the device for `ID3D12Device5*` (DXR interface)
  3. `QueryInterface` the command list for `ID3D12GraphicsCommandList4*` (needed for
     `BuildRaytracingAccelerationStructure` and `DispatchRays`)
  4. If either QI fails → DXR not available on this hardware
- Store `m_device5`, `m_cmdList4`, `m_dxrSupported`
- If false: log a warning; the engine falls back to FBO path (Task 3.3 handles this)

**Note:** Do NOT change the base feature level. DXR works on FL 11_0 devices that
expose the capability (most RTX/RDNA2+ GPUs).

Files: `SkullbonezRenderBackendDX12.h/.cpp`

### Task 1.3 — BLAS builder utility
Add `SkullbonezBLAS.h/.cpp` in `SkullbonezSource/`:

```cpp
class BLAS {
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;
public:
    void Build( ID3D12Device5* device,
                ID3D12GraphicsCommandList4* cmdList,
                D3D12_GPU_VIRTUAL_ADDRESS vbVA, int vertexCount, int vertexStride,
                DXGI_FORMAT vertexPosFormat );  // DXGI_FORMAT_R32G32B32_FLOAT
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void ReleaseAfterBuild();  // free scratch, keep result
    void Reset();
};
```

- No index buffer parameters — vertex-only BLAS (see Task 1.1)
- `D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC.IndexBuffer = 0`
- `vertexStride` lets the runtime skip normals/UVs and read only XYZ positions
- Build flags: `D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE` for both terrain and spheres
  (all geometry is opaque — no any-hit shader needed, saves perf)
- Use `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE` for
  terrain (built once, traced many times per frame)

Files: `SkullbonezBLAS.h`, `SkullbonezBLAS.cpp`

### Task 1.4 — TLAS builder utility
Add `SkullbonezTLAS.h/.cpp`:

```cpp
class TLAS {
    ID3D12Resource* m_scratch;
    ID3D12Resource* m_result;
    ID3D12Resource* m_instanceDescs;  // upload heap, rewritten each frame
public:
    void Init( ID3D12Device5* device, int maxInstances );
    void Build( ID3D12GraphicsCommandList4* cmdList,
                const std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instances );
    D3D12_GPU_VIRTUAL_ADDRESS GetResultVA() const;
    void Reset();
};
```

TLAS is rebuilt every frame (balls move). Scratch and instance desc buffers are
persistent (sized for `maxInstances = 301`: 300 balls + 1 terrain).

Files: `SkullbonezTLAS.h`, `SkullbonezTLAS.cpp`

---

## Phase 2 — RT Pipeline and Shaders

### Task 2.1 — DXR root signature
Add a new root signature for the RT pipeline (separate from the raster root sig):

```
Slot 0: SRV — TLAS                      (t0, space1)
Slot 1: UAV — output reflection texture (u0)
Slot 2: CBV — RT constants              (b1: camera pos, water Y, time)
Slot 3: SRV table — terrain VB/IB       (t1, space1)
Slot 4: SRV table — sphere VB/IB        (t2, space1)
Slot 5: SRV — terrain texture           (t3, space1)
Slot 6: SRV — sphere texture            (t4, space1)
```

Files: `SkullbonezRenderBackendDX12.cpp`

### Task 2.2 — Write HLSL RT shaders
New file: `SkullbonezData/shaders/reflect.rt.hlsl`

Three shader entry points in one file (compiled via DXC with `-T lib_6_3`):

**raygen** `RayGen`:
- Dispatched over the FULL reflection texture (same size as FBO: 2× viewport).
- For each pixel: reconstruct a world-space ray from the camera through that pixel
  (using inverse VP matrix), compute intersection with the water Y plane analytically,
  then reflect the ray direction about `(0,1,0)` at that intersection point.
- Fire `TraceRay` into TLAS with the reflected ray. Write result colour to UAV.
- **Key point:** This is NOT a "per water pixel" dispatch — it's a full-screen dispatch
  that computes which pixels correspond to water surface reflections via analytic
  ray-plane intersection. The water shader's `reflectClipPos` projection then picks
  the correct texels, exactly as it does with the FBO path today.

**closest_hit** `ClosestHit`:
- Read barycentrics + `PrimitiveIndex()` to reconstruct triangle vertices from the VB
- Vertex positions are in LOCAL SPACE — use `ObjectToWorld3x4()` intrinsic to
  transform hit normal to world space for lighting
- Simple diffuse + texture sample (match the raster shading so the seam is invisible)
- For spheres: compute UV from world-space normal (spherical mapping)

**miss** `Miss`:
- Sample the skybox colour for the ray direction (simple gradient or env colour constant)
- Write to payload

Shader constants: inverse VP matrix, camera world pos, water Y, `uTime`, light pos.

**Compilation strategy:** Link `dxcompiler.dll` at runtime for iteration speed.
Load the `.hlsl` file and call `IDxcCompiler3::Compile` with `-T lib_6_3` at engine
init time. Ship pre-compiled `.dxil` blob for release builds later.

Files: `SkullbonezData/shaders/reflect.rt.hlsl`

### Task 2.3 — RT pipeline state object (RTPSO)
In `RenderBackendDX12`, add:

```cpp
ID3D12StateObject*            m_rtPSO;
ID3D12StateObjectProperties*  m_rtPSOProps;
```

Create the RTPSO with `D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE`:
- `D3D12_DXIL_LIBRARY_SUBOBJECT` — compiled `reflect.rt.hlsl`
- `D3D12_HIT_GROUP_SUBOBJECT` — terrain hit group, sphere hit group
- `D3D12_RAYTRACING_SHADER_CONFIG` — payload size: 16 bytes (float4 colour)
- `D3D12_RAYTRACING_PIPELINE_CONFIG` — max recursion depth: 1
- Associate root signature

Files: `SkullbonezRenderBackendDX12.h/.cpp`

### Task 2.4 — Shader Binding Table (SBT)
Add `SkullbonezSBT.h/.cpp`:

```cpp
class SBT {
    ID3D12Resource* m_buffer;  // upload heap
    UINT m_rayGenOffset;
    UINT m_missOffset;
    UINT m_hitGroupOffset;
    UINT m_hitGroupStride;  // must be aligned to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32)
public:
    void Build( ID3D12StateObjectProperties* props,
                D3D12_GPU_VIRTUAL_ADDRESS terrainVBVA, int terrainVertCount, int terrainStride,
                D3D12_GPU_VIRTUAL_ADDRESS sphereVBVA, int sphereVertCount, int sphereStride );
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissRange() const;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupRange() const;
};
```

**SBT layout and alignment:**
- Each record = shader identifier (32 bytes) + local root arguments
- Records must be aligned to `D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT` (32 bytes)
- Table start must be aligned to `D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT` (64 bytes)
- Layout: `[raygen | pad][miss | pad][terrain hit + local args | sphere hit + local args]`

**InstanceContributionToHitGroupIndex mapping:**
- Terrain TLAS instance → `InstanceContributionToHitGroupIndex = 0` → hits SBT entry 0 (terrain)
- Sphere TLAS instances → `InstanceContributionToHitGroupIndex = 1` → hits SBT entry 1 (sphere)
- Each hit group record's local root arguments: VB GPU VA, vertex count, stride
  (so the closest_hit shader can read back vertex data for UV/normal reconstruction)

Files: `SkullbonezSBT.h`, `SkullbonezSBT.cpp`

### Task 2.5 — UAV reflection texture
In `RenderBackendDX12`:
- Allocate `m_reflectionUAV` — `ID3D12Resource*`, `DXGI_FORMAT_R8G8B8A8_UNORM`,
  same dimensions as `m_cReflectionFBO` (2× viewport)
- Allocate UAV descriptor in the CBV/SRV/UAV heap
- Allocate SRV descriptor so the water shader can sample it at `t1` as normal
- On `Resize()`: recreate at new dimensions

Files: `SkullbonezRenderBackendDX12.h/.cpp`

---

## Phase 3 — Integration

### Task 3.1 — Build BLAS at scene load
In `SkullbonezRun::Initialise()` (after terrain mesh is created, after game models
are set up):

```cpp
if ( Gfx().IsDXRSupported() )
{
    m_terrainBLAS.Build( device, cmdList, terrain VB/IB... );
    m_sphereBLAS.Build( device, cmdList, sphere VB/IB... );
    m_terrainBLAS.ReleaseAfterBuild();
    m_sphereBLAS.ReleaseAfterBuild();
}
```

Add `IsDXRSupported()` to `IRenderBackend` (returns false on GL/DX11).

Files: `SkullbonezRun.cpp`, `SkullbonezIRenderBackend.h`, backends

### Task 3.2 — Rebuild TLAS each frame, dispatch RT
In `SkullbonezRun::Run()`, in the render section, **replace** the FBO reflection
pre-pass block (lines 674–711) with:

```cpp
if ( Gfx().IsDXRSupported() )
{
    // Build instance list: terrain (identity matrix) + one entry per live ball
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    instances.push_back( MakeTerrainInstance( m_terrainBLAS.GetResultVA() ) );
    for ( int i = 0; i < m_modelCount; i++ )
        instances.push_back( MakeSphereInstance( m_sphereBLAS.GetResultVA(),
                                                  m_cGameModelCollection.GetWorldMatrix(i) ) );
    m_tlas.Build( cmdList4, instances );

    // Transition UAV to unordered access, dispatch rays
    Gfx().DispatchReflectionRays( m_tlas.GetResultVA(), waterY, reflVP );
    // UAV barrier, then transition to SRV for water shader
}
else
{
    // existing FBO mirror-camera path (unchanged)
    ... lines 674-711 ...
}
```

Add `DispatchReflectionRays(...)` to `IRenderBackend` (no-op on GL/DX11).

Files: `SkullbonezRun.cpp`, `SkullbonezIRenderBackend.h`, `SkullbonezRenderBackendDX12.cpp`

### Task 3.3 — Bind UAV SRV as water reflection texture on DX12
In the water draw call, the shader already binds `uReflectionTex` at `t1`. On DX12
the FBO SRV currently goes there. Change `RenderBackendDX12` to bind
`m_reflectionUAV`'s SRV instead when DXR is active.

No changes to the water HLSL shader needed.

Files: `SkullbonezRenderBackendDX12.cpp`

### Task 3.4 — Fallback path verification
Confirm GL and DX11 are unaffected:
- `IsDXRSupported()` returns false → FBO path runs as before
- Water shader is identical — it only cares about `t1`, not how it was produced
- Run `water_ball_test.scene` on all three renderers, check baselines match

---

## Phase 4 — Polish

### Task 4.1 — Perturbed reflection rays
The current FBO path perturbs reflection UVs in the water PS using a sine wave.
With DXR we can perturb the ray direction itself in the raygen shader instead:

```hlsl
float wave = sin(worldPos.x * 0.04 + uTime * 1.2) * 0.002;
rayDir = normalize(reflected + float3(wave, 0, wave));
```

This is physically more correct and costs nothing extra.

### Task 4.2 — Sphere hit shader texture sampling
The closest-hit shader for spheres needs to sample the sphere texture. Pass the
sphere texture SRV through the SBT hit record local root signature so each hit
group can access it.

### Task 4.3 — TLAS update vs rebuild
For large ball counts, consider `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE`
instead of a full rebuild. Only worthwhile if TLAS build shows up in profiler.

---

## What does NOT change

| Thing | Status |
|-------|--------|
| `water.hlsl` HLSL shader | Unchanged — still samples `uReflectionTex` at `t1` |
| GL backend | Unchanged — FBO mirror path |
| DX11 backend | Unchanged — FBO mirror path |
| Water mesh geometry + animation | Unchanged |
| `m_cReflectionFBO` on GL/DX11 | Unchanged |

---

## New files summary

| File | Purpose |
|------|---------|
| `SkullbonezBLAS.h/.cpp` | BLAS build/hold utility |
| `SkullbonezTLAS.h/.cpp` | TLAS per-frame rebuild utility |
| `SkullbonezSBT.h/.cpp` | Shader binding table builder |
| `SkullbonezData/shaders/reflect.rt.hlsl` | Raygen + ClosestHit + Miss shaders |

## Modified files summary

| File | Change |
|------|--------|
| `SkullbonezMeshDX12.h/.cpp` | Add index buffer |
| `SkullbonezIMesh.h` | Add `GetIndexCount()`, `GetIndexBufferGPUVA()` |
| `SkullbonezIRenderBackend.h` | Add `IsDXRSupported()`, `DispatchReflectionRays()` |
| `SkullbonezRenderBackendDX12.h/.cpp` | DXR check, RTPSO, UAV texture, dispatch |
| `SkullbonezRenderBackendGL.cpp` | Stub `IsDXRSupported()` → false |
| `SkullbonezRenderBackendDX11.cpp` | Stub `IsDXRSupported()` → false |
| `SkullbonezRun.cpp` | Replace FBO pre-pass with DXR dispatch (behind capability check) |

---

## Task order (dependency chain)

```
1.1 Index buffers
  └─ 1.3 BLAS builder
       └─ 1.4 TLAS builder
            └─ 3.1 Build BLAS at scene load
                 └─ 3.2 Rebuild TLAS + dispatch RT per frame
1.2 DXR capability check
2.1 DXR root signature
  └─ 2.3 RTPSO
       └─ 2.4 SBT
            └─ 3.2 Dispatch
2.2 HLSL RT shaders
  └─ 2.3 RTPSO
2.5 UAV reflection texture
  └─ 3.3 Bind UAV as water t1
       └─ 3.4 Fallback verification
```

4.1–4.3 are independent polish tasks, do last.

---

## Known Risks & Gotchas

| # | Severity | Issue | Mitigation |
|---|----------|-------|------------|
| 1 | 🔴 | Raygen dispatch is full-screen, not per-water-pixel | Dispatch over full reflection texture; water shader samples via projected UV as today — no mask needed |
| 2 | 🔴 | Need ID3D12Device5 / ID3D12GraphicsCommandList4 for DXR APIs | Task 1.2 handles QI; if QI fails → fallback to FBO |
| 3 | 🟡 | SBT alignment (32-byte records, 64-byte table start) | Use explicit alignment math in SBT::Build; test with DX12 debug layer |
| 4 | 🟡 | Closest-hit vertex data is local-space | Use `ObjectToWorld3x4()` HLSL intrinsic for normals; UVs don't need transform |
| 5 | 🟡 | Index buffers NOT required for BLAS (vertex-only is valid) | Task 1.1 does NOT add index buffers; passes vertex-only geometry desc |
| 6 | 🟢 | DXC compiler needed for SM 6.3 (current shaders use D3DCompile / SM 5.0) | Link dxcompiler.dll at runtime for dev; pre-compile .dxil for release |
| 7 | 🟢 | Upload buffer (8 MB) is not used for BLAS/TLAS — they use own committed resources | No contention with existing upload ring |
| 8 | 🟢 | TLAS rebuild every frame for 301 instances is cheap (~0.1ms on modern GPU) | Can upgrade to refit/update in Phase 4 if profiling shows need |
