---
name: skore-render-test
description: Run targeted DX12 renderer pre-commit/PR validation and manage renderer screenshot baselines.
---

# skore-render-test

Use for shader, renderer, texture, screenshot, or visual-baseline work when the
user explicitly asks for renderer validation or when PR-bound work is ready for
its targeted gate.

## Pre-Commit/PR Validate

```bat
tools\validate_dx12_renderer.bat
```

This builds Profile, runs `SkullbonezData/scenes/render_tests.suite` with DX12, checks stdout/stderr, verifies DX12 InfoQueue output, and compares screenshots against committed DX12 baselines.

Expected captures are written to `Profile\`:
- `dx12_screenshot.bmp`, `dx12_solver_smoke.bmp`

Baselines live in `TestOutput\baselines\baseline_dx12_<scene>.png`.

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

- If DX12 crashes or hangs, use `skore-cdb-debug`.
- If local pixel comparison fails, convert the relevant BMPs to PNG before visual inspection.
- Do not send BMPs to image viewers; use PNGs.

The suite is render-only. Performance validation is a separate pre-commit/PR
gate:

```bat
tools\validate_perf.bat
```
