# SkullbonezCore

**A deterministic, DirectX 12 physics engine built from scratch by one person — started in 2005, still sharpening.**

![SkullbonezCore](https://github.com/skullbonez/SkullbonezCore/blob/main/SkullbonezCore.png)

SkullbonezCore is Simon Eschbach's Windows x64 C++17 graphics and physics
engine. It began life in 2005 and has been continuously modernized into a
DX12-first, fixed-step, byte-exact simulation engine with its own renderer,
contact solver, replay system, in-engine UI, and validation tooling — all
first-party. The only third-party code in the shipped runtime is a
single-header JSON parser and an image loader. The physics, the collision
detection, the solver, the render graph, the replay recorder, the UI
widgets, the profiler: handwritten, on purpose.

This is not a wrapper around middleware. It is an engine in the original
sense — and it is held to standards most commercial codebases don't attempt.

## The Numbers

- **Byte-exact deterministic physics.** Every validation run diffs a
  44,401-line simulation CSV against a committed baseline — not "close
  enough," *identical*. Multithreaded determinism is certified by an 18/18
  process matrix across 0, 1, and 4 workers over six scenes.
- **5,000 bodies, 1.3 ms.** The sleeping-heavy 5,000-body witness scene
  simulates at 1.30 ms median physics frame (Profile build); 2,000 fully
  awake bodies run at ~1.9 ms. Sleep islands with support propagation do
  the heavy lifting.
- **Zero heap allocation in steady gameplay.** A global static allocation
  policy — enforced by a checker over the whole source tree — bans runtime
  heap growth. Storage is fixed or preallocated at scene load; pool
  exhaustion fails loudly with owner, capacity, and high-water diagnostics.
- **Zero exceptions.** Engine code uses typed error lanes: fatal invariants,
  recoverable results, and machine-readable probe failures. The source throw
  count is zero and reviews keep it there.
- **Zero warnings at `/W4`. Zero DX12 validation-layer errors.** Both are
  hard gates, not aspirations.
- **337 test cases, 68,634 assertions** in the broad gate, plus per-subsystem
  coverage floors, image-baseline renderer comparison, performance baselines,
  and a frame-exact replay fidelity oracle — the full gate runs in about
  four minutes.

## What's Inside

**Physics.** A staged fixed-step pipeline — force, broadphase (spatial
grid), narrowphase, terrain, persistent-contact solver with warm starting,
sleep controller — operating on dense SoA body/collider stores with
generational handles. Spheres, boxes, and baked convex hulls with authored
mass and inertia; buoyancy and fluid drag; ragdolls and point joints;
mutual gravity; deterministic worker-pool parallelism with fixed scratch
and no allocation inside the tick.

**Rendering.** A DX12 renderer with a render graph that owns pass
scheduling and barrier emission end to end — single execution path, no
hand-written barrier fallbacks. Passes declare their raster state up front
and their PSOs precompile; the frame covers shadow mapping, water with
planar or DXR-raytraced reflections, an HDR cinematic pipeline with
volumetric light shafts and tonemapping, instanced geometry, and debug
overlays. In-process InfoQueue validation runs every launch and must stay
silent.

**Replay.** Deterministic capture, timeline scrubbing, and physics
prediction with causal analysis — why a body ended up where it did, traced
through contact cause trees. Guarded by a frame-exact 200-box visual
fidelity gate that a refactor cannot quietly move.

**Tooling.** A first-party immediate-mode operator UI in the engine; an
optional Dear ImGui docking editor with Tracy profiler integration in
development builds; SkullScope, a queryable physics diagnostics pipeline
(trace once, then ask focused frame/body/contact/island questions instead
of grepping CSVs); a scene/asset system with versioned, deterministically
migrated JSON schemas; and a `tools/` directory of validation gates that
make regressions loud.

## How It's Built

The architecture is boring in the best way. Dependency direction is
enforced and grep-provable: `Core → Maths → Physics/Rendering → Runtime`,
with gameplay content quarantined in its own `Gameplay/` module so the
engine stays an engine. The runtime shell is a composition root that owns
concrete subsystem owners — no god objects, no service locators, no
singletons in the frame path. Cross-system identity flows through one
stable scene-object id; hot paths speak dense rows and typed handles.

Every structural claim above was earned, not asserted: the codebase is
developed under an evidence-based plan ledger (`Agentic/`) in which every
campaign closes with independent adversarial review, recorded validation
output, and byte-exact or image-identical proof that behavior did not
move. Completed plans are deleted; git history is the archive. It is also,
deliberately, a proving ground for AI-agent-driven engineering — the
governance model (startup contracts, validation maps, closure gates) is
designed so that human and machine contributors are held to the same
uncompromising bar.

## Start Here

For humans:
1. Read `FIRST_TIME_SETUP.md` if this is a new machine.
2. Build or validate with the scripts in `tools\`.
3. Use `Agentic/Reference/runtime-reference.md` for command-line, scene,
   physics, and key-binding reference.

For AI agents:
1. Follow the Agent Startup Contract in `AGENTS.md`.
2. Load only the skill, plan, audit, report, or reference file needed for
   the current task.

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

Development builds can enable the ImGui/Tracy editor lane; initialize the
pinned submodules first (`git submodule update --init --recursive`) and
launch with `--dev-ui imgui`. The legacy in-engine UI remains the default.

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
| Engine source | `SkullbonezSource/` (`Core`, `Maths`, `Physics`, `Rendering`, `Runtime`, `Scene`, `World`, `UI`, `Assets`, `Gameplay`) |
| Shaders | `SkullbonezData/shaders/` |
| Style descriptors | `SkullbonezData/styles/` |
| Scenes | `SkullbonezData/scenes/` |
| Baselines | `TestOutput/baselines/` |
| Validation scripts | `tools/` |
| Agent handoff docs | `Agentic/` |

## Notes

- **Platform:** Windows x64, Visual Studio 2022 (v143), C++17. DirectX 12 is
  the only runtime renderer; the engine's GL/DX11 heritage has been retired
  with archived parity evidence.
- **Third-party:** runtime — nlohmann JSON, stb image; development/test
  only — doctest, Dear ImGui (docking), Tracy. Everything else is
  first-party. See `THIRD_PARTY_NOTICES.md`.
- **Determinism scope:** byte-exactness is certified per pinned
  toolchain/content envelope and across 0/1/4 workers; cross-platform and
  rollback determinism are not claimed.

## More Detail

Long-form reference lives outside this file to keep first-read context small:
- `Agentic/Reference/runtime-reference.md`
- `Agentic/Reference/physics-overview.md`
- `Agentic/Plans/`
- `Agentic/Audits/`

---

*SkullbonezCore — twenty years of one engineer refusing to ship "close
enough."*
