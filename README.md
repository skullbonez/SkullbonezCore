# SkullbonezCore

SkullbonezCore is a Windows x64 C++17 graphics and physics engine originally written in 2005 and modernized into a DX12-first shader-based engine.

Official production graphics API:
- DirectX 12 with in-process InfoQueue validation

Legacy parity/reference renderers kept for validation while they remain in tree
(feature work frozen except final parity-retirement fixes):
- OpenGL 3.3 Core Profile
- DirectX 11

New renderer architecture, shader work, materials, diagnostics, and user-facing features should target DX12 first. OpenGL and DX11 are retained to catch visual drift and convention bugs until the DX12-only validation stack replaces them, not as long-term product targets. While the parity renderers exist, they are expected to produce visually equivalent output in the renderer validation suite.

![SkullbonezCore](https://github.com/skullbonez/SkullbonezCore/blob/main/SkullbonezCore.png)

## Start Here

For humans:
1. Read `FIRST_TIME_SETUP.md` if this is a new machine.
2. Build or validate with the scripts in `tools\`.
3. Use `Agentic/Reference/runtime-reference.md` for command-line, scene, physics, and key-binding reference.

For AI agents:
1. Read `AGENTS.md`.
2. Read `Agentic/SessionState.md`.
3. Load only the skill file needed for the current task.

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
| Renderer, shader, texture, screenshot behavior | `tools\validate_renderers.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Hot path or allocation-sensitive work | `tools\validate_perf.bat` |
| Broad or uncertain scope | `tools\validate_full.bat` |
| Unsure at the PR gate | `tools\agent_validate.bat` |

Physics baseline changes are behavior changes. If a physics CSV or SkullScope
baseline is intentionally refreshed, update it from the final Debug executable
and committed scene/config state, then rerun `tools\validate_physics.bat` so the
new baseline is proven byte-exact. `tools\update_baselines.bat` is for visual
and perf artifacts, not physics baselines.

You can also run any targeted subset with one line:

```bat
tools\validate_select.bat fast
tools\validate_select.bat physics renderers
tools\validate_select.bat format build-profile
```

## Common Launches

```bat
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --suite SkullbonezData\scenes\render_tests.suite --vsync off
Profile\SKULLBONEZ_CORE.exe --fixed-step --scene SkullbonezData\scenes\perf_test.scene --vsync off

REM Frozen legacy parity/reference paths while they remain in tree:
Profile\SKULLBONEZ_CORE.exe --renderer gl
Profile\SKULLBONEZ_CORE.exe --renderer dx11 --scene SkullbonezData\scenes\water_ball_test.scene
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
