# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-23rd-june` in worktree `C:\SkullbonezCore` |
| Last committed milestone | Runtime interaction controller is retained on `nightrunner-23rd-june`; the humanoid ragdoll constraints experiment was rolled back after user review. Simple ragdolls now keep fixed part colors, have a small neck swing stop, and explicit wakes spread through sleeping support piles/tree parts. |
| Active objective | None. `runtime-interaction-controller-plan.md` remains archived; `humanoid-ragdoll-constraints-plan.md` is restored to `Agentic/Plans/` for a future rethink instead of being treated as done. |
| Pending work | Do not delete legacy replay exporters yet. Runtime input follow-up: Launcher mode and Free/Inspect camera mode do not need right-click hold for camera rotation; other modes should require right-click hold, and every mode should still offer right-click rotate when appropriate. |
| Blockers | None known. |
| Orchestrator policy | The old `Agentic/Orchestrator` JSON policy/queue/state-machine path was removed; use the `orchestrator` skill instead. |
| Worktree expectation | Do not assume cleanliness; run `git status --short --branch` before editing or committing. |
| Validation | Runtime interaction controller slice is covered by `tools\validate_fast.bat`, `tools\validate_physics.bat`, and `tools\validate_full.bat`. Ragdoll rollback is covered by `tools\validate_fast.bat` and `tools\validate_full.bat`; logs are in `TestOutput\agent_logs\2026-06-24_validate_fast_revert_ragdoll_constraints.log` and `TestOutput\agent_logs\2026-06-24_validate_full_revert_ragdoll_constraints.log`. Ragdoll head/color/wake fixes are covered by `tools\validate_physics.bat` and `tools\validate_full.bat`; logs are in `Agentic\Logs\ragdoll_wake_validate_physics.log` and `Agentic\Logs\ragdoll_wake_validate_full_final.log`. |

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
