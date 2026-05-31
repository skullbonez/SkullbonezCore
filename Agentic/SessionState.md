# SkullbonezCore Session State

Keep this file short. Put detailed history in a task-specific plan only when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `main` |
| Last commit | `c7ab05b` - docs: reduce agent context cost |
| Pending work | Lean-up deletion pass awaiting user review; do not commit yet. |
| Uncommitted changes | See `git status --short`; use `tools\find_git.bat` first if Git is not on PATH. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git for validation.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the validation scripts, which call it where needed.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you launched.
- Time large work: record wall-clock start/end and report elapsed time for pipeline runs, multi-file features, and debugging sessions.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Natural contact solver plan | Pending | See `Agentic/Plans/physics-natural-contact-solver-plan.md`. |
| SIMD/SSE math optimization pass | Pending | Reported large wins in matrix and render markers; verify current code before continuing. |
| Camera tween reflection fix | Recent | Reflection pass should use the exact render camera state during camera transitions. |
| DX12 GPU timer readback | Recent | Non-blocking readback restored when `pipeline_sync` is off. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Open |

Additional bug notes live in `Agentic/Bugs.md`.

## Validation Map

Use `AGENTS.md` as the source of truth. Common cases:

| Change | Validation |
|--------|------------|
| Docs, Agentic docs, small non-render refactor | `tools\validate_fast.bat` |
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
