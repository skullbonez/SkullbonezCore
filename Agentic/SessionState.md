# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/contact-audio-perceptual-model` in worktree `C:\SkullbonezCore`. |
| Active objective | Replace hot-path migration abstractions with data-oriented physics side effects, add repo rules against hot-path polymorphism/inheritance drift, and keep completed contact-audio evidence reviewable on the branch. |
| Last documentation milestone | Completed and moved the contact-audio perceptual model plan to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`. |
| Last source/data milestone | Added material-aware contact-audio recipes and the deterministic `contact_audio_material_lab.scene.json` acceptance fixture. |
| Pending work | Contact-audio plan work is complete for one-shot impact sounds; future separate looped roll/slide sound support and any higher-quality licensed impact sample are explicit future work, not hidden blockers. The physics/GameModel authority bridge still needs later store-authority and render-projection cleanup, but the clear physics inheritance violation is fixed. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No active blocker for the default `aaa_ragdoll_sunset_showcase` cinematic/water render path or the contact-audio perceptual model plan. |
| Validation | Final branch-head gate after the inheritance/render cleanup commits: `tools\validate_fast.bat` passed format, project filters, runtime boundaries, and Profile/Debug builds (`TestOutput\agent_validate_fast_inheritance_guard.log`); `tools\validate_full.bat` passed with metadata/runtime-boundary checks clean, Profile and Debug builds at 0 warnings / 0 errors, DX12 InfoQueue 0 errors with screenshots matching baselines, and `physics_regression_solver.csv` byte-exact (`TestOutput\agent_validate_full_inheritance_guard.log`). Contact-audio material/acceptance slice: `Debug\SKULLBONEZ_CORE.exe --contact-audio-smoke` loaded 6 sound sets / 38 samples and submitted 1 voice; material lab, roll, and showcase SkullScope traces completed; `tools\validate_full.bat` passed (`TestOutput\agent_validate_full_contact_audio_material_acceptance.log`). End-of-slice rubber-duck review flagged missing perf evidence, then `tools\validate_perf.bat` passed absolute budgets and no-regression checks for DX12 and physics_bench (`TestOutput\agent_validate_perf_contact_audio_material_acceptance.log`). 2026-07-04 DX12 graph-barrier reopen fix: Debug build passed (`TestOutput\agent_build_debug_dx12_graph_barrier_reopen.log`); CDB repro `aaa_ragdoll_sunset_showcase` 10-frame default cinematic/water run exited without the prior `COMMAND_LIST_CLOSED` break (`TestOutput\agent_cdb_debug_ragdoll_showcase_10f_after.log`); `tools\validate_dx12_renderer.bat` passed with DX12 InfoQueue 0 errors and screenshots matching baselines (`TestOutput\agent_validate_dx12_renderer_graph_barrier_reopen.log`). Previous contact-audio UI wording slice: `tools\validate_ui.bat` passed (`TestOutput\agent_validate_ui_contact_audio_wording.log`). Previous kind slice: `tools\validate_format.bat`, `tools\validate_build.bat Debug`, `Debug\SKULLBONEZ_CORE.exe --contact-audio-smoke`, `python -m py_compile tools\physics_query.py tools\check_physics_query_regression.py`, `python tools\check_physics_query_regression.py --update`, `python tools\check_physics_query_regression.py`, `tools\validate_fast.bat`, and `tools\validate_physics_deep.bat` all passed. |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- DX12 is the only runtime renderer. OpenGL and DX11 parity evidence is
  historical.
- Repository validation scripts are pre-commit/PR gates, not routine iteration
  commands.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md` unless the user asks to bypass it.
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
