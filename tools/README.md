# Validation Tools

Scripts for validating SkullbonezCore changes. Run from the repo root or from within this directory.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | Unsure what to run, runs everything | ~3 min |
| `validate_select.bat` | Run any subset of validations by name | ~depends |
| `validate_fast.bat` | Small code refactors and non-render code edits | ~30s |
| `validate_renderers.bat` | Shader, texture, render backend changes | ~60s |
| `validate_ui.bat` | Optional in-game UI visual screenshots, blur, and control automation | ~depends |
| `validate_physics.bat` | Physics, collision, solver, rigid body | ~45s |
| `validate_physics_query.bat` | SkullScope query-output baseline check | ~depends |
| `validate_perf.bat` | Performance-sensitive, hot-path changes | ~1 min |
| `validate_full.bat` | Broad changes, pre-merge, uncertain scope | ~3 min |

### Selection Example

Run only what you need:

```bat
tools\validate_select.bat format
tools\validate_select.bat renderers physics
tools\validate_select.bat ui
tools\validate_select.bat fast build-profile
```

## Utility Scripts

| Script | Purpose |
|--------|---------|
| `validate_format.bat` | Check clang-format compliance without auto-fixing |
| `format_fix.bat` | Auto-fix formatting in-place |
| `validate_build.bat <Config>` | Build a specific configuration (`Debug`, `Profile`, `Release`) |
| `validate_ui.bat` | Optional tri-renderer UI suite that captures UI screenshots and checks blur strength |
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
| `check_parity.py` | Cross-renderer pixel comparison |
| `check_physics_regression.py` | Byte-exact physics CSV diff |
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
