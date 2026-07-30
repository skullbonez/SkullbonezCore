# Validation Tools

Scripts for validating SkullbonezCore changes. These are formal pre-commit/PR
gates, not routine as-you-go checks. Run from the repo root or from within this
directory when PR-bound work is ready, or when the user explicitly asks for
validation.

## Quick Reference

| Script | Use When | Runtime |
|--------|----------|---------|
| `agent_validate.bat` | PR gate when truly unsure; delegates once to `validate_full.bat` | CPU tests + 5 engine processes |
| `validate_select.bat` | Run any subset of validations by name | ~depends |
| `validate_fast.bat` | Small code refactors: preflight plus the doctest runner | ~30s |
| `validate_all_cpu_tests.bat` | Run every mandatory CPU test and coverage gate with fail-fast attribution | incremental builds + 7 console launches |
| `validate_tests.bat` | Build and run the doctest unit-test executable | build + console test runner |
| `validate_coverage.bat` | Build the Debug doctest runner, export Cobertura product coverage, and report/check versioned subsystem floors | incremental Debug build + console test runner |
| `validate_runtime_interaction_policy.bat` | Build/run Debug and Release interaction-policy tests | 2 console test launches |
| `validate_automation.bat` | Prove Profile excludes scripted diagnostics and run one combined replay/prediction plus development-UI command smoke in Automation | build + 2 engine processes |
| `validate_scene_parser_tests.bat` | Build/run CPU-side scene/style parser contract tests | build + console test runner |
| `validate_ui_boundary_tests.bat` | Build/run the production UI library with no Runtime, Rendering, or DX12 link dependency | Release build + console test runner |
| `validate_dx12_arch_tests.bat` | Build/run CPU-side renderer architecture tests; no device creation | build + console test runner |
| `validate_dx12_fault_injection.bat` | Debug runtime proof that the first injected DX12 submission failure exits nonzero and issues zero submissions | build + one bounded engine launch |
| `validate_native_diagnostics.bat` | Opt-in MSVC AddressSanitizer and bounded native static-analysis lane | ~20s; no engine launch |
| `validate_dx12_renderer.bat` | DX12-only screenshot regression and InfoQueue gate | ~2 min |
| `validate_renderers.bat` | Retired compatibility alias that runs `validate_dx12_renderer.bat` | ~2 min |
| `validate_alt_velocity_visualization.bat` | Real ALT-VEL drag with a selected-path preview and release-only rebuild assertions | build + two engine processes |
| `validate_deep.bat` | Opt-in broad sweep: render, deep physics, ALT-VEL interaction, and perf | ~depends |
| `validate_concepts.bat` | Finite smoke/core/full concept-scene validation tiers | ~depends |
| `validate_shaders.bat` | Shader stage, cbuffer uniform, and resource-slot contract drift helper | ~depends |
| `validate_project_filters.bat` | Visual Studio project/filter drift plus transitive JSON cold-boundary fence | ~depends |
| `validate_dependency_graph.bat` | Data-driven include direction, planted generated-proof drift, Runtime package closure, Replay boundary, retired ownership-vocabulary deletion, and exact project-ownership XML/path fixtures plus repository scan | ~2s |
| `validate_ui.bat` | Optional in-game UI visual screenshots, blur, and control automation | ~depends |
| `validate_ui_stress.bat` | Deterministic Legacy plus ImGui editor stress matrix with exclusive hot swaps, scene/replay churn, resize/DPI captures, and DX12 checks | ~depends |
| `launch_tracy_viewer.bat [--build-only]` | Build the pinned external Tracy profiler on first use and connect it to the local engine; `--build-only` verifies without starting the GUI | first use depends |
| `validate_demo_stress.bat` | Generated demo scene plus UI interaction crash sweep | ~depends |
| `run_graphics_stress.bat` | General DX12 graphics stress fuzzer with scene/settings churn and memory telemetry | bounded or overnight |
| `validate_physics.bat` | Standalone physics API smoke plus core physics, collision, solver, and rigid body baseline | 2 exe launches |
| `validate_physics_deep.bat` | Opt-in bullet sweep, shooting, known-issue, and SkullScope physics baselines | ~45s+ |
| `validate_physics_query.bat` | SkullScope query-output baseline check | ~depends |
| `validate_perf.bat` | Hard gate for DX12, physics, and hot-path perf budgets/regressions | ~1 min |
| `validate_replay_allocation_policy.bat` | Strict two-generation Replay allocation/owner probe | ~20 s |
| `validate_full.bat` | Default broad PR gate: mandatory CPU, Automation replay smoke, DX12 renderer, and core physics | CPU tests + 5 engine processes |
| `watch_ui_stress.bat` | Repeated UI stress watcher, finite by default | ~depends |
| `watch_demo_stress.bat` | Repeated generated demo stress watcher, finite by default | ~depends |

## Broad Gate Composition

`validate_full.bat` is the broad mandatory superset. It runs these owners in
order and stops before any engine launch when a CPU target fails:

1. `validate_fast.bat --preflight-only` runs formatting, production project
   metadata, staged-size policy, and the Profile build without a test launch.
2. `validate_all_cpu_tests.bat` runs the doctest, enforced coverage floors,
   runtime-interaction, scene parser, renderer-free UI boundary, and DX12
   architecture targets.
3. `validate_automation.bat` proves Profile rejects diagnostic scripts, builds
   the dedicated Automation executable, and runs one combined replay/prediction
   plus development-UI command smoke required on every broad pre-commit pass.
4. The Debug build, DX12 renderer gate, and core physics determinism gate run
   only after the mandatory CPU and automation lanes pass. Automation launches
   two engine processes, rendering launches one, and physics launches its
    engine lifecycle smoke and regression scene, for five engine processes in total.

## Unit Coverage Floors

`validate_coverage.bat` builds the Debug doctest runner, captures product-line
Cobertura XML with OpenCppCoverage, then applies the versioned tier map in
`coverage_floors.json`. It is part of `validate_all_cpu_tests.bat`, so the full
PR gate enforces Tier 1 at 85%, Tier 2 at 70%, and Tier 3 at 50%; whole-product
coverage remains informational.

Run `tools\validate_coverage.bat` directly when changing coverage floors,
exclusions, instrumentation scope, coverage tooling, or tests intended to raise
subsystem coverage. Also run it as the final pre-commit/PR gate when explicit
confirmation against the ratified floors is required. Do not run it again after
`validate_all_cpu_tests.bat`: that umbrella already invokes it, and
`validate_full.bat`, `agent_validate.bat`, and hosted mandatory CPU CI all use
the same umbrella.

Each subsystem also lists `required_instrumented_sources`. The checker fails if
one of those translation units disappears from the XML, preventing a link or
project-file omission from silently shrinking a denominator. Tier-4 and
separate-gate owners are recorded in the config's exclusions and scope rulings.
Run the checker policy tests directly with:

```bat
python tools\check_coverage.py --self-test
```

Direct `validate_fast.bat` use still runs `SKULLBONEZ_TESTS.exe`. Its
`--preflight-only` switch is an internal composition mode for `validate_full`;
it prevents the doctest runner from being executed both by fast validation and
the CPU umbrella. `agent_validate.bat` delegates once to `validate_full.bat`, so
it has the same ordering and exit status.

The file-size preflight reads the git index for local pending commits. Hosted
pull-request jobs set `SKORE_SIZE_DIFF_BASE` so the same gate compares changed
HEAD blobs with the pull-request base instead of silently inspecting a clean CI
index. Both modes disable Git rename detection so moving an allowlisted blob to
an ordinary path checks the destination under its new policy.

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
tools\validate_select.bat automation
tools\validate_select.bat scene-parser-tests
tools\validate_select.bat dx12-arch-tests
tools\validate_select.bat all-cpu-tests
tools\validate_select.bat tests
tools\validate_select.bat ui
tools\validate_select.bat build-profile
tools\validate_select.bat build-automation
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
| `generate_physics_scale_sleepy_scene.py --check\|--write` | Check or deterministically regenerate the 5,000-body sleeping-heavy scale fixture |
| `validate_format.bat` | Check the composite clang-format pipeline, including assignment heads, multiline statement/control-flow spacing, header comment alignment, and `Related:` path resolution |
| `format_fix.bat` | Auto-fix C++ layout, keep the first assigned expression beside `=`, separate wrapped statements/control blocks, and align header comments |
| `separate_multiline_cpp_declarations.py --check\|--check-pipeline\|--write\|--stdin` | Keep assignment heads together and add semantic paragraph breaks; `--stdin` previews the post-pass without touching files |
| `check_related_paths.py [--self-test]` | Verify repository-relative paths in tracked source learning-header `Related:` blocks and exercise live/dead fixtures |
| `inventory_authority_free_aggregates.py [--repo .] [--strict] [--self-test] [--format text\|json\|markdown] [--output path]` | Discover data-bearing structs/classes without suffix filtering; report members, behavior, stated invariants, lexical sites, and joined owner rulings; `--strict` fails on an unruled bounded legacy-suffix/no-invariant row, while stale, malformed, or source-drifted rulings always fail |
| `inventory_extraction_scars.py [--repo .] [--self-test]` | Report function-block member-prefixed locals and pure reference aliases, including control/direct initializers and structured bindings; fail on a finding with no owner ruling |
| `cpp_source_scan.py` | Shared tracked-source enumeration and comment/literal masking for the two inventories; masking is imported from `inventory_wide_signatures.py` so there is one implementation |
| `validate_build.bat <Config>` | Build a specific configuration (`Debug`, `Profile`, `Automation`, `Release`) |
| `validate_all_cpu_tests.bat` | Run all six first-party CPU/coverage gates, stop at the first failure, print a combined summary, and preserve the child exit code |
| `validate_tests.bat` | Build `SKULLBONEZ_TESTS`, validate its project filters, and run the doctest console runner |
| `validate_concepts.bat [smoke\|core\|full] [dx12] [frames]` | Run finite concept-scene tiers and write logs plus JSON under `TestOutput\validation\concepts` |
| `validate_shaders.bat` | Check shader file contracts from `tools\shader_contracts.json`; incomplete symbol, uniform, or resource coverage is reported as warnings |
| `validate_project_filters.bat` | Check `.vcxproj` and `.vcxproj.filters` item coverage, exact path casing, source/header category pairing, scene/style/shader filters, declared filter names, and exact transitive JSON reachability for the ratified 19 cold-boundary translation units |
| `validate_dependency_graph.bat` | Run data-only package, planted proof-drift, malformed-marker, residual-parser, retired-vocabulary, and exact missing/duplicate project-owner XML/path fixtures; freshness-check the marked `AGENTS.md` projection; then scan live source includes, bounded deleted concepts, and dedicated project ownership such as `SkullbonezSource/UI` in `SKULLBONEZ_UI.vcxproj` |
| `validate_runtime_interaction_policy.bat` | CPU-only checks for runtime interaction ownership, pointer capture, camera-look, and physics-step policy |
| `validate_automation.bat` | Pre-commit boundary check plus one combined replay/prediction and development-UI command smoke in the diagnostics-only Automation build |
| `validate_replay_visual_fidelity.bat` | Authoritative frame-exact 200-box replay gate: one hidden engine process, one prediction generation, immutable golden comparison, offline artifact round-trip, and false-pass controls |
| `validate_replay_allocation_policy.bat` | Builds Automation, runs one hidden two-generation tornado prediction process, and requires zero gameplay/reserve violations plus a complete frame-180 interaction report |
| `validate_replay_scrub.bat` | Historical replay-scrub entry point; delegates exclusively to `validate_replay_visual_fidelity.bat` and preserves its failure status |
| `validate_alt_velocity_visualization.bat` | Builds Automation and runs instant/amortized N-body ALT-VEL drags, requiring a live selected-path preview, zero held-drag restarts, and release-only authoritative replacement |
| `validate_ui.bat` | Optional DX12 UI suite that captures UI screenshots and checks blur strength |
| `validate_ui_stress.bat` | Run the Legacy UI backdrop sweep, then an ImGui editor matrix covering exclusive hot swaps, exact scene transition, typed replay scrub, panel/layout churn, minimum/default/ultrawide captures, descriptor bounds, logs, and DX12 validation |
| `validate_demo_stress.bat` | Generated demo scene crash sweep that keeps physics/rendering active while changing UI settings |
| `run_graphics_stress.bat [minutes\|overnight] [seed] [actions] [sceneInterval] [memoryInterval]` | General DX12 graphics stress runner; writes stdout, stderr, CSV, and JSON memory artifacts under `TestOutput\graphics_stress` |
| `validate_dx12_renderer.bat` | Build or reuse Profile, run only DX12 render-test scenes, check InfoQueue, and compare screenshots against DX12 baselines |
| `validate_dx12_fault_injection.bat` | Build Debug, inject immediately before the first DX12 queue submission, and verify nonzero exit, bounded diagnostics, zero submissions, and zero InfoQueue errors |
| `validate_deep.bat` | Opt-in broad validation pipeline for expensive sweeps |
| `validate_physics.bat` | Build or reuse Debug, run the shipping PhysicsEngine lifecycle smoke, and compare all 44,401 rows from `physics_bench_varied.scene.json` against `physics_regression_varied.csv` byte-for-byte |
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
build in Debug, Profile, Profile-WPO, Automation, and Release. Visual Studio fast up-to-date
skipping is disabled for that project so an HLSL-only edit still reaches the
bake; shader compiler diagnostics and a nonzero bake exit fail the build.

`validate_perf.bat` is a hard gate: baseline regressions and
`check_perf_budgets.py` absolute-budget failures return nonzero. Do not treat
perf output as a warning-only review note unless the script itself exits 0.
It also runs the deterministic 200/520/1,000/2,000-body physics scale matrix.
Those four artifacts are measurement-only: they report stage timing for the
retained scalar SoA path without weakening the existing DX12 and physics-bench
baseline or absolute-budget comparisons.

`validate_replay_visual_fidelity.bat` is the single replay presentation oracle.
Each invocation starts exactly one hidden Automation engine process and permits
exactly one prediction generation. It compares all 2,401 presentation ticks
through the complete 200-box wall cascade, proves the saved prediction state by
an in-process CPU projection after the last rendered reveal plus offline
artifact checks, and never updates its committed manifests. The projection has
no render-backend access and generation capability is permanently disabled
before RVPD is decoded. `validate_replay_scrub.bat` is only a delegating alias
and must not grow a parallel replay oracle or launch the engine again.

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
