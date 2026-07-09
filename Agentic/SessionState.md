# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-9th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Owner redirected active work to `Agentic/Plans/TODO/replay-visuals-prediction-and-memory.md`. Stage 4 is complete: hierarchy topology/reveal, contact-tick child activation, and twice-run prediction trajectory determinism are proven. Next actionable work is Stage 5: prediction worker job. The overall engine cleanup goal remains active and incomplete. |
| Last documentation milestone | 2026-07-09 plan consolidation: `Agentic/Plans/MASTER-PLAN.md` + the seven grouped plans in `Agentic/Plans/TODO/` supersede the retired fable/To_Eval/In_Progress plan files (content preserved in git history). |
| Last source/data milestone | FAC-001 renderer aggregate retirement and the new shadow plan slice are complete: `IRenderBackend.h` is deleted, runtime render wiring uses narrow lifecycle/resource/command/diagnostics/capture/raytracing facets, `shadow_parallel_prep` is no longer hard-disabled, `GameModelRenderer::BuildShadowCasterBatches()` uses bounded count/prefix/fill worker prep, object-shadow bounds use fixed chunk accumulators, and `WorkerPool::BuildChunkRangesNoAlloc()` exposes deterministic caller-owned chunk ranges. Runtime allocation allowlist metadata was also corrected after the Terrain factory cleanup removed `Run.cpp`'s direct Terrain allocation. |
| Pending work | See `Agentic/Plans/MASTER-PLAN.md` for the full inventory. Former fable open work now lives in `Agentic/Plans/TODO/`: fable-03 P3 → replay-prediction-and-memory, fable-04 mega-files → runtime-shell-decomposition, fable-06 C2-C5/Z → physics-authority-and-identity; fable-05 remnants are owned by engine-cleanup plan 04. fable-01/02/08/09 were checklist complete and deleted. |
| Concurrent work warning | The earlier detached duplicate fable-08 reveal-pacing diff was preserved in stash `codex-preserve-detached-reveal-pacing-duplicate-20260708`; do not drop it unless the user asks. |
| Blockers | The 2026-07-07 overnight blocker ledger was absorbed into the TODO plans: PHYS-* rows → `TODO/physics-authority-and-identity.md`, RGRAPH-* rows → `TODO/render-backend-decomposition.md`, RUN-* rows → `TODO/runtime-shell-decomposition.md` (each under "Known hard blockers"). |
| Validation | Latest branch gates passed on 2026-07-08: FAC-001 touched-file comment audit inspected 24 source-bearing paths with 0 deferred, `python tools\check_runtime_boundaries.py --self-test` passed, `python tools\check_runtime_boundaries.py --max-errors 20` passed with 0 errors, targeted `tools\validate_build.bat Profile` passed in 14.80s with 0 warnings/errors, `tools\validate_fast.bat` passed, and `tools\validate_full.bat` passed with DX12 validation errors 0, screenshots matching baselines, and `physics_regression_solver.csv` byte-exact; shadow-prep opt-in audit inspected 3 source-bearing files with 0 deferred, `python tools\check_allocation_policy.py --self-test` passed, allocation policy passed with `allowlist_errors=0`, focused smoke emitted `WorkerBuildBatches`, `WorkerFillBatches`, and `WorkerScanBounds`, focused allocation guard exited 0 with `gameplay_violations=0`, isolated DX12 off/on proof produced byte-identical `off_nowater.bmp` / `on_nowater.bmp` (`5143326` bytes, SHA-256 `DCE3F4FEA913680F9E22BB72CB40539849E41AEF5D1758ACC0CE93B9DE946B61`), `tools\validate_full.bat` passed, and `tools\validate_perf.bat` completed with allocation guard PASS. Final merged-`main` gate `tools\validate_full.bat` rerun passed in 00:01:05.6465549 after resolving the Terrain resource-factory merge fix: project filters 0 errors, runtime boundaries 0 errors, Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` was byte-exact. |
| Current Plan 02 validation | 2026-07-09 Plan 02 Step 3.1 replay snapshot table-drive passed focused `tools\validate_build.bat Profile` (14.70s; 0 warnings/errors), `tools\validate_physics.bat` (26.69s; `VALIDATE_PHYSICS: ALL PASSED`; byte-exact physics baseline), `tools\validate_replay_scrub.bat` (11.03s; `VALIDATE_REPLAY_SCRUB: ALL PASSED`; scrub/restore SkullScope probes passed), and `tools\validate_physics_deep.bat` (84.09s; `VALIDATE_PHYSICS_DEEP: ALL PASSED`). Logs are listed in `engine-cleanup-plans/HANDOFF-2026-07-09-PLAN02-REPLAY-SNAPSHOT.md`. |
| Current Plan 04 validation | 2026-07-09 Plan 04 DX12 fence/readback recoverable result batch converted three strict `RenderDeviceDX12` rows to `SbResult`/neutral-return paths: Step 0.1 inventory rows 106, 107, and 109. Strict anchored source throw statement count is now 7; `SB_FATAL` macro invocations remain 165. Comment audit inspected 6 source-bearing files with 0 deferred; no subsystem checklist was required because this was a touched-file pass. `tools\validate_dx12_renderer.bat` passed in 00:00:40.0 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, and screenshots matching committed baselines. `python tools\check_runtime_boundaries.py` passed in 00:00:18.3 with 0 errors. Logs: `Agentic/Reports/validate_dx12_renderer_plan04_dx12_fence_result_20260709.log`, `Agentic/Reports/check_runtime_boundaries_plan04_dx12_fence_result_20260709.log`. |
| Current replay visuals validation | 2026-07-10 Stage 4.3 is complete. Touched-source comment audit inspected 4 source-bearing/tool files with 0 deferred. Focused checks passed: Python syntax check for `tools/check_replay_prediction_determinism.py`, `tools\validate_format.bat` in 9.92s (`Agentic/Reports/validate_format_replay_stage4_3_20260710.log`), `tools\validate_build.bat Profile` in 9.76s with 0 warnings/errors (`Agentic/Reports/validate_build_profile_replay_stage4_3_20260710.log`), allocation policy self-test in 0.08s, allocation scan in 3.10s (`scanned=296 direct_heap_findings=28 dynamic_stl_member_findings=0 allowlist_errors=0`), and `tools\validate_fast.bat` in 20.41s (`Agentic/Reports/validate_fast_replay_stage4_3_20260710.log`). Required gates passed: `tools\validate_full.bat` in 29.47s (`Agentic/Reports/validate_full_replay_visuals_stage4_3_20260710.log`) with DX12 validation errors 0, screenshots matching baselines, and `physics_regression_solver.csv` byte-exact; `tools\validate_replay_scrub.bat` in 59.86s (`Agentic/Reports/validate_replay_scrub_replay_visuals_stage4_3_20260710.log`) with scrub/restore probes passed and prediction fingerprint `0x395F6E239C82A7B4` matching across two runs (401 records, 56,881 points, 281 active frames); `tools\validate_physics.bat` in 13.49s (`Agentic/Reports/validate_physics_replay_stage4_3_20260710.log`) with `VALIDATE_PHYSICS: ALL PASSED` and byte-exact solver baseline. |

## Active Notes

- Follow the startup contract in `AGENTS.md` before editing: read the required
  docs, run `git status --short --branch`, and protect dirty worktrees.
- Owner steering on 2026-07-09 resolved the Plan 03, Plan 07, Plan 11, and
  FAC-005 decision gates. The record is
  `engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md`; do not reopen
  those ask-human gates unless the owner changes direction again.
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
- Owner requested faster Plan 04 progress on 2026-07-09. Batch compatible
  same-lane/same-subsystem rows that share the same validation gate, while
  keeping high-risk or cross-validator changes separate.
- 2026-07-09 plan consolidation (owner-directed, two passes): all Done/Failed/
  Rejected plans, stale handoffs, and superseded drafts were deleted, then
  `fable_plans/`, `To_Eval/`, and `In_Progress/` were consolidated into seven
  grouped plans under `Agentic/Plans/TODO/`. The authoritative inventory with
  percent-complete is `Agentic/Plans/MASTER-PLAN.md`; review findings are in
  `engine-cleanup-plans/15-review-gaps.md`. Completed plans are deleted, not
  archived — do not recreate Done/, Failed/, Rejected/, To_Eval/, or
  In_Progress/ folders.
- Engine cleanup latest handoff:
  `engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md`.
  The owner redirected the current run to the replay visuals/prediction/memory
  plan on 2026-07-09; resume that plan at Stage 3.1 unless the user redirects
  again.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Plan inventory | Authoritative reference | `Agentic/Plans/MASTER-PLAN.md` lists every remaining plan with percent-complete. Completed plans are deleted, not archived. Owner redirected this run to the replay visuals/prediction/memory plan. |
| Behavioral test depth | In progress (33%) | `Agentic/Plans/TODO/behavioral-test-depth.md`; P1 solver-stage tests and P4 replay snapshot/hash round-trip are complete. Remaining: P2 manifold reduction, P3 parser error paths, P5 injected-bug drill, and P6 sustaining rule. |
| Physics authority + stable identity | Active plan (~55%) | `Agentic/Plans/TODO/physics-authority-and-identity.md`; next big slice needs the physics-owner design decision recorded in its blocker table. |
| Render backend decomposition | Active plan (~50%) | `Agentic/Plans/TODO/render-backend-decomposition.md`; graph-buildout scope dropped per plan 11 owner decision. |
| Interaction state machine | Active plan (~45%) | `Agentic/Plans/TODO/interaction-state-machine.md`; avoid while another agent is editing UI/replay/camera/input code. |
| Replay visuals/prediction/memory/size | Active plan (~62%) | `Agentic/Plans/TODO/replay-visuals-prediction-and-memory.md` - Stage 0 instrumentation/repro, Stage 1 deterministic drawing, Stage 2 rebuild/reveal churn plus visibility/contact-completeness controls, Stage 3.1 TrajectoryStore records/versioning/published prefixes, Stage 3.2 build writers fed by solver-ring appends plus prediction publish events, Stage 3.3 store-only draw reads plus per-frame `targetVisualizer` rebuild/`futureNodes` copy deletion, Stage 3.4 default-off legacy draw fallback, and Stage 4 lock-step hierarchy correctness are complete. Next is Stage 5 prediction worker job. Do not run its stages concurrently in separate sessions. |
| Runtime shell decomposition | Active plan (~25%) | `Agentic/Plans/TODO/runtime-shell-decomposition.md`; owns RunInternal.h retirement and Common.h slimming (the last global-service remnant). |
| 2026-07-09 review gaps | Proposed plan | `engine-cleanup-plans/15-review-gaps.md`: comment-boilerplate cleanup (15.4) remains here; 15.1/15.2/15.3 execute via the TODO plans. |
| 2026-07-07 overnight remediation | Closed into TODO plans | Handoff record: `Agentic/Reports/2026-07-07/overnight-run-handoff.md`. The blocker ledger and fable fix-lists were absorbed into `Agentic/Plans/TODO/` during the 2026-07-09 consolidation; unresolved rows live in each TODO plan's "Known hard blockers" section. |
| Repo pack-size cleanup | User decision needed for history rewrite | `Agentic/Temp/` is ignored and empty in the tip tree, and `tools/check_staged_file_sizes.py` blocks new oversized staged files outside approved data roots. Existing pack size was recorded at 542 MiB; shrinking it requires a user-approved `git filter-repo` rewrite of historical `Agentic/Temp` blobs and coordinated re-clone. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Mitigated but not fully solved; see the git history of `Agentic/Plans/missed_plan_items.md` (item 5; file retired in the 2026-07-09 plan consolidation) before starting new water work. |

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
