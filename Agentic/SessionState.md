# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/contact-audio-perceptual-model` in worktree `C:\SkullbonezCore`. |
| Active objective | Replace hot-path migration abstractions with data-oriented physics side effects, add repo rules against hot-path polymorphism/inheritance drift, then complete `Agentic/Plans/To_Eval/contact-audio-perceptual-model-plan.md`. |
| Last documentation milestone | Updated `Agentic/Plans/To_Eval/contact-audio-perceptual-model-plan.md` and `Agentic/Reference/physics-query-reference.md` with the contact-audio SkullScope query slice, validation evidence, and remaining partial Phase 4 work. |
| Last source milestone | `tools\physics_query.py` now exposes bounded contact-audio summary, events, rejections, body, and timeline queries over existing SkullScope verdict events; the query regression baseline covers the new command families. |
| Pending work | Finish the remaining contact-audio plan phases: 200-box baseline counts, explicit heavy-landing/support reporting, optional raw-fact/merge aggregate SkullScope rows, material-layer polish, and acceptance-scene proof. The physics/GameModel authority bridge still needs later store-authority and render-projection cleanup, but the clear physics inheritance violation is fixed. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No blocker for the current contact-audio query slice. The prior known stacking signature mismatch was refreshed from final Debug artifacts and `tools\validate_physics_deep.bat` now passes. |
| Validation | 2026-07-03 contact-audio SkullScope query slice: `python -m py_compile tools\physics_query.py tools\check_physics_query_regression.py` passed. `python tools\check_physics_query_regression.py` passed with exact `physics_query_varied.json` match after intentional baseline refresh. `tools\validate_fast.bat` passed with log `TestOutput\agent_validate_fast_contact_audio_queries.log`; Debug build had 0 warnings and 0 errors. `tools\validate_physics_deep.bat` passed with log `TestOutput\agent_validate_physics_deep_contact_audio_queries.log`; broad CSVs matched byte-exact, known issue signatures matched, SkullScope query regression matched, and Debug build had 0 warnings and 0 errors. |

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
