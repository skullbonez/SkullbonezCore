# Terrain Tessellation Plan

Status: proposed
Created: 2026-06-17
Branch: `codex/non-cinematic-photoreal-lighting`
Scope: terrain render mesh density, DX12 terrain shaders, shadows, DXR reflection terrain BLAS, optional physics terrain resolution

## Purpose

Reduce the visible low-poly jaggies on terrain without destabilizing the
deterministic physics surface.

The current height-map terrain is built from `terrain.raw` with:

- `m_mapSize = 256`
- `m_stepSize = 8`
- `terrain_scale = 5.0`
- roughly 32 x 32 terrain posts
- roughly 31 x 31 quads
- 5,766 non-indexed terrain vertices
- 40 world units between rendered posts

That coarse grid is the main reason slopes and silhouettes read as jagged. The
first fix should increase terrain render density while keeping collision
queries and physics baselines unchanged. Hardware tessellation can come later,
but it is not the cheapest safe first slice because the current DX12 shader and
PSO path only compiles VS/PS shaders and always draws triangle lists.

## Recommendation

Do CPU-built render tessellation first, not hardware tessellation first.

Reasons:

- It improves the visible mesh, shadow caster, and DXR terrain BLAS together.
- It does not require hull/domain shader plumbing through `ShaderDX12`,
  `RenderBackendDX12::CreatePSO()`, PSO cache keys, primitive topology, and
  shader contracts.
- It lets the implementation stay terrain-local for the first useful result.
- DXR reflection cannot see hardware-generated tessellated triangles anyway, so
  a denser source mesh is still needed if reflected terrain should match the
  visible terrain.

Hardware tessellation should be an optional later optimization for adaptive LOD,
not the first correctness path.

## Validation Rule

This plan is documentation only, so no validation is required for drafting it.

Implementation slices that touch terrain render code, shaders, shadow depth, or
DXR terrain geometry must run:

```bat
tools\validate_dx12_renderer.bat
```

If the chosen slice changes per-frame terrain cost, terrain BLAS size, PSO
creation, or LOD behavior, also run:

```bat
tools\validate_perf.bat
```

Only run physics validation if the implementation intentionally changes the
physics-visible terrain surface, collision queries, or physics baselines:

```bat
tools\validate_physics.bat
```

## Current Source Anchors

| Area | Source |
|------|--------|
| Terrain mesh, height queries, render calls | `SkullbonezSource/SkullbonezTerrain.h/.cpp` |
| Current mesh build | `Terrain::BuildMesh()` |
| Flat slope mesh | `Terrain::BuildFlatSlopeMesh()` |
| Visible terrain draw | `Terrain::Render()` |
| Shadow terrain caster | `Terrain::RenderShadowDepth()` |
| Terrain pass order | `SkullbonezSource/SkullbonezRunPasses.cpp`, `TerrainPass::Render()` |
| Frame pass order | `SkullbonezSource/SkullbonezRunRender.cpp` |
| Mesh abstraction | `SkullbonezSource/SkullbonezIMesh.h`, `SkullbonezMeshDX12.*` |
| DX12 PSO/topology path | `SkullbonezSource/SkullbonezRenderBackendDX12.Pipeline.cpp` |
| Shader compile path | `SkullbonezSource/SkullbonezShaderDX12.cpp` |
| Terrain shader | `SkullbonezData/shaders/lit_textured.hlsl` |
| Shadow depth shader | `SkullbonezData/shaders/shadow_depth.hlsl` |
| DXR terrain BLAS init | `SkullbonezSource/SkullbonezRunScene.cpp`, scene-load DXR block |

## Non-Goals For The First Slice

- Do not change physics terrain behavior.
- Do not refresh physics baselines.
- Do not add OpenGL or DX11 paths.
- Do not rewrite the render graph.
- Do not add hull/domain shader support until a denser CPU-built mesh has been
  measured and reviewed.

## Phase 0: Visual And Cost Baseline

Goal: prove the jaggies, record the current cost, and pick a target density.

Tasks:

1. Choose one or two representative scenes where terrain jaggies are obvious.
2. Capture current screenshots and note camera position, scene, and config.
3. Record current terrain vertex count and world spacing in a lightweight log.
4. Use profiler/draw-call data to record terrain render and shadow cost.
5. Decide the first target render step:
   - `4` raw pixels: about 63 x 63 quads, 23,814 vertices, 20 world-unit spacing.
   - `2` raw pixels: about 127 x 127 quads, 96,774 vertices, 10 world-unit spacing.
   - `1` raw pixel: about 255 x 255 quads, 390,150 vertices, 5 world-unit spacing.

Recommended first target: render step `2`. It is an 16.8x vertex increase from
today, but still modest for one static terrain mesh on DX12, and it should make
the visual difference obvious.

## Phase 1: Split Render Height Data From Physics Posts

Goal: let rendering sample a denser height field while physics keeps using the
existing coarse deterministic collision posts.

Tasks:

1. Add terrain render-density config, for example:
   - `terrain_render_step_size = 2`
   - clamp to `[1, m_stepSize]`
   - require it to divide `m_mapSize` cleanly or round to the nearest safe
     divisor with a log message.
2. Preserve enough height data for render rebuilds after device reset.
   - Current constructor clears `m_terrainData` after `BuildMesh()`.
   - A denser rebuild needs either retained raw bytes or a compact
     `m_renderHeightSamples` float grid.
3. Keep `m_postData`, `BuildCollisionCache()`, `GetTerrainHeightAt()`,
   `GetTerrainNormalAt()`, and `GetTerrainHeightAndPlaneAt()` as the
   physics-authoritative surface.
4. Add helper functions for render-only sampling:
   - `SampleRenderHeightRaw(float rawX, float rawZ)`
   - `SampleRenderHeightWorld(float worldX, float worldZ)`
   - `SampleRenderNormalWorld(float worldX, float worldZ)`
5. Keep exact agreement at original physics posts where possible, so balls and
   boxes remain visually close to the rendered terrain at rest.

Acceptance:

- Old physics queries produce byte-identical results.
- `ResetRenderResources()` can rebuild the terrain render mesh without needing
  to reload the raw file.
- The default config can disable the new path by setting render step to `8`.

## Phase 2: Build A Denser Terrain Render Mesh

Goal: replace the coarse render triangle list with a denser static mesh built
from render height samples.

Tasks:

1. Replace or wrap `Terrain::BuildMesh()` with `BuildRenderMesh()`.
2. Generate vertices over the render grid using `terrain_render_step_size`.
3. Build normals from central differences on the render surface, not from the
   old 32 x 32 physics post normals.
4. Preserve the same vertex format: `P3_N3_UV2`.
5. Keep texture coordinates visually equivalent to the current wrapping.
6. Ensure `Terrain::Render()` and `Terrain::RenderShadowDepth()` automatically
   draw the same denser mesh.
7. Log render-grid dimensions and vertex count once at terrain build time.

Acceptance:

- Terrain jaggies are visibly reduced in the representative scenes.
- Shadow silhouettes follow the improved terrain mesh.
- No terrain cracks at map edges or quad seams.
- DX12 validation log remains clean.

## Phase 3: DXR Reflection And Resource Rebuild Hygiene

Goal: keep reflected terrain and backend reset behavior consistent with the new
render mesh.

Tasks:

1. Confirm `Terrain::GetMesh()` returns the denser mesh used by visible terrain.
2. Confirm scene-load DXR initialization builds the terrain BLAS from that mesh.
3. If changing terrain render density at runtime is allowed, add a safe path to
   shut down and rebuild DXR terrain acceleration structures.
4. If render resources are reset after device loss or resize, confirm the DXR
   terrain BLAS is rebuilt from the new vertex buffer before raytraced water
   reflection is used again.

Acceptance:

- Planar reflection path and DXR reflection path do not show obvious terrain
  geometry disagreement.
- No stale GPU virtual address is used after terrain mesh rebuild.
- DX12 validation remains clean with raytraced water reflection enabled.

## Phase 4: Visual Polish After Geometry Density

Goal: improve terrain smoothness after the mesh has enough geometry to carry
the shape.

Possible tasks:

1. Add an optional render-only smoothing mode for normals, not heights, if the
   terrain still reads faceted.
2. Keep the existing cinematic terrain relief path aligned between visible and
   shadow shaders.
3. If height smoothing is added, bound it with a small max displacement and
   expose it as a render-only setting so physics determinism is not silently
   changed.
4. Review terrain mode 7, which deliberately uses facet styling, so it does not
   fight the goal when the user wants smoother terrain.

Acceptance:

- The normal/default terrain mode reads smoother.
- Low-poly/faceted cinematic modes still work when intentionally selected.
- Contact mismatch remains visually tolerable.

## Phase 5: Optional Hardware Tessellation Spike

Goal: decide whether adaptive hardware tessellation is worth the renderer
complexity after the CPU-built mesh path is working.

Required changes if pursued:

1. Extend `ShaderDX12` to optionally compile `main_hs` and `main_ds`.
2. Reflect constant buffers across VS/HS/DS/PS.
3. Add HS/DS bytecode to shader accessors.
4. Extend `RenderBackendDX12::CreatePSO()` to fill `HS` and `DS` bytecode.
5. Add PSO cache-key fields for hull/domain shader bytecode and primitive
   topology.
6. Add a mesh or draw path that can bind control-point patches instead of
   always using `D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST`.
7. Add terrain-specific shader assets:
   - visible terrain tessellation shader,
   - shadow depth tessellation shader.
8. Keep a dense CPU mesh or a separately generated BLAS mesh for DXR, because
   hardware tessellation output is not available to acceleration-structure
   builds.

Acceptance:

- Hardware tessellation gives better quality or lower cost than the dense
  static mesh for representative scenes.
- DX12 renderer validation is clean.
- Perf validation shows no unacceptable PSO churn or GPU cost.

Decision gate:

- If CPU render step `2` is visually good and perf is fine, skip hardware
  tessellation.
- If CPU render step `1` is needed but costs too much in shadows or DXR BLAS
  builds, prototype hardware tessellation plus a lower-density DXR fallback.

## Phase 6: Optional Physics Terrain Resolution

Goal: only if visual/physics mismatch becomes obvious, deliberately decide
whether the collision surface should also become denser.

Tasks:

1. Compare ball and box rest contacts against the denser rendered surface.
2. Use SkullScope if contact classification looks suspicious.
3. If physics must change, make it a separate physics plan:
   - deterministic terrain sampling,
   - collision cache rebuild,
   - physics baseline refresh from final Debug artifacts,
   - final `tools\validate_physics.bat`.

Acceptance:

- No silent physics behavior change lands inside a visual terrain PR.
- Any future physics terrain resolution change has byte-exact baseline proof.

## Proposed Work Queue

| ID | Scope | Dependencies | Validation |
|----|-------|--------------|------------|
| terrain-baseline-capture | Capture current jaggies and vertex counts | none | none unless user asks for captures |
| terrain-render-data-split | Keep render height samples separate from physics posts | baseline | targeted build, then DX12 gate before PR |
| terrain-dense-render-mesh | Build configurable denser terrain mesh | data split | `validate_dx12_renderer`, `validate_perf` if default density changes |
| terrain-dxr-rebuild-hygiene | Rebuild/verify DXR terrain BLAS uses dense mesh safely | dense mesh | `validate_dx12_renderer` |
| terrain-visual-polish | Normals/smoothing and cinematic mode review | dense mesh | `validate_dx12_renderer` |
| terrain-hardware-tessellation-spike | Optional HS/DS adaptive tessellation | dense mesh perf result | `validate_dx12_renderer`, `validate_perf` |
| terrain-physics-resolution-decision | Optional physics terrain change | mismatch evidence | `validate_physics` |

## Risks

| Risk | Mitigation |
|------|------------|
| Render terrain diverges from collision terrain | Keep physics posts authoritative in first slice; review rest-contact scenes visually; move physics to a separate plan only if needed. |
| Vertex count increases shadow or DXR BLAS cost | Start with render step `2`; profile terrain, shadow, and reflection paths; keep config fallback to old density. |
| Device reset rebuilds terrain mesh without raw height data | Store compact render height samples or keep raw height bytes in `Terrain`. |
| Hardware tessellation adds renderer-wide churn | Treat HS/DS support as an optional spike after static mesh results are known. |
| Cinematic relief shadows drift | Continue applying the same relief uniforms in visible and shadow terrain shaders. |
| Terrain mode 7 still looks intentionally faceted | Keep faceted style as an art mode, but make normal terrain modes smoother by default. |

## Done Criteria

- Default terrain no longer reads as obviously low-poly in representative
  ground-level and wide shots.
- Terrain shadow caster uses the same improved geometry as the visible terrain.
- Raytraced reflection either uses the improved terrain BLAS or has a documented
  fallback with no obvious mismatch in validation scenes.
- Physics baselines are unchanged unless a later, explicit physics terrain slice
  is approved.
- Required validation for the final implementation scope is run and quoted in
  the PR or handoff.
