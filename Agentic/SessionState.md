# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `codex/contact-audio-perceptual-model` in worktree `C:\SkullbonezCore`. |
| Active objective | Replace hot-path migration abstractions with data-oriented physics side effects, add repo rules against hot-path polymorphism/inheritance drift, then complete `Agentic/Plans/To_Eval/contact-audio-perceptual-model-plan.md`. |
| Last documentation milestone | Updated `Agentic/Plans/To_Eval/contact-audio-perceptual-model-plan.md` with the classifier/reducer flash-mode slice, validation evidence, and remaining partial phase work. |
| Last source milestone | Contact audio now collapses candidates by contact patch plus material pair, rejects low-attack roll/settle/propagated rows with explicit reasons, records global/per-body burst-budget drops, and exposes Sound-tab flash modes for emitted/candidate/rejected decision review. |
| Pending work | Finish the remaining contact-audio plan phases: 200-box baseline counts, explicit heavy-landing/support reporting, SkullScope contact-audio queries, material-layer polish, and acceptance-scene proof. The physics/GameModel authority bridge still needs later store-authority and render-projection cleanup, but the clear physics inheritance violation is fixed. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No blocker for the current contact-audio slice. `tools\validate_physics_deep.bat` had a prior known stacking signature mismatch on this branch; treat deep physics evidence separately if the next slice touches SkullScope/deep baselines. |
| Validation | 2026-07-03 contact-audio classifier/reducer flash-mode slice: one pre-commit rubber-duck review found no blocking issues and prompted the now-fixed `burst_budget` decision record for candidates skipped by the global cap. `git diff --check` passed. `python tools\check_runtime_boundaries.py --repo .` passed with 0 errors. `tools\validate_build.bat Profile` passed with log `TestOutput\agent_build_profile_contact_audio_classifier_flash_mode.log`; 0 warnings and 0 errors. `Profile\SKULLBONEZ_CORE.exe --contact-audio-smoke` passed with log `TestOutput\agent_contact_audio_smoke_classifier_flash_mode.log`, reporting one submitted voice from one event. `tools\validate_full.bat` passed with log `TestOutput\agent_validate_full_contact_audio_classifier_flash_mode.log`; Profile and Debug builds had 0 warnings and 0 errors, DX12 InfoQueue reported 0 validation errors, DX12 screenshots matched committed baselines, standalone physics/runtime handle smoke passed, and `physics_regression_solver.csv` matched byte-exactly. |

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
