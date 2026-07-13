# Validation Tools

Scripts for validating SkullbonezCore changes. These are formal pre-commit/PR
gates, not routine as-you-go checks. Run from the repo root or from within this
directory when PR-bound work is ready, or when the user explicitly asks for
validation.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | PR gate when truly unsure; delegates once to `validate_full.bat` | CPU tests + 3 engine processes |
| `validate_select.bat` | Run any subset of validations by name | ~depends |
| `validate_fast.bat` | Small code refactors: preflight plus the doctest runner | ~30s |
| `validate_all_cpu_tests.bat` | Run every mandatory CPU test target once with fail-fast attribution | incremental builds + 5 console launches |
| `validate_tests.bat` | Build and run the doctest unit-test executable | build + console test runner |
| `validate_replay_scrub.bat` | Replay scrub/restore, prediction determinism, submitted-geometry stability, and predicted-vs-live solver fidelity | build + bounded engine probes |
| `validate_runtime_interaction_policy.bat` | Build/run Debug and Release interaction-policy tests | 2 console test launches |
| `validate_scene_parser_tests.bat` | Build/run CPU-side scene/style parser contract tests | build + console test runner |
| `validate_dx12_arch_tests.bat` | Build/run CPU-side renderer architecture tests; no device creation | build + console test runner |
| `validate_dx12_fault_injection.bat` | Debug runtime proof that the first injected DX12 submission failure exits nonzero and issues zero submissions | build + one bounded engine launch |
| `validate_native_diagnostics.bat` | Opt-in MSVC AddressSanitizer and bounded native static-analysis lane | ~20s; no engine launch |
| `validate_dx12_renderer.bat` | DX12-only screenshot regression and InfoQueue gate | ~2 min |
| `validate_renderers.bat` | Retired compatibility alias that runs `validate_dx12_renderer.bat` | ~2 min |
| `validate_deep.bat` | Opt-in broad sweep: render, deep physics, and perf | ~depends |
| `validate_concepts.bat` | Finite smoke/core/full concept-scene validation tiers | ~depends |
| `validate_shaders.bat` | Shader stage, cbuffer uniform, and resource-slot contract drift helper | ~depends |
| `validate_project_filters.bat` | Visual Studio `.vcxproj.filters` category and path-casing drift helper | ~depends |
| `validate_ui.bat` | Optional in-game UI visual screenshots, blur, and control automation | ~depends |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep | ~10s |
| `validate_demo_stress.bat` | Generated demo scene plus UI interaction crash sweep | ~depends |
| `run_graphics_stress.bat` | General DX12 graphics stress fuzzer with scene/settings churn and memory telemetry | bounded or overnight |
| `validate_physics.bat` | Standalone physics API smoke plus core physics, collision, solver, and rigid body baseline | 2 exe launches |
| `validate_physics_deep.bat` | Opt-in bullet sweep, shooting, known-issue, and SkullScope physics baselines | ~45s+ |
| `validate_physics_query.bat` | SkullScope query-output baseline check | ~depends |
| `validate_perf.bat` | Hard gate for DX12, physics, and hot-path perf budgets/regressions | ~1 min |
| `validate_full.bat` | Default broad PR gate: mandatory CPU lane, DX12 renderer, and core physics | CPU tests + 3 engine processes |
| `watch_ui_stress.bat` | Repeated UI stress watcher, finite by default | ~depends |
| `watch_demo_stress.bat` | Repeated generated demo stress watcher, finite by default | ~depends |

## Broad Gate Composition

`validate_full.bat` is the broad mandatory superset. It runs these owners in
order and stops before any engine launch when a CPU target fails:

1. `validate_fast.bat --preflight-only` runs formatting, production project
   metadata, staged-size policy, and the Profile build without a test launch.
2. `validate_all_cpu_tests.bat` runs the doctest, runtime-interaction, scene
   parser, and DX12 architecture targets exactly once.
3. The Debug build, DX12 renderer gate, and core physics determinism gate run
   only after the mandatory CPU lane passes. The renderer lane launches one
   engine process; physics launches its standalone smoke and regression scene,
   for three engine processes in total.

Direct `validate_fast.bat` use still runs `SKULLBONEZ_TESTS.exe`. Its
`--preflight-only` switch is an internal composition mode for `validate_full`;
it prevents the doctest runner from being executed both by fast validation and
the CPU umbrella. `agent_validate.bat` delegates once to `validate_full.bat`, so
it has the same ordering and exit status.

The file-size preflight reads the git index for local pending commits. Hosted
PR and merge-queue jobs set `SKORE_SIZE_DIFF_BASE` so the same gate compares
changed HEAD blobs with the event base instead of silently inspecting a clean
CI index. Both modes disable Git rename detection so moving an allowlisted blob
to an ordinary path checks the destination under its new policy.

## Native Diagnostics

Run `tools\validate_native_diagnostics.bat` for the opt-in native safety lane.
It builds and runs an isolated AddressSanitizer copy of
`SKULLBONEZ_TESTS`, then runs MSVC `/analyze` over the five maths-library
translation units. Artifacts stay under
`TestOutput\validation\native_diagnostics`; normal Debug/Profile outputs are
not replaced. Use `--prove-asan-fixture` only to self-test the detector: that
mode generates a temporary heap-use-after-free, requires the exact sanitizer
diagnostic, and removes the faulty source and executable before returning.

Static-analysis exceptions live in
`tools\native_diagnostics_suppressions.json`. Each row must match one exact
path/code and name its owner, reason, deletion condition, and review evidence;
stale rows fail the lane.

`tools\validate_native_diagnostics.bat --self-test` exercises warning
classification and bounded-log guards without invoking Visual Studio.

`.github/workflows/native-diagnostics.yml` runs the detector proof and healthy
lanes weekly and on manual dispatch. It is an informational signal rather than
a required pull-request check.

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
tools\validate_select.bat runtime-interaction-policy
tools\validate_select.bat scene-parser-tests
tools\validate_select.bat dx12-arch-tests
tools\validate_select.bat all-cpu-tests
tools\validate_select.bat tests
tools\validate_select.bat ui
tools\validate_select.bat build-profile
```

### Direct Stress Runners

General graphics stress runs directly so short probes and overnight soaks can
choose their own duration, seed, mutation rate, scene interval, and memory sample
interval without hiding that state inside a selector target:

```bat
tools\run_graphics_stress.bat 1
tools\run_graphics_stress.bat overnight 3235774467 16 36 1800
```

## Utility Scripts

| Script | Purpose |
|--------|---------|
| `refresh_hulls.bat` | Rewrite every committed convex hull asset from source geometry, then verify the result |
| `bake_hulls.bat --check\|--write` | Check or rewrite serialized convex hull v2 runtime data from source geometry |
| `migrate_data_formats.py --check\|--write` | Check or upgrade asset-library, hull, and engine-config files to their current owned versions |
| `validate_format.bat` | Check clang-format compliance without auto-fixing |
| `format_fix.bat` | Auto-fix formatting in-place |
| `validate_build.bat <Config>` | Build a specific configuration (`Debug`, `Profile`, `Release`) |
| `validate_all_cpu_tests.bat` | Run all four first-party CPU test gates, stop at the first failure, print a combined summary, and preserve the child exit code |
| `validate_tests.bat` | Build `SKULLBONEZ_TESTS`, validate its project filters, and run the doctest console runner |
| `validate_concepts.bat [smoke\|core\|full] [dx12] [frames]` | Run finite concept-scene tiers and write logs plus JSON under `TestOutput\validation\concepts` |
| `validate_shaders.bat` | Check shader file contracts from `tools\shader_contracts.json`; incomplete symbol, uniform, or resource coverage is reported as warnings |
| `validate_project_filters.bat` | Check `.vcxproj` and `.vcxproj.filters` item coverage, exact path casing, source/header category pairing, scene/style/shader filters, and declared filter names |
| `validate_runtime_interaction_policy.bat` | CPU-only checks for runtime interaction ownership, pointer capture, camera-look, and physics-step policy |
| `validate_ui.bat` | Optional DX12 UI suite that captures UI screenshots and checks blur strength |
| `validate_ui_stress.bat` | Single deterministic UI-only stress crash sweep over a UI backdrop |
| `validate_demo_stress.bat` | Generated demo scene crash sweep that keeps physics/rendering active while changing UI settings |
| `run_graphics_stress.bat [minutes\|overnight] [seed] [actions] [sceneInterval] [memoryInterval]` | General DX12 graphics stress runner; writes stdout, stderr, CSV, and JSON memory artifacts under `TestOutput\graphics_stress` |
| `validate_dx12_renderer.bat` | Build or reuse Profile, run only DX12 render-test scenes, check InfoQueue, and compare screenshots against DX12 baselines |
| `validate_dx12_fault_injection.bat` | Build Debug, inject immediately before the first DX12 queue submission, and verify nonzero exit, bounded diagnostics, zero submissions, and zero InfoQueue errors |
| `validate_deep.bat` | Opt-in broad validation pipeline for expensive sweeps |
| `validate_physics.bat` | Build or reuse Debug, run the standalone physics API smoke, and compare all 44,401 rows from `physics_bench_varied.scene.json` against `physics_regression_varied.csv` byte-for-byte |
| `validate_physics_deep.bat` | Run the old broad physics sweep, known-issue checks, shooting reaction check, and SkullScope query baseline |
| `watch_ui_stress.bat [--test ui\|demo] [--iterations N] [--sleep N] [--forever]` | Repeated stress watcher; defaults to a finite 25-lap UI-only run and requires `--forever` for an intentional soak |
| `watch_demo_stress.bat [--iterations N] [--sleep N] [--forever]` | Convenience wrapper for repeated generated demo interaction stress |
| `capture_ui_screenshot.bat [dx12] [output.png] [max_width]` | Capture the profiler UI scene and export a phone-friendly PNG |
| `check_perf_budgets.py --artifact <perf.json>` | Fail when critical frame, physics, or render markers exceed absolute millisecond budgets |
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
| `bake_shaders.bat` | Bake all shipping raster/compute shaders with pinned DXC and generate fixed reflection POD metadata; `--check` verifies bytecode, hashes, and metadata freshness |

`SKULLBONEZ_CORE.vcxproj` runs `bake_shaders.bat` before every Visual Studio
build in Debug, Profile, Profile-WPO, and Release. Visual Studio fast up-to-date
skipping is disabled for that project so an HLSL-only edit still reaches the
bake; shader compiler diagnostics and a nonzero bake exit fail the build.

`validate_perf.bat` is a hard gate: baseline regressions and
`check_perf_budgets.py` absolute-budget failures return nonzero. Do not treat
perf output as a warning-only review note unless the script itself exits 0.

## Physics Baselines

Physics CSV and SkullScope JSON baselines are byte-exact behavior artifacts.
The normal physics gate uses the authored 37-body, 1,200-frame varied scene as
its full CSV contract. The deep gate retains the older seeded solver distribution
as an exact SHA-256 signature in `physics_known_issue_signatures.json`.
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

The CPU umbrella returns the first failing child gate's code unchanged; its
summary identifies completed, failed, and not-run targets.

## Prerequisites

- Visual Studio with C++ and LLVM tools
- Git for Windows
- Python 3.x with Pillow (`py -m pip install Pillow`)
- Built executable in `Profile\` for render/perf tests or `Debug\` for physics tests
