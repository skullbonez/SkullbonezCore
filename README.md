# SkullbonezCore

SkullbonezCore is a Windows x64 C++17 graphics and physics engine originally written in 2005 and modernized into a DX12-first shader-based engine.

Official production graphics API:
- DirectX 12 with in-process InfoQueue validation

OpenGL and DX11 final parity evidence has been archived on the DX12-only
retirement branch. Runtime launches now use DX12 only; `--renderer dx12` remains
accepted as a compatibility alias, while GL/DX11 runtime choices are retired.

![SkullbonezCore](https://github.com/skullbonez/SkullbonezCore/blob/main/SkullbonezCore.png)

## Start Here

For humans:
1. Read `FIRST_TIME_SETUP.md` if this is a new machine.
2. Build or validate with the scripts in `tools\`.
3. Use `Agentic/Reference/runtime-reference.md` for command-line, scene, physics, and key-binding reference.

For AI agents:
1. Follow the Agent Startup Contract in `AGENTS.md`.
2. Load only the skill, plan, audit, report, or reference file needed for the
   current task.

## Build

```bat
tools\validate_build.bat Debug
tools\validate_build.bat Profile
tools\validate_build.bat Release
```

Manual MSBuild equivalent:

```bat
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Profile /p:Platform=x64
```

Build outputs:
- `Debug\SKULLBONEZ_CORE.exe`
- `Profile\SKULLBONEZ_CORE.exe`
- `Release\SKULLBONEZ_CORE.exe`

## Validation

Validation scripts are formal pre-commit/PR gates, not routine as-you-go checks.
During implementation, use targeted builds, launches, or focused tests only when
they answer a specific question about the fix. Successful validation entry
points finish by rebuilding both `Profile` and `Debug` so the binaries are ready
for launching or F5 debugging. Before PR-bound feature-branch work is committed
or pushed, use the repository scripts instead of retyping long commands:

| Change Type | Command |
|-------------|---------|
| Documentation only | No validation required |
| Small refactor, no render or physics changes | `tools\validate_fast.bat` |
| Renderer, shader, texture, screenshot behavior | `tools\validate_dx12_renderer.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` |
| Hot path or allocation-sensitive work | `tools\validate_perf.bat` |
| Opt-in native lifetime/static analysis | `tools\validate_native_diagnostics.bat` |
| Every first-party CPU test target | `tools\validate_all_cpu_tests.bat` |
| Coverage floors, exclusions, instrumentation, tooling, or coverage-raising tests | `tools\validate_coverage.bat` |
| Broad or uncertain scope | `tools\validate_full.bat` |
| Unsure at the PR gate | `tools\agent_validate.bat` |

Run `tools\validate_coverage.bat` directly for coverage-specific changes and
when a final gate needs explicit proof that ratified subsystem floors still
hold. Do not run it a second time after `validate_all_cpu_tests.bat`:
the CPU umbrella already includes it, and `validate_full.bat`,
`agent_validate.bat`, and hosted mandatory CPU CI all reach it through that
umbrella.

`validate_full` is the mandatory broad superset: it runs cheap preflight checks,
then the doctest, interaction-policy, scene-parser, and DX12-architecture CPU
targets exactly once before any engine launch. Its two runtime lanes use three
engine processes in total: one DX12 renderer suite, then physics standalone
smoke and the core deterministic regression scene. `agent_validate` delegates
once to that same entry point. Use `tools\validate_deep.bat` only for intentional
broad sweeps.

General graphics stress is an opt-in crash, resource-lifetime, and memory-growth
test rather than part of the default PR gate. Use
`tools\run_graphics_stress.bat` for DX12 scene churn, cinematic/sky/fog/ray
settings churn, render-toggle fuzzing, and overnight soaks.

Physics baseline changes are behavior changes. If a physics CSV or SkullScope
baseline is intentionally refreshed, update it from the final Debug executable
and committed scene/config state, then rerun the matching gate:
`tools\validate_physics.bat` for the core varied-scene baseline, or
`tools\validate_physics_deep.bat` for the broader physics/SkullScope baseline
set. `tools\update_baselines.bat` is for visual and perf artifacts, not physics
baselines.

You can also run any targeted subset with one line:

```bat
tools\validate_select.bat fast
tools\validate_select.bat dx12-renderer
tools\validate_select.bat physics dx12-renderer
tools\validate_select.bat physics-deep
tools\validate_select.bat project-filters
tools\validate_select.bat format build-profile
```

Run the general graphics stress test directly:

```bat
tools\run_graphics_stress.bat 1
tools\run_graphics_stress.bat overnight 3235774467 16 36 1800
```

## Common Launches

```bat
Profile\SKULLBONEZ_CORE.exe --suite SkullbonezData\scenes\render_tests.suite.json --vsync off
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\water_ball_test.scene.json --vsync off
Profile\SKULLBONEZ_CORE.exe --fixed-step --scene SkullbonezData\scenes\perf_test.scene.json --vsync off
```

## Repository Map

| What | Path |
|------|------|
| Solution | `SKULLBONEZ_CORE.sln` |
| Source | `SkullbonezSource/` |
| Shaders | `SkullbonezData/shaders/` |
| Style descriptors | `SkullbonezData/styles/` |
| Scenes | `SkullbonezData/scenes/` |
| Baselines | `TestOutput/baselines/` |
| Validation scripts | `tools/` |
| Agent handoff docs | `Agentic/` |

## More Detail

Long-form reference lives outside this file to keep first-read context small:
- `Agentic/Reference/runtime-reference.md`
- `Agentic/Reference/physics-overview.md`
- `Agentic/Plans/`
- `Agentic/Audits/`
