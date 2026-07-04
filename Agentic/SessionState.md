# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-4th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Finish the physics/GameModel authority migration with store-owned physics state, handle-keyed commands, deterministic standalone views, and guardrails against contrived migration artifacts. |
| Last documentation milestone | Completed and moved the contact-audio perceptual model plan to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`. |
| Last source/data milestone | Added store-owned standalone sphere/sphere and sphere/box contact rows: `PhysicsStandaloneWorld` rebuilds immutable contact views from `ColliderStore` records after `Step()`, and the standalone smoke now requires `contacts=2` with a stable contact hash. |
| Pending work | Physics standalone still needs later shape-pair contact coverage if required, islands/sleep views, remaining update-desc/state cleanup, compatibility assertion/guardrail shrink decisions, and runtime adapter migration. Contact-audio one-shot impact work remains complete; separate roll/slide loops are future work. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No active blocker for the default `aaa_ragdoll_sunset_showcase` cinematic/water render path or the contact-audio perceptual model plan. |
| Validation | 2026-07-04 sphere/box contact slice: Debug build passed with 0 warnings / 0 errors in 6.3s (`TestOutput\agent_build_debug_sphere_box_contacts.log`); standalone smoke passed with `contacts=2`, `contact_hash=0x5DBDF5257E90EA9B`, runtime mirror pass, and deterministic hash `0x82F2DF48D6E4E21B` (`TestOutput\agent_physics_standalone_smoke_sphere_box_contacts_report.txt`); repeated smoke reports were byte-identical (`TestOutput\agent_physics_standalone_smoke_sphere_box_repeat1.txt`, `TestOutput\agent_physics_standalone_smoke_sphere_box_repeat2.txt`); `python tools\check_runtime_boundaries.py --repo .` passed with 0 errors in 7.0s (`TestOutput\agent_runtime_boundaries_sphere_box_contacts.log`); `tools\validate_physics.bat` passed in 19.0s with byte-exact `physics_regression_solver.csv` (`TestOutput\agent_validate_physics_sphere_box_contacts.log`). |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
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
| Physics/GameModel authority | Active follow-up | `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md`; the persistent solver event/writeback boundary is narrowed, but strict store authority and wake-handle ownership remain future work. |
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
