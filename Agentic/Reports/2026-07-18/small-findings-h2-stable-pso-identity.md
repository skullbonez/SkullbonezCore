# Small Findings H2 — Stable PSO Cache Identity

Date: 2026-07-18  
Branch: `nightrunner-17th-july`  
Task elapsed: approximately 8 minutes

## Result

- `PSOKey12` no longer contains a raw root-signature pointer. Its recipe uses
  the pipeline owner's nonzero `uint64_t` root-signature identity.
- `Dx12PipelineOwner` issues the identity only after native root-signature
  creation succeeds. The issuance sequence is monotonic across
  shutdown/reinitialization and treats exhaustion as a fatal invariant.
- Shutdown serializes the persistent cache and releases every dependent
  in-memory PSO before releasing the root signature and clearing its active
  identity. The next identity is deliberately not rewound.
- The PSO hash and cache-miss diagnostic both consume the stable identity;
  neither retains or reports a COM address as recipe identity.

No baseline, golden, screenshot, authored-data, seven retained render consumer
interface, or `FRAME_COUNT = 2` change occurred.

## Validation

- Focused `Profile|x64` solution build: passed in 10.010s.
- `tools\validate_dx12_renderer.bat`: passed in 55.677s. Profile and Debug
  builds completed with zero warnings/errors; DX12 InfoQueue reported zero
  validation errors; `water_ball_test`, `solver_smoke`, and `space_three_body`
  matched their committed baselines.
- `tools\run_graphics_stress.bat 1`: passed in 61.661s. PID 36784 ran for the
  bounded minute, reached frame 13,478 across 370 scene loads, accepted the
  PID-scoped close, wrote its shutdown memory artifact, and left stderr empty.

Renderer comparison artifacts:
`TestOutput/validation/dx12_renderer/20260718T073028Z/manifest.json` and
`TestOutput/validation/dx12_renderer/20260718T073028Z/summary.json`.

