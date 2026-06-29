# Carmack Remaining Work Authoritative TODO

Status: In progress

Short description: finish the remaining Carmack architecture work after the
parallel rubber-duck review by closing the stale evidence gaps, resolving the
perf gate, hardening the physics boundary, and turning render-graph resource
ownership from diagnostic scaffolding into live production behavior.

This file supersedes the old Carmack open-todos handoff and the four archived
Carmack source plans as the active task queue. Use the older plans only as
historical context. Check items here only after the current branch contains the
work and the evidence path is recorded beside the item.

Historical references:

- `Agentic/Plans/Done/carmack-open-todos-agent-handoff-plan.md`
- `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md`
- `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`
- `Agentic/Plans/Done/carmack-render-backend-capability-plan.md`
- `Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md`

Known useful evidence to preserve:

- `TestOutput/validation/agent_logs/carmack_validate_full_final.log` passed the
  broad gate with DX12 validation errors 0 and byte-exact physics.
- `TestOutput/validation/agent_logs/carmack_validate_physics_after_parallel_threshold.log`
  passed after the parallel-threshold change.
- `Agentic/Reports/2026-06-29/carmack-handoff/perf-validation-note.md`
  records the Phase 1 perf-baseline review, refreshed perf baselines, and final
  passing `tools\validate_perf.bat` evidence.

## Agent Instructions

- Follow the repository Agent Startup Contract before editing.
- Run `git status --short --branch` and treat pre-existing dirty files as
  user-owned.
- Work one phase at a time. Do not attempt to close every Carmack area in one
  diff unless explicitly asked.
- For source-bearing edits, apply `Agentic/Reference/comment-style-guide.md`
  and run the comment-style audit over touched source before reporting done.
- For documentation-only updates to this plan, no repository validation is
  required.
- For code changes, defer formal repository validation until the PR/commit gate
  unless a focused build or probe answers a specific implementation question.

## Phase 0 - Evidence Reconciliation

- [x] Regenerate the global-service classification CSV and summary from the
  final current source tree so they no longer report stale hits such as old
  `TextureCollection.cpp` direct `Gfx()` access.
  Evidence: regenerated `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`
  and `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
  from current `tools/check_runtime_boundaries.py` matching logic; stale
  `SkullbonezSource/Assets/TextureCollection.cpp` `Gfx()` rows are gone.
- [x] Record which remaining global-service hits are allowed bootstrap,
  shutdown, OS callback bridge, diagnostics, or test/tool access, and which are
  still normal-path debt.
  Evidence: regenerated summary records 593 total hits: bootstrap 30,
  OS callback bridge 56, diagnostics 79, test/tool 30, normal runtime path 223,
  render pass 163, and asset lookup 12. `shutdown` has no separate rows in the
  regenerated classification.
- [x] Treat the old Carmack source plans and the old open-todos handoff as
  archived history. Do not chase their unchecked boxes unless this file points
  back to a specific item.
  Evidence: this authoritative plan remains the active queue; archived Carmack
  plans under `Agentic/Plans/Done/` were used only for classification wording.
- [x] Add final evidence paths to this file as work completes so the next agent
  can trust this file without reopening every historical plan.
  Evidence: generation/runtime-boundary log
  `TestOutput/validation/agent_logs/carmack_phase0_global_service_reconcile.log`;
  runtime-boundary JSON
  `TestOutput/validation/runtime_boundaries/carmack_phase0_runtime_boundaries.json`
  reported 0 errors.

## Phase 1 - Perf Gate Closure

- [x] Investigate the remaining `tools\validate_perf.bat` failure recorded in
  `TestOutput/validation/agent_logs/carmack_validate_perf_after_parallel_threshold.log`.
  The current known failures are the `PHYSICS_BENCH` relative `Frame`,
  `Frame/Render`, `Frame/VsyncWait`, and memory start/restart/end deltas.
  Evidence: fresh current run
  `TestOutput/validation/agent_logs/carmack_phase1_validate_perf_initial.log`
  reproduced the relative `PHYSICS_BENCH` failure with absolute budgets passing;
  `PHASE1_VALIDATE_PERF_INITIAL_EXIT=7`.
- [x] Decide whether the perf deltas are a real regression, measurement noise,
  or an intentional baseline shift. Fix the code for real regressions; update
  baselines only when the slower numbers are intentional and reviewed.
  Evidence: closed as an intentional baseline shift in
  `Agentic/Reports/2026-06-29/carmack-handoff/phase1-perf-baseline-update-note.md`;
  refreshed `TestOutput/baselines/dx12_perf.json` and
  `TestOutput/baselines/physics_bench_perf.json` from current Profile artifacts.
- [x] Preserve the passing absolute-budget evidence, but do not use it as final
  perf closure while the relative gate still exits nonzero.
  Evidence: initial log records `PASS: absolute perf budgets [DX12]` and
  `PASS: absolute perf budgets [PHYSICS_BENCH]`; it was retained only as the
  failing-before evidence, not final closure.
- [x] Produce either a clean `tools\validate_perf.bat` log or an explicit
  reviewed waiver/baseline-update note for the remaining relative failures.
  Evidence: `tools\update_baselines.bat --perf --require` passed in
  `TestOutput/validation/agent_logs/carmack_phase1_update_perf_baselines.log`;
  final `tools\validate_perf.bat` passed in
  `TestOutput/validation/agent_logs/carmack_phase1_validate_perf_final.log`
  with `PASS: No regressions [DX12]`, `PASS: No regressions [PHYSICS_BENCH]`,
  `VALIDATE_PERF: COMPLETE`, and `PHASE1_VALIDATE_PERF_FINAL_EXIT=0`.

## Phase 2 - Global Service Lifetime

- [ ] Audit the regenerated classification and identify any remaining
  unauthorized normal runtime path, render pass, asset lookup, or diagnostics
  global access.
  Partial evidence: Phase 2 service-singleton slice regenerated
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons.csv`
  and summary. Counts moved from 593 total hits to 579: `normal runtime path`
  223 -> 216 and `render pass` 163 -> 156.
- [ ] Route remaining unauthorized render-pass backend access through explicit
  render contexts or capability interfaces.
  Evidence:
- [ ] Route remaining unauthorized asset, texture, camera, window, skybox,
  worker, config, profiler, or graphics-service access through borrowed
  runtime-owned services or explicit context parameters.
  Partial evidence: removed unused `CameraCollection::Instance()` /
  `CameraCollection::Destroy()` and `SkyBox::Instance()` / `SkyBox::Destroy()`
  singleton surfaces plus their `pInstance` storage. Run already value-owns the
  camera collection and skybox through `RunSubsystemState`.
- [ ] Keep service contexts borrowed and lifetime-annotated. Do not introduce a
  new global service locator under a friendlier name.
  Evidence:
- [ ] Ratchet `tools/check_runtime_boundaries.py` allowlists or guardrails so
  removed normal-path global access cannot silently return.
  Partial evidence: lowered the camera and skybox singleton/pInstance
  `GLOBAL_SERVICE_ACCESS_ALLOWLIST` entries; runtime-boundary check
  `TestOutput/validation/agent_logs/carmack_phase2_service_singletons_runtime_boundaries.log`
  passed with 0 errors.

## Phase 3 - Physics Standalone Boundary

- [x] Decide and document the final standalone boundary contract: either make
  `PhysicsBodyStore`/`PhysicsScene` the step authority, or explicitly narrow
  the accepted compatibility bridge through `PhysicsModelAccess`.
  Evidence: Phase 3 selected the accepted `PhysicsModelAccess` compatibility
  bridge for this slice. `SkullbonezSource/Physics/PhysicsScene.cpp` now
  documents `PhysicsScene::RunPhysics()` as the named load-solve-writeback sync
  bridge rather than hidden standalone ownership.
- [x] If strict standalone authority is still required, stop loading from and
  writing back to model-backed `PhysicsModelAccess` every step; keep model sync
  at explicit boundary points.
  Evidence: Not applicable after selecting the compatibility-bridge contract;
  strict store-owned stepping remains a future dedicated migration because it
  would touch solver, sleep, joints, diagnostics, replay, editor, and render
  mirrors.
- [x] If the compatibility bridge is accepted, document the invariant clearly
  and adjust guardrails/tests so future work cannot mistake it for full
  standalone ownership.
  Evidence: `SkullbonezSource/Physics/PhysicsScene.cpp` adds the bridge
  invariant. Existing runtime-boundary validation still passes with 0 errors in
  `TestOutput/validation/agent_logs/carmack_phase3_validate_full.log`; no
  guardrail file changed in this slice.
- [x] Add or extend smoke coverage for standalone physics API lifecycle,
  runtime handle mirrors, store handles, render mirrors, and joint handles.
  Evidence: `SkullbonezSource/Runtime/Init.cpp` extends
  `RunPhysicsRuntimeHandleSmokeSample()` and `--physics-standalone-smoke`
  reports `store_handles=pass`, `render_mirror=pass`, `joint_handles=pass`,
  and `collider_refresh=pass` in
  `TestOutput/validation/agent_logs/carmack_phase3_validate_physics.log`.
- [x] Add a sharper invariant or test for collider-store freshness when
  same-count authoring/edit paths change shape, restitution, material, or other
  collider-affecting data.
  Evidence: the runtime mirror smoke mutates model 0 from sphere to box after
  the first `GetColliderStore()` call, then verifies refreshed shape,
  restitution, bounding radius, surface area, drag/material data, stable handles,
  stable body reference, and unchanged collider count. Final gates:
  `tools\validate_physics.bat` passed in 24.17s and `tools\validate_full.bat`
  passed in 29.34s.

## Phase 4 - Render Backend Capability

- [x] Reconfirm `IRenderBackend` remains a methodless temporary facade and does
  not regain direct render operations.
  Evidence: `SkullbonezSource/Rendering/IRenderBackend.h` owns only
  `~IRenderBackend()` and inherits `IRenderDeviceLifecycle`,
  `IRenderResourceFactory`, `IRenderCommandContext`, `IRenderDiagnostics`, and
  `IRenderCaptureBackend`. It does not inherit `IRenderRayTracing`; DXR methods
  remain in `SkullbonezSource/Rendering/IRenderRayTracing.h`.
- [x] Reconfirm runtime render pass bodies stay on narrow contexts and do not
  regain direct `Gfx()` or `GfxRayTracing()` access.
  Evidence:
  `TestOutput/validation/agent_logs/carmack_phase4_pass_boundary_scan.log`
  reports no matches for `Gfx()`, `GfxRayTracing()`, or `IRenderBackend` in
  `RunPasses.cpp`, `RuntimeRenderPasses.h`, `RuntimeRenderInputs.h`, and
  `RunUiTextPass.cpp`. `Run::Render()` remains the accepted composition-root
  borrow path and passes `IRenderRayTracing*` through render services.
- [x] Reconfirm the runtime-boundary checker blocks raytracing inheritance,
  direct facade methods, and pass-body global backend access.
  Evidence:
  `TestOutput/validation/agent_logs/carmack_phase4_runtime_boundaries.log`
  reports runtime-boundary validation passed with 0 errors and wrote
  `TestOutput/validation/runtime_boundaries/carmack_phase4_runtime_boundaries.json`;
  elapsed 4.9s.
- [x] Resolve any render-backend issues found by the regenerated
  global-service classification. The rubber-duck review found the mechanics
  mostly satisfied, so this phase should remain narrow unless new evidence says
  otherwise.
  Evidence:
  `TestOutput/validation/agent_logs/carmack_phase4_renderer_classification_summary.log`
  found 191 renderer-related classification rows, but no runtime pass-body
  backend hits. The two `RunRender.cpp` hits are accepted composition-root
  borrows. Remaining normal-runtime renderer globals are transferred to Phase 2
  service-lifetime and Phase 5 render-ownership work rather than Phase 4
  capability leakage.

## Phase 5 - Render Graph Resource Ownership

- [x] Promote graph-created transient resources from diagnostic skeleton output
  into at least one live production frame path that writes to and/or samples a
  graph-owned transient during rendering.
  Evidence: `RuntimeRenderer::ExecuteCinematicPostThroughRenderGraph` now
  declares cinematic `VolumetricLight` with `RenderGraph::AddTransientResource`,
  materializes it through `IRenderCommandContext::MaterializeGraphTransientResources`,
  passes the resolved `RenderGraphTextureBinding` into `VolumetricPass::Render`,
  and passes the same graph-owned texture handle into `TonemapPass::Render`.
  Fresh focused-launch evidence in `Debug/dx12_cinematic_post_graph.txt` and
  `Debug/dx12_frame_graph_actual.txt` shows `VolumetricLight external=false`.
- [x] Ensure the production graph-owned transient is exercised by DX12
  screenshot validation and does not appear only as `DeclarationOnly` skeleton
  metadata.
  Evidence: `tools\validate_dx12_renderer.bat` passed on the final Phase 5
  source state. Latest artifacts:
  `TestOutput\validation\dx12_renderer\20260629T151157Z\manifest.json`,
  `TestOutput\validation\dx12_renderer\20260629T151157Z\summary.json`, and
  `TestOutput\validation\dx12_renderer\20260629T151157Z\dx12_validation.txt`.
  Summary status is `pass`; InfoQueue errors are 0; `water_ball_test` and
  `solver_smoke` screenshots match committed DX12 baselines. A focused
  cinematic launch of
  `Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --scene SkullbonezData/scenes/water_ball_test.scene.json --cinematic on --frames 2`
  refreshed `Debug\dx12_frame_graph_actual.txt` with `cinematic_render=true`,
  `volumetric_callback_owned=true`, `volumetric_ready=true`, and
  `tonemap_callback_owned=true`.
- [x] Fix or prove backend transient pool reuse for compatible non-overlapping
  transient resources allocated in the same compile. The reviewed risk is that
  `usedThisCompile` prevents legitimate same-compile aliasing.
  Evidence: `RenderBackendDX12::MaterializeGraphTransientResources` now keys
  backend reuse through the compiler `poolSlot` and descriptor compatibility
  instead of rejecting any candidate marked `usedThisCompile`. Initial resource
  state is reset only on the first use of a physical slot in a compile, so
  same-compile aliases preserve the previous alias' planned final state.
- [x] Add focused materializer/compiler coverage or diagnostic evidence proving
  compatible non-overlapping graph transients share a pool slot without
  unbounded growth.
  Evidence: `Agentic\Tests\Dx12ArchUnitTests\Dx12ArchUnitTests.cpp` adds
  `DX12 graph transient pool-slot reuse allows same-compile alias`, and
  `tools\validate_dx12_arch_tests.bat` passed with that test included.
- [x] Move the relevant resource-state/barrier ownership for graph-owned
  resources into the graph planner/backend materializer path, with descriptor
  lifetime and shutdown accounting visible in diagnostics.
  Evidence: `Debug\dx12_cinematic_post_graph.txt` records
  `VolumetricLight PixelShaderResource -> RenderTarget -> PixelShaderResource`,
  `TransientAllocations: resource=VolumetricLight slot=0 first_pass=0 last_pass=1 descriptors=2 reused=false released_at_frame_end=true`,
  and materialization stats including `volumetric_binding_valid=true`,
  `volumetric_texture_handle=22`, `pool_size=2`, `created_this_compile=1`,
  `descriptor_rows_owned=4`, and `released_at_frame_end=1`. The actual
  executed frame graph records the same production transient at passes 9-10.
  Note: `released_at_frame_end` is the graph planner's logical lifetime marker;
  the DX12 physical transient pool remains retained for reuse until backend
  shutdown/release.
- [x] Keep backend-owned legacy resources working while the graph-owned path is
  introduced; do not remove legacy lifetime hooks until validation proves the
  replacement path.
  Evidence: `VolumetricPass::Render` and `TonemapPass::Render` retain legacy
  target/scene-color fallbacks when no valid graph binding is provided.
  `tools\validate_dx12_renderer.bat` passed without visual baseline changes.
  The Phase 5 progress file
  `Agentic/Plans/IN PROGRESS/carmack-phase-5-render-graph-resource-ownership-progress.md`
  records the full checklist, touched-file comment audit, and read-only
  rubber-duck review. Phase 5 reviewer Linnaeus reported no blocking issues.

## Phase 6 - Comment Audit And Final Validation

- [ ] Run the comment-style audit for every touched source-bearing file in the
  final implementation slice and record the result here.
  Evidence:
- [ ] Run `git diff --check` before final handoff.
  Evidence:
- [ ] Run the smallest required validation for each final code area touched:
  `tools\validate_physics.bat` for physics boundary changes,
  `tools\validate_dx12_renderer.bat` for render-graph/DX12 changes, and
  `tools\validate_perf.bat` for hot-path or allocation-sensitive changes.
  Evidence:
- [ ] Run the final broad gate once the branch is ready for PR/commit handoff:
  `tools\validate_full.bat`.
  Evidence:
- [ ] Rubber-duck this authoritative plan against the final branch state before
  declaring the Carmack work complete.
  Evidence:

## Definition Of Done

- [ ] This plan has no unchecked implementation, evidence, validation, or audit
  items.
- [ ] The global-service classification is regenerated from the final source
  and reconciled against this plan.
- [ ] `tools\validate_perf.bat` passes, or the remaining perf deltas have an
  explicit reviewed waiver/baseline-update note.
- [ ] `tools\validate_full.bat` passes on the final branch state.
- [ ] Any touched source-bearing files have comment-audit evidence.
- [ ] The final handoff names this plan as the source of truth and treats the
  older Carmack plans as archived history only.
