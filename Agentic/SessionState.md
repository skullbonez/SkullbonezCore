# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-9th-july` in worktree `C:\SkullbonezCore`. |
| Active objective | Engine cleanup Plan 04 error-handling reconciliation is in progress. Step 0.1 throw-lane inventory is complete, Step 1.1 has committed/pushed the physics/terrain, WorkerPool, RunFrame, RunRender, RenderGraph, RenderDeviceDX12, RenderBackendDX12 graph/transient, CameraCollection, TestScene collection, Input window-bridge, GameModelRenderer shadow-batch, SceneRuntime scene-object-id, AssetSystem registration/shader-key, TLASDX12 instance-count, FramebufferDX12 backend-lifetime, MeshDX12 upload-buffer, RenderBackendDX12 dynamic-geometry PSO-cache, RenderBackendDX12 pipeline-cache/descriptor-heap, RenderBackendDX12 platform-profiler GPU stack, RunScene DXR render-facet, TextureCollection capacity/context/hash, RenderBackendDX12 DXR TLAS-capacity, and pure math precondition Lane F sub-slices, Step 2.1 probe/automation P rows are complete, and Step 3.1 now has replay-load, authored scene object-group, Window creation, capture screenshot, Input cursor/client-coordinate, Timer high-resolution counter, text/font SDF atlas, DX12 screenshot readback, DXR initialization, DX12 device/backend startup, DX12 root-signature/gen-mips startup, TextureCollection texture-load, ConvexHullShape hull-load, DX12 lifecycle/depth-stencil/texture-create, DX12 PSO/dynamic-geometry, DX12 shader compile/reflection, and DX12 fence/readback recoverable boundaries complete. Resume Plan 04 by batching compatible same-lane/same-validator rows where safe; leave the RuntimeAllocationTracker Lane F row until a clean/approved perf gate is available. The overall engine cleanup goal remains active and incomplete. |
| Last documentation milestone | `fable_plans/HANDOFF-2026-07-07.md`, fable-05/fable-06 progress docs, `Agentic/Plans/In_Progress/shadow-edge-quality-plan.md`, `Agentic/Plans/In_Progress/shadow-edge-quality-progress.md`, the top-level `engine-cleanup-plans/` bundle, and `Agentic/Plans/To_Eval/render-graph-irender-interface-plan.md` document the latest fable, shadow-quality, engine-cleanup, and FAC-001 work. |
| Last source/data milestone | FAC-001 renderer aggregate retirement and the new shadow plan slice are complete: `IRenderBackend.h` is deleted, runtime render wiring uses narrow lifecycle/resource/command/diagnostics/capture/raytracing facets, `tools/check_runtime_boundaries.py` blocks aggregate resurrection, `shadow_parallel_prep` is no longer hard-disabled, `GameModelRenderer::BuildShadowCasterBatches()` uses bounded count/prefix/fill worker prep, object-shadow bounds use fixed chunk accumulators, and `WorkerPool::BuildChunkRangesNoAlloc()` exposes deterministic caller-owned chunk ranges. Runtime allocation allowlist metadata was also corrected after the Terrain factory cleanup removed `Run.cpp`'s direct Terrain allocation. |
| Pending work | Not all fable plans are complete. Open work remains in fable-03 P3.1-P3.4 worker-job stepping, remaining fable-04 mega-file decomposition, fable-05 remaining P4.1 deeper parser conversion plus P5 DX12 and closure, fable-06 C2-C5 plus Z1-Z4 closure, and fable-07 per `fable_plans/HANDOFF-2026-07-07.md`; fable-01, fable-02, fable-08, and fable-09 are checklist complete. |
| Concurrent work warning | The earlier detached duplicate fable-08 reveal-pacing diff was preserved in stash `codex-preserve-detached-reveal-pacing-duplicate-20260708`; do not drop it unless the user asks. |
| Blockers | `Agentic/Plans/In_Progress/overnight-blockers-2026-07-07.md`; Plan03 2 blocked, Plan05 9 blocked, Plan02 11 blocked, Plan01 4 blocked, Plan04 0 blocked. |
| Validation | Latest branch gates passed on 2026-07-08: FAC-001 touched-file comment audit inspected 24 source-bearing paths with 0 deferred, `python tools\check_runtime_boundaries.py --self-test` passed, `python tools\check_runtime_boundaries.py --max-errors 20` passed with 0 errors, targeted `tools\validate_build.bat Profile` passed in 14.80s with 0 warnings/errors, `tools\validate_fast.bat` passed, and `tools\validate_full.bat` passed with DX12 validation errors 0, screenshots matching baselines, and `physics_regression_solver.csv` byte-exact; shadow-prep opt-in audit inspected 3 source-bearing files with 0 deferred, `python tools\check_allocation_policy.py --self-test` passed, allocation policy passed with `allowlist_errors=0`, focused smoke emitted `WorkerBuildBatches`, `WorkerFillBatches`, and `WorkerScanBounds`, focused allocation guard exited 0 with `gameplay_violations=0`, isolated DX12 off/on proof produced byte-identical `off_nowater.bmp` / `on_nowater.bmp` (`5143326` bytes, SHA-256 `DCE3F4FEA913680F9E22BB72CB40539849E41AEF5D1758ACC0CE93B9DE946B61`), `tools\validate_full.bat` passed, and `tools\validate_perf.bat` completed with allocation guard PASS. Final merged-`main` gate `tools\validate_full.bat` rerun passed in 00:01:05.6465549 after resolving the Terrain resource-factory merge fix: project filters 0 errors, runtime boundaries 0 errors, Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, DX12 screenshots matched committed baselines, and `physics_regression_solver.csv` was byte-exact. |
| Current Plan 02 validation | 2026-07-09 Plan 02 Step 3.1 replay snapshot table-drive passed focused `tools\validate_build.bat Profile` (14.70s; 0 warnings/errors), `tools\validate_physics.bat` (26.69s; `VALIDATE_PHYSICS: ALL PASSED`; byte-exact physics baseline), `tools\validate_replay_scrub.bat` (11.03s; `VALIDATE_REPLAY_SCRUB: ALL PASSED`; scrub/restore SkullScope probes passed), and `tools\validate_physics_deep.bat` (84.09s; `VALIDATE_PHYSICS_DEEP: ALL PASSED`). Logs are listed in `engine-cleanup-plans/HANDOFF-2026-07-09-PLAN02-REPLAY-SNAPSHOT.md`. |
| Current Plan 04 validation | 2026-07-09 Plan 04 DX12 fence/readback recoverable result batch converted three strict `RenderDeviceDX12` rows to `SbResult`/neutral-return paths: Step 0.1 inventory rows 106, 107, and 109. Strict anchored source throw statement count is now 7; `SB_FATAL` macro invocations remain 165. Comment audit inspected 6 source-bearing files with 0 deferred; no subsystem checklist was required because this was a touched-file pass. `tools\validate_dx12_renderer.bat` passed in 00:00:40.0 with `VALIDATE_DX12_RENDERER: ALL PASSED`, formatting clean, Profile/Debug builds 0 warnings/errors, DX12 validation errors 0, and screenshots matching committed baselines. `python tools\check_runtime_boundaries.py` passed in 00:00:18.3 with 0 errors. Logs: `Agentic/Reports/validate_dx12_renderer_plan04_dx12_fence_result_20260709.log`, `Agentic/Reports/check_runtime_boundaries_plan04_dx12_fence_result_20260709.log`. |

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
- Engine cleanup latest handoff:
  `engine-cleanup-plans/HANDOFF-2026-07-09-OWNER-DECISIONS.md`.
  The latest Plan 04 source-slice handoff is
  `engine-cleanup-plans/HANDOFF-2026-07-09-FINAL-TAKEOVER.md`.
  Resume Plan 04 with Phase 3 Lane R boundary conversions unless the remaining
  RuntimeAllocationTracker Lane F row has an allocator-safe fatal strategy and a
  clean/approved perf gate path.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| Contact-audio perceptual model | Complete | Plan moved to `Agentic/Plans/Done/contact-audio-perceptual-model-plan.md`; final acceptance evidence covers material lab, rolling quietness, 200-brick/showcase reducer counts, tail quietness, and `validate_full`. |
| Contrived migration artifact cleanup | Complete; final review passed | Plan: `Agentic/Plans/To_Eval/contrived-migration-artifact-removal-plan.md`; kill list: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`; implementation status: `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`. |
| Missed plan items index | Active reference | `Agentic/Plans/missed_plan_items.md` summarizes actionable leftovers from mostly completed Done plans. |
| Runtime interaction state-machine hardening | Active plan | `Agentic/Plans/runtime-interaction-state-machine-hardening-plan.md`; avoid while another agent is editing UI/replay/camera/input code. |
| Render graph / backend interface continuation | Active plan | `Agentic/Plans/To_Eval/render-graph-irender-interface-plan.md`; FAC-001 renderer aggregate retirement is complete, but graph callbacks/transients/concrete DX12 owner cleanup continue in focused slices. |
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
