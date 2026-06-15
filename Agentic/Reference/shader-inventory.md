# Shader Inventory

Status: DX12-only after the renderer retirement branch.

## Canonical Source

HLSL is the production shader source for the runtime. The renderer loads raster
programs by base name through `RenderBackendDX12::CreateShader`, which resolves
`SkullbonezData/shaders/<name>.hlsl` and compiles `main_vs` / `main_ps`.

GLSL `.vert` and `.frag` files were removed with the retired OpenGL backend.
DX11-only shader paths were removed with the retired DX11 backend. Future shader
work should update the HLSL contract and `tools/shader_contracts.json`.

## Raster And Post Shaders

| Shader | Role |
|--------|------|
| `collision_visualizer.hlsl` | Per-instance colored collision/sleep-state overlay. |
| `grid_line.hlsl` | Broadphase/grid line overlay. |
| `lit_textured.hlsl` | Non-instanced lit terrain/object rendering. |
| `lit_textured_instanced.hlsl` | Instanced lit dynamic-object rendering. |
| `post_tonemap.hlsl` | HDR scene tonemap and final composite. |
| `post_volumetric_light.hlsl` | Half-resolution depth-aware sun shaft texture. |
| `shadow_depth.hlsl` | Terrain/static depth-only shadow map pass. |
| `shadow_depth_instanced.hlsl` | Instanced dynamic-object depth-only shadow map pass. |
| `sky_atmosphere.hlsl` | Procedural cinematic sky pass. |
| `solid_color.hlsl` | HUD/background solid quads. |
| `solid_color_batch.hlsl` | Batched colored UI/debug quads. |
| `text.hlsl` | Batched font-atlas text rendering. |
| `UIBackdropBlur.hlsl` | UI backdrop blur sampling pass. |
| `unlit_textured.hlsl` | Unlit textured skybox/simple textured pass. |
| `water_calm.hlsl` | Inner flat reflective water zone. |
| `water_ocean.hlsl` | Outer animated reflective ocean zone. |

## DX12 Utility Shaders

| Shader | Role |
|--------|------|
| `generate_mips.hlsl` | DX12 compute mip-generation shader. |
| `reflect.rt.hlsl` | DXR reflection raytracing library source. |
| `reflect.rt.dxil` | Checked-in DXR bytecode loaded by the runtime. Rebuild it from `reflect.rt.hlsl` when the raytracing source changes. |

## Validation

Run `python tools/validate_shaders.py` for shader inventory/contract checks.
Renderer-visible shader behavior is covered by `tools/validate_dx12_renderer.bat`;
use `tools/validate_full.bat` when shader cleanup also changes broad runtime or
project loading behavior.
