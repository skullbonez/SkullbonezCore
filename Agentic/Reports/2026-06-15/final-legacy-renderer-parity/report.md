# Final Legacy Renderer Parity Report

Date: 2026-06-15
Branch: `codex/dx12-only-renderer-retirement`
Commit tested: `d47d840744fe6127a1ed5f396d0c5f2e7555beda`
Validation command: `tools\validate_renderers.bat`
Result: passed

## Purpose

This is the final GL/DX11/DX12 parity evidence captured before retiring the
legacy OpenGL and DX11 renderers. After this point, renderer validation should
move to the DX12-only screenshot regression gate added in Phase 1.

## Artifact Record

| Item | Path |
|------|------|
| Manifest | `TestOutput\validation\renderers\20260615T032107Z\manifest.json` |
| Summary | `TestOutput\validation\renderers\20260615T032107Z\summary.json` |
| DX12 validation log | `TestOutput\validation\renderers\20260615T032107Z\dx12_validation.txt` |

The generated `TestOutput\validation` artifacts are local validation output.
This report captures the durable values needed after those artifacts are
cleaned from a workspace.

## Commands

| Renderer | Command |
|----------|---------|
| GL | `Profile\SKULLBONEZ_CORE.exe --vsync off --suite SkullbonezData/scenes/render_tests.suite` |
| DX11 | `Profile\SKULLBONEZ_CORE.exe --renderer dx11 --vsync off --suite SkullbonezData/scenes/render_tests.suite` |
| DX12 | `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --suite SkullbonezData/scenes/render_tests.suite` |

## Results

Threshold: average pixel diff below `10.0`.
Viewport: `1784x961` for all captured renderer/scene pairs.
DX12 InfoQueue validation errors: `0`.

| Scene | Comparison | Average Diff | Max Diff | Pixels Over 10 | Status |
|-------|------------|--------------|----------|----------------|--------|
| `water_ball_test` | GL vs DX11 | `2.8648` | `82` | `450385` | Pass |
| `water_ball_test` | GL vs DX12 | `2.9023` | `82` | `450111` | Pass |
| `solver_smoke` | GL vs DX11 | `1.2574` | `175` | `144605` | Pass |
| `solver_smoke` | GL vs DX12 | `1.2128` | `170` | `143975` | Pass |

## Notes

- These differences are the known acceptable endpoint for the legacy parity
  stack. Average differences are below threshold in both scenes.
- DX12 reported zero validation-layer errors during the parity run.
- GL and DX11 should not be kept merely to regenerate this evidence. Future
  renderer work should use `tools\validate_dx12_renderer.bat` unless a broader
  runtime gate is required.
