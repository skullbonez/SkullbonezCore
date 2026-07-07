# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-7th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Continue the 2026-07-07 takeover cleanup after fable-02 phase 4 L1 discovery; all five authoritative CSVs were drained earlier, and follow-up blocker-plan work remains. |
| Last documentation milestone | Fable-02 phase 4 L1 singleton lifetime discovery complete: construction sites and teardown risks are recorded for `EngineConfig`, `WorkerPool`, `Window`, `Profiler`, and frozen `LockOrderValidator`; no `TextureCollection`/`CameraCollection`/`SkyBox` hits remain. |
| Last source/data milestone | Fable-02 phase 2 made a comment-only source edit in `SkullbonezSource\Core\Config.h`; fable-01 phase 4 remains the last behavior/test milestone with real PhysicsEngine determinism coverage. Current authoritative blocker-ledger totals remain 131 done and 29 blocked. |
| Pending work | Continue fable-02 L2 singleton demotion/freezing and the remaining fable directory follow-ups, while addressing the remaining 29 blocked rows in `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`. |
| Concurrent work warning | No unrelated dirty files were present before the fable-01 phase 0 slice. Still run `git status --short --branch` before editing. |
| Blockers | `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; Plan03 5 blocked, Plan05 9 blocked, Plan02 11 blocked, Plan01 4 blocked, Plan04 0 blocked. |
| Validation | Latest source gates passed on 2026-07-07 for fable-01 phase 4: `tools\validate_tests.bat` (5.203s, 42 doctest cases and 527 assertions passed, 0 warnings/errors) and `tools\validate_physics.bat` (15.907s, byte-exact `physics_regression_solver.csv`). Latest comment/docs-only fable-02 phase 2 evidence: `python tools\check_runtime_boundaries.py` passed with 0 errors; no repository validation script was required. |

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
| 2026-07-07 overnight remediation | Remediation pass active | Handoff: `Agentic/Reports/2026-07-07/overnight-run-handoff.md`; blockers: `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; SVC-034, fable-01 phase 0/M1/M2/M3/M4/S1/S2/S3/S4/S5/E1/E2/E3/D1/D2/D3/closure, fable-02 phases 1-3 G1/G2 and phase 4 L1, PHYS-035/fable-03 prediction isolation, fable-04 phase 1, and fable-05 phase 1 are fixed. |
| Repo pack-size cleanup | User decision needed for history rewrite | `Agentic/Temp/` is ignored and empty in the tip tree, and `tools/check_staged_file_sizes.py` blocks new oversized staged files outside approved data roots. Existing pack size was recorded at 542 MiB; shrinking it requires a user-approved `git filter-repo` rewrite of historical `Agentic/Temp` blobs and coordinated re-clone. |

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
| Unit tests only | `tools\validate_tests.bat` |
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
