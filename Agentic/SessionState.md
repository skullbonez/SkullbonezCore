# SkullbonezCore Session State

Keep this file short. Put detailed history in a task-specific plan only when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/cinematic-renderer` in worktree `C:\SkullbonezCore` |
| Last commit | Branch point from current main before cinematic renderer integration. |
| Pending work | Cinematic renderer merge, runtime Cine controls, terrain relief default-off polish, targeted pre-PR validation, then feature-branch commit/push as needed. |
| Uncommitted changes | See `git status --short`; feature-branch commits, main-branch commits, and normal pushes are allowed when scoped to the requested work. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git for validation.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the validation scripts, which call it where needed.
- Repository validation scripts are pre-commit/PR gates, not as-you-go checks. During implementation, run only targeted builds, launches, focused tests, or inspections that answer the current fix question.
- Feature-branch commits, main-branch commits, and normal pushes are allowed when scoped to the requested work. Do not force-push, rebase, or rewrite git history.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you launched.
- Time large work: record wall-clock start/end and report elapsed time for pipeline runs, multi-file features, and debugging sessions.
- Management demo handoff: `Agentic/Plans/agent-loop/autonomous-agentic-loop-demo-handoff.md`.
- Broadphase demo design note: `Agentic/Plans/agent-loop/broadphase-plan.md`.
- For the management demo loop, run only `Agentic\Plans\agent-loop\run_perf_demo_visible.bat`; use `--wait` from Codex when the agent must wait for completion, then inspect `Profile\gl_perf.json` and physics artifacts.
- Broadphase shadow files live in `Agentic\Plans\agent-loop\shadow-broadphase\SkullbonezSource\` for the recorded restore loop.
- Do not run screenshot, renderer, full, or general build-pipeline validation during the recorded loop unless explicitly requested.
- The demo regression step runs with `--broadphase-visualizer`; the OpenGL perf step intentionally does not.
- When manually inspecting the visible demo, press `G` after launch to toggle the broadphase visualizer.
- The recorded demo should include honest staged failure beats: an initial compile failure during broadphase reintegration, then a correct but inefficient cache pass, then an optimized cached broadphase recovery.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Catto physics solver finalisation | Recent | Object/object response now belongs to persistent Catto rows with pipeline visualizer and SkullScope `pipeline` query support. User-approved physics CSV and SkullScope query baselines were updated; `tools\validate_full.bat` passed. |
| Bullet sweep regression | In progress | Adding wall/object/terrain high-speed sweep scenes with Debug collision-time CSV baselines. |
| Terrain shared row pipeline | In progress | New plan: `Agentic/Plans/physics-terrain-shared-row-pipeline-plan.md`. |
| SIMD/SSE math optimization pass | Pending | Reported large wins in matrix and render markers; verify current code before continuing. |
| Camera tween reflection fix | Recent | Reflection pass should use the exact render camera state during camera transitions. |
| DX12 GPU timer readback | Recent | Non-blocking readback restored when `pipeline_sync` is off. |
| Cinematic volumetric rendering | Integrating for PR | Main worktree branch `codex/cinematic-renderer`. Use `Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\cinematic_volumetric.scene --cinematic --hold` for interactive look-dev. The implementation includes HDR scene FBOs, shader-readable depth, procedural sky/cloud shader, half-res volumetric light, tonemap/bloom/fog/god-ray composite, crisp shader-sampled red/yellow ball/box pattern, terrain basin relief with default relief `0.0`, `--cinematic` / `--hold`, Cine UI feature toggles/sliders plus master runtime toggle, and scene-file `cinematic_*` overrides. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Open |

Additional bug notes live in `Agentic/Bugs.md`.

## Validation Map

Use `AGENTS.md` as the source of truth. These are targeted pre-commit/PR gates,
not routine iteration steps. Common cases:

| Change | Validation |
|--------|------------|
| Documentation-only | No validation required |
| Small non-render code refactor | `tools\validate_fast.bat` |
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_renderers.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Performance-sensitive hot path | `tools\validate_perf.bat` |
| Broad or uncertain scope | `tools\validate_full.bat` |

## Key Paths

| Purpose | Path |
|---------|------|
| Source | `SkullbonezSource/` |
| Scenes | `SkullbonezData/scenes/` |
| Shaders | `SkullbonezData/shaders/` |
| Baselines | `TestOutput/baselines/` |
| Validation scripts | `tools/` |
| Runtime reference | `Agentic/Reference/runtime-reference.md` |
| Physics overview | `Agentic/Reference/physics-overview.md` |
