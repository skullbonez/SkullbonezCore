# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-24th-june-refactor` in worktree `C:\SkullbonezCore` |
| Last committed milestone | Runtime run decomposition is committed through the Phase 4C scene-caller slice on `nightrunner-24th-june-refactor`: generated/authored scene setup lives in scene helpers, public physics handle/API scaffolding is in place, `PhysicsEngine` owns the existing `PhysicsScene`, and authored scene sleep/joint setup routes direct physics mutations through `PhysicsEngine`. |
| Active objective | Implement `Agentic/Plans/runtime-run-decomposition-plan.md` with the repo-local orchestrator skill. Validate and commit per phase before advancing. |
| Pending work | Runtime run decomposition is active. Do not delete legacy replay exporters. Runtime input follow-up remains: Launcher mode and Free/Inspect camera mode do not need right-click hold for camera rotation; other modes should require right-click hold, and every mode should still offer right-click rotate when appropriate. |
| Blockers | None known. |
| Orchestrator policy | The old `Agentic/Orchestrator` JSON policy/queue/state-machine path was removed; use the `orchestrator` skill instead. |
| Worktree expectation | Do not assume cleanliness; run `git status --short --branch` before editing or committing. |
| Validation | Runtime run decomposition Phase 0 is documentation-only: no validation required. Phase 1 render-input plumbing is covered by `tools\validate_fast.bat` and direct `python tools\validate_project_filters.py`; logs are in `Agentic\Logs\runtime_run_phase1_validate_fast.log` and `Agentic\Logs\runtime_run_phase1_validate_project_filters.log`. Phase 2A render pass contract promotion is covered by `tools\validate_fast.bat`, direct `python tools\validate_project_filters.py`, and `tools\validate_dx12_renderer.bat` using the freshly built Profile binary; logs are in `Agentic\Logs\runtime_run_phase2a_validate_fast.log`, `Agentic\Logs\runtime_run_phase2a_validate_project_filters.log`, and `Agentic\Logs\runtime_run_phase2a_validate_dx12_renderer_prebuilt.log`. Phase 2B RuntimeRenderer ownership is covered by `tools\validate_full.bat`; log is in `Agentic\Logs\runtime_run_phase2b_validate_full.log`. Phase 2C render dependency narrowing is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, and final `tools\validate_full.bat`; logs are in `TestOutput\validation\phase2c_profile_build.log`, `TestOutput\validation\phase2c_project_filters.log`, `TestOutput\validation\phase2c_validate_format.log`, and `TestOutput\validation\phase2c_validate_full.log`. Phase 3A scene lifecycle decision extraction is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, and final `tools\validate_full.bat`; logs are in `TestOutput\validation\phase3a_profile_build.log`, `TestOutput\validation\phase3a_project_filters.log`, `TestOutput\validation\phase3a_validate_format.log`, and `TestOutput\validation\phase3a_validate_full.log`. Phase 3B generated scene setup extraction is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, `tools\validate_fast.bat`, and final `tools\validate_full.bat`; logs are in `TestOutput\validation\phase3b_profile_build.log`, `TestOutput\validation\phase3b_project_filters.log`, `TestOutput\validation\phase3b_format.log`, `TestOutput\validation\phase3b_validate_fast.log`, and `TestOutput\validation\phase3b_validate_full.log`. Phase 3C authored scene setup extraction is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, `tools\validate_fast.bat`, and final `tools\validate_full.bat`; logs are in `TestOutput\validation\phase3c_profile_build.log`, `TestOutput\validation\phase3c_project_filters.log`, `TestOutput\validation\phase3c_format.log`, `TestOutput\validation\phase3c_validate_fast.log`, and `TestOutput\validation\phase3c_validate_full.log`. Phase 4A public physics API scaffolding is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, and final `tools\validate_fast.bat`; logs are in `TestOutput\validation\phase4a_profile_build.log`, `TestOutput\validation\phase4a_project_filters.log`, `TestOutput\validation\phase4a_format.log`, and `TestOutput\validation\phase4a_validate_fast.log`. Phase 4B PhysicsEngine facade introduction is covered by focused `tools\validate_build.bat Profile`, direct `python tools\validate_project_filters.py`, `tools\validate_format.bat`, `tools\validate_fast.bat`, and final `tools\validate_physics.bat`; logs are in `TestOutput\validation\phase4b_profile_build.log`, `TestOutput\validation\phase4b_project_filters.log`, `TestOutput\validation\phase4b_format.log`, `TestOutput\validation\phase4b_validate_fast.log`, and `TestOutput\validation\phase4b_validate_physics.log`. Phase 4C scene-caller physics command routing is covered by focused `tools\validate_build.bat Profile`, `tools\validate_format.bat`, `tools\validate_physics.bat`, and final `tools\validate_full.bat`; logs are in `TestOutput\validation\phase4c_scene_profile_build.log`, `TestOutput\validation\phase4c_scene_format.log`, `TestOutput\validation\phase4c_scene_validate_physics.log`, and `TestOutput\validation\phase4c_scene_validate_full.log`. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git
  for build and validation work.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the
  validation scripts, which call it where needed.
- Repository validation scripts are pre-commit/PR gates, not as-you-go checks.
  During implementation, run only targeted builds, launches, focused tests, or
  inspections that answer the current fix question.
- DX12 is the only runtime renderer. OpenGL and DX11 backend files and shader
  families are retired; final parity evidence is archived under
  `Agentic/Reports/2026-06-15/final-legacy-renderer-parity/`.
- Do not commit, push, merge, or submit PRs on `main` without explicit user
  confirmation.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you
  launched.
- Time user-requested work and report elapsed wall-clock time in the final
  answer or handoff.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md`.
- Runtime run decomposition Phase 2C removed direct `Run&` ownership from
  `RuntimeRenderer` and render passes, but `RuntimeRenderHost` is intentionally
  still a broad bridge over Run-owned editor, replay, scene/UI, physics-debug,
  timing, world, and model state. Later phases should narrow those services
  instead of treating the host as a final renderer boundary.
- Runtime run decomposition Phase 4C now routes authored scene setup and ragdoll
  joint/sleep commands through `Physics::PhysicsEngine`; ragdoll body creation
  still uses `GameModelCollection` until model construction itself is lifted out
  of collection ownership.
- Runtime run decomposition Phase 4C launcher migration added
  `PhysicsEngine::ApplyBodyImpulse()` and routes launcher laser/projectile wake
  operations through the physics facade while preserving collection-backed body
  storage.
- Runtime run decomposition Phase 4C editor-tool migration routes mouse-pickup
  impulses, gizmo motion wakeups, and placement wake/sleep commands through
  `PhysicsEngine`; editor shape/pose mutation still happens on `GameModel`
  during this compatibility slice.
- Runtime run decomposition Phase 4C frame migration routes replay-applied
  editor transform wakeups and restore-target physics stepping through
  `PhysicsEngine`, leaving broader replay snapshot ownership for the replay
  runtime phase.
- Runtime run decomposition Phase 4C replay migration routes replay sample
  restore, solver snapshot capture/restore, prediction stepping, velocity-edit
  wakeups, and prediction diagnostics suppression through `PhysicsEngine`; a
  dedicated `ReplayRuntime` remains the Phase 5 owner boundary.
- Runtime run decomposition Phase 4C scene initial impulse migration added
  `PhysicsEngine::SetPendingBodyImpulse()` and routes authored/generated scene
  initial force setup through the physics facade while preserving model creation
  order and collection-backed body storage.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| DX12-only renderer retirement | Done | Archived in `Agentic/Plans/Done/dx12-only-renderer-retirement-plan.md`; DX12 is the production renderer and DX12-only validation is the safety net. |
| Render resource lifetime | Done | Archived in `Agentic/Plans/Done/render-resource-lifetime-plan.md`; current lifecycle phases, release hooks, source records, and device-lost diagnostics are in place. |
| Render pipeline extraction | Done | Archived in `Agentic/Plans/Done/render-pipeline-extraction-plan.md`; pass bodies and resources live outside the former monolithic frame renderer. |
| Shader architecture cleanup | Done | Archived in `Agentic/Plans/Done/shader-architecture-cleanup-plan.md`; object material contracts, typed upload paths, shader contract checking, and the `t4` material table landed on `main`. |
| DX12 descriptor/upload/root-signature cleanup | Done | Archived in `Agentic/Plans/Done/dx12-descriptor-upload-root-signature-plan.md`; ordinary raster ABI is `b0 + t0..t4` with named descriptor/upload accounting. |
| Material system v1 object slice | Done | Archived in `Agentic/Plans/Done/material-system-v1-implementation-plan.md`; named material assets and terrain/water/post unification should be new focused work. |
| Agent documentation alignment | Done | Archived in `Agentic/Plans/Done/agent-docs-alignment-plan.md`; startup, dirty-worktree, scoped instruction, review, and agent-orchestration guidance are now centralized. |
| Agent orchestrator skill | Active | The old JSON/Python control plane is retired. `Agentic/Skills/orchestrator/SKILL.md` is the active coordinator contract for plan queues, fresh worker agents, rubber-duck review agents, validation, commits, pushes, and handoffs. |
| Runtime interaction controller | Done | Archived in `Agentic/Plans/Done/runtime-interaction-controller-plan.md`; central workspace/owner policy now coordinates Inspect/Edit/Replay/Launcher/Manipulator transitions, stepping, and stale interaction cleanup. |
| Physics playground refactor and prefix cleanup | Done | Completed on `major-refactor` through `Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md`; source prefix cleanup, module folders, runtime/physics/render/editor boundaries, dead-code audit, and full comment pass are committed and pushed. |
| Catto physics solver finalisation | Done | Persistent Catto rows, terrain shared row pipeline, SkullScope query support, and updated deterministic baselines are on `main`. |
| Post-PR73 roadmap follow-up | Done | Runtime extraction review fixes and validation report are recorded in `Agentic/Reports/2026-06-16/post-pr73-roadmap-review-fixes/validation-report.md`. |
| Water rendering cleanup | Active plan | `Agentic/Plans/water-rendering-cleanup-plan.md` remains the focused renderer plan for water material/intersection quality work. |
| Render graph completion | Active plan | `Agentic/Plans/dx12-render-graph-completion-plan.md` remains the focused DX12 resource-state ownership plan. |
| Architecture pass follow-up | Active reference | `Agentic/Plans/architecture_pass_2026-06-02.md` remains the broad checkpoint for runtime, physics data, asset, parser, and render graph boundaries. |
| Authoritative replay rollback | Done | Archived in `Agentic/Plans/Done/authoritative-replay-rollback-plan.md`; legacy replay paths were intentionally retained. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Mitigated but not fully solved; continue through `Agentic/Plans/water-rendering-cleanup-plan.md`. |

Additional bug notes live in `Agentic/Bugs.md`.

## Validation Map

Use `AGENTS.md` as the source of truth. These are targeted pre-commit/PR gates,
not routine iteration steps.

| Change | Validation |
|--------|------------|
| Documentation-only | No validation required |
| Small non-render code refactor | `tools\validate_fast.bat` |
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_dx12_renderer.bat` |
| DX12 renderer gate or validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` |
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
