# Validation Tools

Scripts for validating SkullbonezCore changes. These are formal pre-commit/PR
gates, not routine as-you-go checks. Run from the repo root or from within this
directory when PR-bound work is ready, or when the user explicitly asks for
validation.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | PR gate when truly unsure, runs everything | ~3 min |
| `validate_select.bat` | Run any subset of validations by name | ~depends |
| `validate_fast.bat` | Small code refactors and non-render code edits | ~30s |
| `validate_renderers.bat` | Shader, texture, render backend changes | ~60s |
| `validate_concepts.bat` | Finite smoke/core/full concept-scene validation tiers | ~depends |
| `validate_shaders.bat` | Shader stage manifest and contract drift helper | ~depends |
| `validate_ui.bat` | Optional in-game UI visual screenshots, blur, and control automation | ~depends |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep | ~10s |
| `validate_demo_stress.bat` | Generated demo scene plus UI interaction crash sweep | ~depends |
| `validate_physics.bat` | Physics, collision, solver, rigid body, bullet sweep collision-time baselines | ~45s |
| `validate_physics_query.bat` | SkullScope query-output baseline check | ~depends |
| `validate_perf.bat` | Performance-sensitive, hot-path changes | ~1 min |
| `validate_full.bat` | Broad PR-bound changes, pre-merge, uncertain scope | ~3 min |
| `watch_ui_stress.bat` | Repeated UI stress watcher, finite by default | ~depends |
| `watch_demo_stress.bat` | Repeated generated demo stress watcher, finite by default | ~depends |

### Selection Example

Run only the targeted gate you need:

```bat
tools\validate_select.bat format
tools\validate_select.bat renderers physics
tools\validate_select.bat concepts
tools\validate_select.bat shaders
tools\validate_select.bat ui
tools\validate_select.bat fast build-profile
```

## Utility Scripts

| Script | Purpose |
|--------|---------|
| `validate_format.bat` | Check clang-format compliance without auto-fixing |
| `format_fix.bat` | Auto-fix formatting in-place |
| `validate_build.bat <Config>` | Build a specific configuration (`Debug`, `Profile`, `Release`) |
| `validate_concepts.bat [smoke\|core\|full] [gl\|dx11\|dx12\|all] [frames]` | Run finite concept-scene tiers and write logs plus JSON under `TestOutput\validation\concepts` |
| `validate_shaders.bat` | Check shader file contracts from `tools\shader_contracts.json`; incomplete symbol/resource coverage is reported as warnings |
| `validate_ui.bat` | Optional tri-renderer UI suite that captures UI screenshots and checks blur strength |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep over a UI backdrop |
| `validate_demo_stress.bat` | Generated demo scene crash sweep that keeps physics/rendering active while changing UI settings and renderers |
| `watch_ui_stress.bat [--test ui\|demo] [--iterations N] [--sleep N] [--forever]` | Repeated stress watcher; defaults to a finite 25-lap UI-only run and requires `--forever` for an intentional soak |
| `watch_demo_stress.bat [--iterations N] [--sleep N] [--forever]` | Convenience wrapper for repeated generated demo interaction stress |
| `capture_ui_screenshot.bat [gl\|dx11\|dx12] [output.png] [max_width]` | Capture the profiler UI scene and export a phone-friendly PNG |
| `export_screenshot_png.py <input.bmp> <output.png>` | Convert an engine BMP capture to an optimized PNG |
| `validate_physics_query.bat` | Generate the varied physics diagnostic trace and compare SkullScope query output to `TestOutput/baselines/physics_query_varied.json` |
| `find_clang_format.bat` | Locate clang-format, called by format scripts |
| `find_git.bat` | Locate Git, called by perf validation |
| `find_msbuild.bat` | Locate MSBuild, called by other scripts |
| `find_python.bat` | Locate Python, called by Python-backed validation scripts |
| `physics_query.bat` | Windows launcher for SkullScope; invokes `physics_query.py` through `find_python.bat` |
| `physics_query.py` | SkullScope: import queryable physics NDJSON traces into SQLite and return bounded JSON summaries/events/frame/body/contact/island queries |
| `check_physics_query_regression.py` | SkullScope baseline checker used by `validate_physics_query.bat` and `validate_physics.bat` |
| `check_dx12_validation.bat` | Verify DX12 InfoQueue clean |
| `check_parity.py` | Cross-renderer pixel comparison plus manifest, side-by-side, heatmap, and JSON summary artifacts |
| `check_physics_regression.py` | Byte-exact physics and bullet collision-time CSV diff |
| `update_baselines.bat` | Copy current Profile visual/perf artifacts into `TestOutput\baselines` |
| `archive_validation_artifacts.bat` | Archive current Profile artifacts under `TestOutput\NNN_<commit>` |

## Exit Codes

All scripts follow this convention:

- `0` = pass
- `1-98` = failure, with the code indicating which step failed
- `99` = tool not found, such as MSBuild, clang-format, Python, or Pillow

## Prerequisites

- Visual Studio with C++ and LLVM tools
- Git for Windows
- Python 3.x with Pillow (`py -m pip install Pillow`)
- Built executable in `Profile\` for render/perf tests or `Debug\` for physics tests
