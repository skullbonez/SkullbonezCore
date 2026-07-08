# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-7th-july-complete-fables` in worktree `C:\Users\sesch\.codex\worktrees\0595\SkullbonezCore`. |
| Active objective | Complete all fable plans, with the user's new request queued to implement the new shadow plan after the current Terrain checkpoint. Fable-03 P3.0, fable-04 L1-L4 and C1-C5, fable-05 P3 SpatialGrid/PhysicsWorld plus GameModelCollection classification/pure topology/legacy camera pose-read/append Lane R conversions, P4.1 scene/style TryLoad entry boundary, one direct missing-camera parser throw removal, and Terrain RAW Lane R cleanup are complete; fable-06 Phase 1, Phase 2 R1/R2/R3, and C1 RuntimeTools cluster are complete; fable-03 P3.1-P3.4, remaining fable-04, remaining fable-05, fable-06 C2-C5/closure, and fable-07 still have open work. |
| Last documentation milestone | `fable_plans/HANDOFF-2026-07-07.md`, `fable_plans/05-unified-error-handling-policy-plan.md`, `fable_plans/05-unified-error-handling-policy-progress.md`, `fable_plans/06-stable-identity-plan.md`, and `fable_plans/06-stable-identity-progress.md` now document the missing-camera parser throw removal, Terrain RAW Lane R cleanup, full fable-06 C1 RuntimeTools completion, lowered ratchets, and remaining work. |
| Last source/data milestone | Fable-05 Terrain RAW Lane R slice is complete: `Terrain::LoadTerrainData` returns `SbResult` for empty, missing, truncated, or failed RAW reads; `Terrain::TryCreateFromHeightMap` only publishes a fully built terrain; startup and scene-load paths report terrain failures through `LastSceneLoadResult`/`LogSceneLoadFailure`. `MAX_SOURCE_THROW_TOKENS` is 263 and `MAX_STORED_MODEL_INDEX_MEMBER_FIELDS` is 26. |
| Pending work | Not all fable plans are complete. Open work remains in fable-03 P3.1-P3.4 worker-job stepping, remaining fable-04 mega-file decomposition, fable-05 remaining P4.1 TextureCollection/AssetSystem/ConvexHullShape/deeper parser conversion plus P5 DX12 and closure, fable-06 C2-C5 plus Z1-Z4 closure, and fable-07 per `fable_plans/HANDOFF-2026-07-07.md`; fable-01, fable-02, fable-08, and fable-09 are checklist complete. Newest user request: implement the new shadow plan after this Terrain checkpoint is committed and pushed. |
| Concurrent work warning | The earlier detached duplicate fable-08 reveal-pacing diff was preserved in stash `codex-preserve-detached-reveal-pacing-duplicate-20260708`; do not drop it unless the user asks. |
| Blockers | `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; Plan03 2 blocked, Plan05 9 blocked, Plan02 11 blocked, Plan01 4 blocked, Plan04 0 blocked. |
| Validation | Latest source gate passed on 2026-07-08 for fable-05 Terrain RAW Lane R cleanup: touched-file comment audit inspected 5 source-bearing/tool files with 0 deferred; `git diff --check` passed; `python tools\check_runtime_boundaries.py --self-test` passed; `python tools\check_runtime_boundaries.py --max-errors 20` passed with 0 errors; and `tools\validate_full.bat` passed in 00:01:04.5130906 with project filters/runtime boundaries at 0 errors, Profile/Debug builds at 0 warnings/errors, DX12 validation errors 0, screenshots matching baselines, and `physics_regression_solver.csv` byte-exact. |

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
| 2026-07-07 overnight remediation | Remediation pass active | Handoff: `Agentic/Reports/2026-07-07/overnight-run-handoff.md`; blockers: `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; SVC-001/SVC-002/SVC-034/SVC-032/SVC-033/SVC-022, fable-01 phase 0/M1/M2/M3/M4/S1/S2/S3/S4/S5/E1/E2/E3/D1/D2/D3/closure, fable-02 phases 1-3 G1/G2/G3 plus phase 4 L1/L2/L3 and closure, PHYS-035/fable-03 prediction isolation, fable-04 phase 1 plus phase 2 L1/L2/L3/L4 and phase 3 C1/C2, fable-05 phase 1 plus P2.1/P2.2/P2.3, P3 SpatialGrid/PhysicsWorld plus GameModelCollection classification/pure topology/camera pose-read conversion, P4.2/P4.3 append Lane R cleanup, P4.1 scene/style TryLoad entry boundary, missing-camera parser throw removal, and Terrain RAW Lane R cleanup, fable-06 phase 1 plus phase 2 R1/R2/R3 plus C1 RuntimeTools cluster, fable-08 phases 0-4 plus closure, and fable-09 phases 0-4 plus closure are fixed. |
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
