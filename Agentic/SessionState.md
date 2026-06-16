# SkullbonezCore Session State

Keep this file short. Put detailed history in a task-specific plan only when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/engine-cleanup` in worktree `C:\SkullbonezCore` |
| Last committed milestone | Shader architecture, DX12 binding ABI, and DX12-only architecture cleanup landed on `main` through PRs #69 and #72. |
| Pending work | `codex/engine-cleanup` now carries the follow-up shader/object-material completion: expanded instance material payloads, typed object/shadow CBV uploads, `t4` material-table binding, shader contract checker coverage, and graph native-resource transition diagnostics. Final validation passed; commit/push remain. |
| Uncommitted changes | Check `git status` before continuing; source, shader, tool, and handoff-doc changes are ready for commit after the passed validation gate. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git for validation.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the validation scripts, which call it where needed.
- Repository validation scripts are pre-commit/PR gates, not as-you-go checks. During implementation, run only targeted builds, launches, focused tests, or inspections that answer the current fix question.
- DX12 is the only runtime renderer. OpenGL and DX11 backend files and GLSL shader families have been removed; final parity evidence is archived under `Agentic/Reports/2026-06-15/final-legacy-renderer-parity/`.
- Feature-branch commits and normal pushes are allowed without asking. Do not commit or push directly on `main` without explicit confirmation.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you launched.
- Time large work: record wall-clock start/end and report elapsed time for pipeline runs, multi-file features, and debugging sessions.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| DX12-only renderer retirement | Done | Retired GL/DX11 backends and shader families, added DX12-only validation, archived final parity evidence, simplified active render contracts, named DX12 diagnostic resources, and added the future backend portability contract. |
| Render resource lifetime | Done | Phases 1-6 implemented: current-lifetime reference, named lifecycle phases, reflection FBO resize split, shader source-record bridge, reusable release hook table, and DX12 device-lost diagnostics/recovery prep. |
| Render pipeline extraction | Done | `SkullbonezRunRender.cpp` now owns frame orchestration only, while `SkullbonezRunPasses.cpp` and `SkullbonezRunUiTextPass.cpp` own named pass resource creation, release, and render bodies directly. The old central cinematic shader factory/private pass hook layer has been removed. `tools\validate_full.bat` passed with DX12 validation errors 0, matching screenshot baselines, and byte-exact physics CSVs. |
| Shader architecture cleanup | Validated | `codex/engine-cleanup` finishes the requested follow-up: object instance payloads are now `mat4 + material4x3`, object rendering consumes typed `RenderMaterial`, object/shadow binders have typed CBV upload paths, `lit_textured_instanced` samples the `t4` material table, and `validate_shaders.py` checks HLSL uniforms/resources against the JSON contract. |
| DX12 descriptor/upload/root-signature cleanup | Validated | The ordinary raster ABI is now `b0`, fixed SRV slots `t0..t4`, and samplers `s0`, `s1`, `s3`; `t4` is scoped to the object material table. Descriptor indexing and structured-buffer material tables remain future work. |
| DX12-only engine architecture cleanup | Validated | Previous comment pass, root-signature-aware PSO keys, PSO cache-miss events, Debug texture resource slot reflection, and run/pass accessor cleanup are still part of the branch. This follow-up also gives render-graph transitions native resource pointers for graph/live matching. `tools\validate_shaders.bat` and `tools\validate_full.bat` passed on 2026-06-16. |
| Catto physics solver finalisation | Recent | Object/object response now belongs to persistent Catto rows with pipeline visualizer and SkullScope `pipeline` query support. User-approved physics CSV and SkullScope query baselines were updated; `tools\validate_full.bat` passed. |
| Bullet sweep regression | Recent | Wall/object/terrain high-speed sweep scenes and Debug collision-time CSV baselines are wired into `tools\validate_physics.bat`. |
| Terrain shared row pipeline | Done | Implemented and documented in `Agentic/Plans/Done/physics-terrain-shared-row-pipeline-plan.md`. |
| SIMD/SSE math optimization pass | Pending | Reported large wins in matrix and render markers; verify current code before continuing. |
| Camera tween reflection fix | Recent | Reflection pass should use the exact render camera state during camera transitions. |
| DX12 GPU timer readback | Recent | Non-blocking readback restored when `pipeline_sync` is off. |
| Cinematic volumetric rendering | Recent | Use `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\cinematic_volumetric.scene --cinematic --hold` for interactive look-dev. |

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
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_dx12_renderer.bat` |
| DX12 renderer gate or validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` |
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
