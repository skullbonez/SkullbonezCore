# Validation Tools

Scripts for validating SkullbonezCore changes. These are formal pre-commit/PR
gates, not routine as-you-go checks. Run from the repo root or from within this
directory when PR-bound work is ready, or when the user explicitly asks for
validation.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | PR gate when truly unsure; delegates to the two-launch default gate | 2 exe launches |
| `validate_select.bat` | Run any subset of validations by name | ~depends |
| `validate_fast.bat` | Small code refactors and non-render code edits | ~30s |
| `validate_dx12_renderer.bat` | DX12-only screenshot regression and InfoQueue gate | ~2 min |
| `validate_renderers.bat` | Retired compatibility alias that runs `validate_dx12_renderer.bat` | ~2 min |
| `validate_deep.bat` | Opt-in broad sweep: render, deep physics, and perf | ~depends |
| `validate_concepts.bat` | Finite smoke/core/full concept-scene validation tiers | ~depends |
| `validate_shaders.bat` | Shader stage, cbuffer uniform, and resource-slot contract drift helper | ~depends |
| `validate_project_filters.bat` | Visual Studio `.vcxproj.filters` category and path-casing drift helper | ~depends |
| `validate_ui.bat` | Optional in-game UI visual screenshots, blur, and control automation | ~depends |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep | ~10s |
| `validate_demo_stress.bat` | Generated demo scene plus UI interaction crash sweep | ~depends |
| `validate_physics.bat` | Core physics, collision, solver, and rigid body baseline | 1 exe launch |
| `validate_physics_deep.bat` | Opt-in bullet sweep, shooting, known-issue, and SkullScope physics baselines | ~45s+ |
| `validate_physics_query.bat` | SkullScope query-output baseline check | ~depends |
| `validate_perf.bat` | DX12 performance-sensitive, hot-path changes | ~1 min |
| `validate_full.bat` | Default broad PR gate: DX12 renderer plus core physics | 2 exe launches |
| `watch_ui_stress.bat` | Repeated UI stress watcher, finite by default | ~depends |
| `watch_demo_stress.bat` | Repeated generated demo stress watcher, finite by default | ~depends |

### Selection Example

Run only the targeted gate you need:

```bat
tools\validate_select.bat format
tools\validate_select.bat dx12-renderer
tools\validate_select.bat dx12-renderer physics
tools\validate_select.bat physics-deep
tools\validate_select.bat deep
tools\validate_select.bat concepts
tools\validate_select.bat shaders
tools\validate_select.bat project-filters
tools\validate_select.bat ui
tools\validate_select.bat build-profile
```

## Utility Scripts

| Script | Purpose |
|--------|---------|
| `refresh_hulls.bat` | Rewrite every committed convex hull asset from source geometry, then verify the result |
| `bake_hulls.bat --check\|--write` | Check or rewrite serialized convex hull v2 runtime data from source geometry |
| `validate_format.bat` | Check clang-format compliance without auto-fixing |
| `format_fix.bat` | Auto-fix formatting in-place |
| `validate_build.bat <Config>` | Build a specific configuration (`Debug`, `Profile`, `Release`) |
| `validate_concepts.bat [smoke\|core\|full] [dx12] [frames]` | Run finite concept-scene tiers and write logs plus JSON under `TestOutput\validation\concepts` |
| `validate_shaders.bat` | Check shader file contracts from `tools\shader_contracts.json`; incomplete symbol, uniform, or resource coverage is reported as warnings |
| `validate_project_filters.bat` | Check `.vcxproj` and `.vcxproj.filters` item coverage, exact path casing, source/header category pairing, scene/style/shader filters, and declared filter names |
| `validate_ui.bat` | Optional DX12 UI suite that captures UI screenshots and checks blur strength |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep over a UI backdrop |
| `validate_demo_stress.bat` | Generated demo scene crash sweep that keeps physics/rendering active while changing UI settings |
| `validate_dx12_renderer.bat` | Build or reuse Profile, run only DX12 render-test scenes, check InfoQueue, and compare screenshots against DX12 baselines |
| `validate_deep.bat` | Opt-in broad validation pipeline for expensive sweeps |
| `validate_physics.bat` | Build or reuse Debug, run one core physics scene, and compare `physics_regression_solver.csv` |
| `validate_physics_deep.bat` | Run the old broad physics sweep, known-issue checks, shooting reaction check, and SkullScope query baseline |
| `watch_ui_stress.bat [--test ui\|demo] [--iterations N] [--sleep N] [--forever]` | Repeated stress watcher; defaults to a finite 25-lap UI-only run and requires `--forever` for an intentional soak |
| `watch_demo_stress.bat [--iterations N] [--sleep N] [--forever]` | Convenience wrapper for repeated generated demo interaction stress |
| `capture_ui_screenshot.bat [dx12] [output.png] [max_width]` | Capture the profiler UI scene and export a phone-friendly PNG |
| `export_screenshot_png.py <input.bmp> <output.png>` | Convert an engine BMP capture to an optimized PNG |
| `validate_physics_query.bat` | Generate one varied physics diagnostic trace and compare SkullScope query output to `TestOutput/baselines/physics_query_varied.json` |
| `find_clang_format.bat` | Locate clang-format, called by format scripts |
| `find_git.bat` | Locate Git, called by perf validation |
| `find_msbuild.bat` | Locate MSBuild, called by other scripts |
| `find_python.bat` | Locate Python, called by Python-backed validation scripts |
| `physics_query.bat` | Windows launcher for SkullScope; invokes `physics_query.py` through `find_python.bat` |
| `physics_query.py` | SkullScope: import queryable physics NDJSON traces into SQLite and return bounded JSON summaries/events/frame/body/contact/island queries |
| `check_physics_query_regression.py` | SkullScope baseline checker used by `validate_physics_query.bat` and `validate_physics_deep.bat` |
| `check_dx12_validation.bat` | Verify DX12 InfoQueue clean |
| `check_dx12_baselines.py` | Compare DX12 captures with committed DX12 baselines and write manifest/summary artifacts |
| `check_physics_regression.py` | Byte-exact core physics CSV diff, with `--deep` for the broader CSV set |
| `update_baselines.bat` | Copy current Profile visual/perf artifacts into `TestOutput\baselines`; do not use for physics CSV or SkullScope baselines |
| `archive_validation_artifacts.bat` | Archive current Profile artifacts under `TestOutput\NNN_<commit>` |

## Physics Baselines

Physics CSV and SkullScope JSON baselines are byte-exact behavior artifacts.
When a physics baseline update is intentional, copy it only from the final Debug
artifact produced by the same scene/config state that will be committed, then
rerun the matching gate:

```bat
tools\validate_physics.bat
tools\validate_physics_deep.bat
```

The commit should include both the baseline file and the validation output. A
copied physics artifact is not considered verified until the matching physics
gate passes against the committed baseline.

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
