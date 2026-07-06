# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-5th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Post-duck correction on `nightrunner-5th-july` is implemented and validated: replay prediction reserve units, F6 overlay proof, and replay branch-restore proof now match the branch claims. |
| Last documentation milestone | Added a post-duck evidence correction section to `Agentic/Plans/Done/runtime-static-allocation-policy-plan.md` with byte-cap proof and tracked interaction-proof commands. |
| Last source/data milestone | Replay prediction registered growth now requests bytes under a 256 MiB hard cap, including aggregate frame-payload bytes for prediction debug contacts. Interaction automation can inject one bounded key press, scrub the replay solver track through its UI hitbox, click the Branch button, assert memory-overlay/replay live-edge state, and run tracked scripts for F6 overlay and branch-restore proof. |
| Pending work | No remaining source work for the post-duck correction. Follow-up architecture items remain in their own active plans and references. |
| Concurrent work warning | No concurrent dirty source work observed at the last status check. Still run `git status --short --branch` before editing. |
| Blockers | No validation blocker. Open scope remains tracked in the plan rows above. |
| Validation | Post-duck targeted proof from final Profile build: `tools\validate_build.bat Profile` 0 warnings/errors; `memory_overlay_f6_toggle.json` report `ok=1`, asserted `memoryOverlayEnabled=true`, screenshot captured, allocation guard PASS; `replay_branch_restore_live_edge.json` report `ok=1`, solver restore hash matched, asserted active Solver track, `replayHistoricalSamplePaused=false`, and solver track at present; replay prediction click and `prediction_ragdoll_wall_200_predict.json` passed under `--allocation-guard gameplay` with `replay_prediction_working_set` hard cap `268435456` bytes and no reserve policy violations. Final gates passed: `tools\validate_perf.bat` clean allocation-guard evidence, `policy_violations=0`, no perf regressions, Profile/Debug 0 warnings/errors; `tools\validate_full.bat` passed project filters, runtime boundaries, formatting, DX12 InfoQueue 0/errors and screenshot baselines, and byte-exact `physics_regression_solver.csv`. |

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
| Physics/GameModel authority | Active follow-up | `Agentic/Plans/To_Eval/physics-game-model-authority-plan.md`; 2026-07-05 endgame slice deleted the remaining physics-side GameModel compatibility import/writeback/API surface, removed render-host concrete collection reliance from production render surfaces, tightened tombstone guardrails, and moved the focused endgame plan to Done. |
| Runtime static allocation policy | Complete; post-duck correction validated | Plan moved to `Agentic/Plans/Done/runtime-static-allocation-policy-plan.md`; post-duck source/data update converts replay prediction growth accounting to bytes, adds tracked F6/branch interaction proofs, and passed targeted proof plus `validate_perf`/`validate_full`. |
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
