# SkullbonezCore Session State

Keep this file short. Put detailed history in a task-specific plan only when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `main` in worktree `C:\SkullbonezCore` |
| Last commit | DX12 renderer planning docs aligned on `main`; DX12 is the official production renderer. |
| Pending work | None currently tracked. |
| Uncommitted changes | None expected after requested commit/push; check `git status --short`. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git for validation.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the validation scripts, which call it where needed.
- Repository validation scripts are pre-commit/PR gates, not as-you-go checks. During implementation, run only targeted builds, launches, focused tests, or inspections that answer the current fix question.
- DX12 is the official production renderer. OpenGL and DX11 are retained as legacy parity/reference backends while they remain in tree; use them to catch visual drift, not as long-term product targets.
- Feature-branch commits and normal pushes are allowed without asking. Do not commit or push directly on `main` without explicit confirmation.
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
| Bullet sweep regression | Recent | Wall/object/terrain high-speed sweep scenes and Debug collision-time CSV baselines are wired into `tools\validate_physics.bat`. |
| Terrain shared row pipeline | Done | Implemented and documented in `Agentic/Plans/Done/physics-terrain-shared-row-pipeline-plan.md`. |
| SIMD/SSE math optimization pass | Pending | Reported large wins in matrix and render markers; verify current code before continuing. |
| Camera tween reflection fix | Recent | Reflection pass should use the exact render camera state during camera transitions. |
| DX12 GPU timer readback | Recent | Non-blocking readback restored when `pipeline_sync` is off. |
| Cinematic volumetric rendering | Recent | Use `Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\cinematic_volumetric.scene --cinematic --hold` for interactive look-dev. |

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
