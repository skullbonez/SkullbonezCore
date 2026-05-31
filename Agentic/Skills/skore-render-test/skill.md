---
name: skore-render-test
description: Run tri-renderer visual validation and manage renderer screenshot baselines.
---

# skore-render-test

Use for shader, renderer, texture, screenshot, or visual-baseline work.

## Validate

```bat
tools\validate_renderers.bat
```

This builds Profile, runs `SkullbonezData/scenes/render_tests.suite` with GL, DX11, and DX12, checks stdout/stderr, verifies DX12 InfoQueue output, and checks cross-renderer pixel parity.

Expected captures are written to `Profile\`:
- `gl_screenshot.bmp`, `gl_legacy_smoke.bmp`
- `dx11_screenshot.bmp`, `dx11_legacy_smoke.bmp`
- `dx12_screenshot.bmp`, `dx12_legacy_smoke.bmp`

Baselines live in `TestOutput\baselines\baseline_<renderer>_<scene>.png`.

## Update Baselines

Only update baselines when the visual change is intentional:

```bat
tools\update_baselines.bat --visuals --require
```

## Archive Captures

```bat
tools\archive_validation_artifacts.bat --visuals --require
```

## Debugging Failures

- If a renderer crashes or hangs, use `skore-cdb-debug`.
- If local pixel comparison fails, convert the relevant BMPs to PNG before visual inspection.
- Do not send BMPs to image viewers; use PNGs.

The suite is render-only. Performance validation is separate:

```bat
tools\validate_perf.bat
```
