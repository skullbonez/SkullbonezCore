# Shader Instructions

Follow the root `../../AGENTS.md` contract first. HLSL is the production shader
source for the runtime.

## Local Rules

- Do not add GLSL or DX11 shader families.
- Ordinary raster shader bindings use the documented DX12 ABI: constants at
  `b0`, fixed SRV slots `t0..t4`, and static samplers `s0`, `s1`, and `s3`.
- Keep shader C++ call sites, HLSL uniforms/resources, and
  `tools/shader_contracts.json` aligned.
- Prefer focused shader changes that preserve existing scene visuals unless the
  task explicitly refreshes baselines.
- PR-bound shader or render-backend changes require
  `tools\validate_dx12_renderer.bat`; shader contract-only checks may also use
  `tools\validate_shaders.bat`.
