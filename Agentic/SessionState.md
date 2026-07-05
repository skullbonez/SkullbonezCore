# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/physics-store-authority` in worktree `C:\SkullbonezCore`. |
| Active objective | Finish the physics/GameModel authority migration with store-owned physics state, handle-keyed commands, deterministic standalone views, and guardrails against contrived migration artifacts. |
| Last documentation milestone | Completed and moved the contact-audio perceptual model plan to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`. |
| Last source/data milestone | Collider topology repair now fails closed instead of recapturing shape/material facts from `GameModel`; `CaptureAuthoredColliderDesc`, `UpdateColliderStoreFromModel`, `GameModelCollection::RefreshPhysicsColliders`, and `PhysicsModelAccess::RefreshPhysicsColliders` are deleted from source. |
| Pending work | Continue scene/entity metadata cleanup, move replay identity from the temporary collection sidecar into stable scene/entity metadata, clear remaining model-index fallbacks that are not pure UI/presentation hints, and run a final completion audit for `GameModel` body/collider readers. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No active blocker for the default `aaa_ragdoll_sunset_showcase` cinematic/water render path or the contact-audio perceptual model plan. |
| Validation | 2026-07-05 collider topology fail-closed slice passed: `git diff --check`; `python -m py_compile tools\check_runtime_boundaries.py`; `python tools\check_runtime_boundaries.py --repo .` with 0 errors; CSV parse check; focused Profile build at 0 warnings/errors; touched-source comment audit; `tools\validate_fast.bat`; and `tools\validate_physics.bat` with standalone/runtime physics smoke passing, `collider_refresh=pass`, and byte-exact 20,001-line `physics_regression_solver.csv`. |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
- For the active physics/GameModel authority migration, the user requested
  intermittent `tools\validate_physics.bat` checkpoints on completed source
  slices so determinism regressions are caught before the final checkpoint.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md` unless the user asks to bypass it.
- Rubber-duck review is for major completed plans/checkpoints, explicit user
  requests, or repeated failure loops. Do not run one per small source slice.
- The retired `Agentic/Orchestrator` JSON/Python path should not be revived
  unless explicitly requested. Use the orchestrator skill for plan work.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you
  launched.
- Time user-requested work and report elapsed wall-clock time in the final
  answer or handoff.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Contact-audio perceptual model | Complete | Plan moved to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`; final acceptance evidence covers material lab, rolling quietness, 200-brick/showcase reducer counts, tail quietness, and `validate_full`. |
| Contrived migration artifact cleanup | Complete; final review passed | Plan: `Agentic/Plans/To_Eval/contrived-migration-artifact-removal-plan.md`; kill list: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`; implementation status: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`. |
| Missed plan items index | Active reference | `Agentic/Plans/missed_plan_items.md` summarizes actionable leftovers from mostly completed Done plans. |
| Runtime interaction state-machine hardening | Active plan | `Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md`; avoid while another agent is editing UI/replay/camera/input code. |
| Render graph / backend interface continuation | Active plan | `Agentic/Plans/render-graph-irender-interface-plan.md`; barrier migration is done, but graph callbacks/transients/capability cleanup continue in focused slices. |
| Global service context cleanup | Active plan | `Agentic/Plans/global-service-context-plan.md`; Carmack plans are complete and only provide evidence for current compatibility debt. |
| Physics/GameModel authority | Active follow-up | `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md`; persistent solver, launcher fixed-tree per-release writeback, replay prediction writeback, replay render-pose override boundaries, replay render apply/ghost identity, replay restore identity, runtime replay-id preflight, final normal runtime bulk body mirror, attached-camera live target identity, replay restore physics API authority, body-store model-index command overloads, replay velocity target identity, replay cause/focus identity, replay path/query/prediction target identity, replay probe body-state reads, authored scene orientation conversion, editor reset wake fixed-state, fixed-contact highlight fixed-state, replay prediction ghost render shape/material reads, convex-hull render shape reads, editor/replay shape-scale reads, editor selection frame math, editor selection body/collider identity, editor selected-member frame/overlay handle reads, editor ragdoll transform grouping, ColliderStore refresh replay/body identity, direct append-time collider registration, same-count collider row ownership, descriptor-owned collider import, scene primitive/hull descriptor ownership, append collider descriptor ownership, runtime config collider material ownership, and the deleted GameModel replay-id mirror are narrowed/deleted or store-owned; explicit collider update commands, scene/entity metadata cleanup, remaining non-presentation model-index fallbacks, and final `GameModel` body/collider reader audit remain future work. |
| Runtime static allocation policy | Draft active plan | `Agentic/Plans/runtime-static-allocation-policy-plan.md`; implementation touches hot runtime paths and needs perf-aware validation. |
| Architecture pass follow-up | Active reference | `Agentic/Plans/architecture_pass_2026-06-02.md` remains the broad checkpoint for runtime, physics data, assets, parser, water, and render graph boundaries. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Mitigated but not fully solved; see `Agentic/Plans/missed_plan_items.md` item 5 before starting new water work. |

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
| General DX12 graphics stress or memory-growth investigation | `tools\run_graphics_stress.bat 1` for a bounded probe; `overnight` only when intentionally soaking |
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
