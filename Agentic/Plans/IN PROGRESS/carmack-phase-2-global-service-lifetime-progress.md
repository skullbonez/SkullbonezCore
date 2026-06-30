# Carmack Phase 2 - Global Service Lifetime Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: `Phase 2 - Global Service Lifetime`

Current status: final Phase 2 audit and guardrail-ratchet closure complete for
the authoritative plan. Broad render/config/worker/diagnostics globals remain,
but they are recorded as audited compatibility debt with concrete owners rather
than treated as eliminated code.

Phase 0 input:
`Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
reports 593 classified hits: `normal runtime path` 223, `render pass` 163,
`asset lookup` 12, and `diagnostics` 79.

Current final evidence:
`Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons-summary.md`
reports 579 classified hits: `normal runtime path` 216, `render pass` 156,
`asset lookup` 12, and `diagnostics` 79.
`Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final-summary.md`
regenerated from current source at `b9323782` reports the same 579 classified
hits, 102 live `(file,label)` groups, 102 allowlist groups after ratchet, zero
stale allowlist groups, and zero over-budget groups.

Completed slice:

- Removed unused `CameraCollection::Instance()`, `CameraCollection::Destroy()`,
  and `CameraCollection::pInstance` from
  `SkullbonezSource/Runtime/CameraCollection.cpp` and
  `SkullbonezSource/Runtime/CameraCollection.h`.
- Removed unused `SkyBox::Instance()`, `SkyBox::Destroy()`, and
  `SkyBox::pInstance` from `SkullbonezSource/World/SkyBox.cpp` and
  `SkullbonezSource/World/SkyBox.h`.
- Lowered matching `GLOBAL_SERVICE_ACCESS_ALLOWLIST` rows in
  `tools/check_runtime_boundaries.py`.
- Boundary check:
  `TestOutput/validation/agent_logs/carmack_phase2_service_singletons_runtime_boundaries.log`
  reports 0 errors and `PHASE2_SERVICE_SINGLETONS_BOUNDARY_EXIT=0`.
- Comment-style audit scope:
  `SkullbonezSource/Runtime/CameraCollection.cpp`,
  `SkullbonezSource/Runtime/CameraCollection.h`,
  `SkullbonezSource/World/SkyBox.cpp`,
  `SkullbonezSource/World/SkyBox.h`, and
  `tools/check_runtime_boundaries.py`; 5 checked, 0 deferred.
- Initial validation:
  `TestOutput/validation/agent_logs/carmack_phase2_service_singletons_validate_fast.log`
  reports `VALIDATE_FAST: ALL PASSED`,
  `PHASE2_SERVICE_SINGLETONS_VALIDATE_FAST_EXIT=0`, and
  `PHASE2_SERVICE_SINGLETONS_VALIDATE_FAST_ELAPSED=129.78s`. Because the slice
  touches `Runtime/*`, the final commit gate is `tools\validate_full.bat`.
- Commit-gate validation:
  `TestOutput/validation/agent_logs/carmack_phase2_service_singletons_validate_full.log`
  reports `VALIDATE_FULL: DEFAULT GATE PASSED`,
  `PHASE2_SERVICE_SINGLETONS_VALIDATE_FULL_EXIT=0`, and
  `PHASE2_SERVICE_SINGLETONS_VALIDATE_FULL_ELAPSED=139.58s`.
- Final audit/ratchet closure:
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final.csv`,
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final-summary.md`,
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.csv`,
  and
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.md`.
- Removed stale `SkullbonezSource/UI/UIBackdropBlur.cpp`
  `CreateShaderFromActiveAssets()` and `Gfx()` allowlist entries plus the stale
  renderer-service classification from `tools/check_runtime_boundaries.py`.
- The final `tools\validate_fast.bat` gate initially caught a Phase 5 project
  metadata gap for `SkullbonezSource\Rendering\DX12\RenderGraphTransientDX12.h`.
  The closure slice added that header to `SKULLBONEZ_CORE.vcxproj` and
  `SKULLBONEZ_CORE.vcxproj.filters`, then added the
  `RenderGraphTransientDX12` DX12 prefix to `tools/validate_project_filters.py`.
- Final comment-style audit scope for this closure slice:
  `tools/check_runtime_boundaries.py` and
  `tools/validate_project_filters.py`; 2 checked, 0 deferred. Both files
  already have learning headers and the touched sections are straightforward
  allowlist/prefix data under existing explanatory comments.

## Checklist

### Classification And Scope

- [x] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification.csv`
  from the final current source tree using the same matching logic as
  `tools/check_runtime_boundaries.py`.
  Evidence:
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons.csv`
  and final current-source regeneration
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final.csv`.
- [x] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification-summary.md`
  from that CSV and record the exact generation command in this file.
  Evidence:
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons-summary.md`
  and final current-source summary
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final-summary.md`.
  Command/method: inline PowerShell here-string piped to `python -`; the script
  imports `tools/check_runtime_boundaries.py` via `importlib`, scans
  `SkullbonezSource/**/*.{cpp,h,hpp,inl}` with
  `GLOBAL_SERVICE_ACCESS_PATTERNS`, `GENERIC_INSTANCE_ACCESS_PATTERN`,
  `PROCESS_GLOBAL_POINTER_PATTERN`,
  `MUTABLE_PROCESS_GLOBAL_PATTERN`,
  `strip_cpp_comments_and_string_literals()`, and `line_for_offset()`, then
  classifies by `(file,label)` from the after-service-singletons CSV with
  deterministic fallback rules for any new group.
- [x] Record the Phase 2 starting totals for `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` from the regenerated summary.
  Phase 0 totals: `normal runtime path` 223, `render pass` 163,
  `asset lookup` 12, `diagnostics` 79. After the service-singleton slice:
  `normal runtime path` 216, `render pass` 156, `asset lookup` 12,
  `diagnostics` 79. Final current totals remain `normal runtime path` 216,
  `render pass` 156, `asset lookup` 12, `diagnostics` 79.
- [x] Audit every regenerated `normal runtime path`, `render pass`,
  `asset lookup`, and `diagnostics` row and record one decision per row:
  `remove now`, `borrow context now`, `authorized bootstrap/shutdown/callback`,
  `diagnostics exception`, `test/tool exception`, or `defer to Phase 3/4/5`.
  Evidence:
  `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.md`
  audits 463 target hits across 81 `(classification,file,label)` groups and
  records `remove now` for the stale `UIBackdropBlur.cpp` guardrail debt.
- [x] Record any rows intentionally deferred out of Phase 2 with the owning
  phase and file path, not a generic note.
  Evidence: the final decision table records each retained target group with a
  concrete owner such as Phase 3 physics compatibility, Phase 5 replay
  compatibility, future render-resource context, future runtime settings
  context, future asset/shader service context, or diagnostics exception.

### Render-Pass Backend Access

- [x] Audit direct `Gfx()` render-pass access from
  `SkullbonezSource/Rendering/Helper.cpp` by routing material-table texture
  binding, instanced mesh creation/destroy, dynamic VB creation/destroy, and
  draw calls through borrowed `IRenderCommandContext` and
  `IRenderResourceFactory` services.
  Decision: retained as owned by future render-resource context; no safe
  single-worker migration because helper resource creation and draw calls span
  shader, material, mesh, and pass plumbing.
- [x] Audit `CreateShaderFromActiveAssets()` in
  `SkullbonezSource/Rendering/Helper.cpp` by passing an explicit asset/shader
  creation context from runtime-owned services.
  Decision: retained as owned by future render-resource context with explicit
  asset/shader service context work.
- [x] Audit direct `Gfx()` and shader-factory access from
  `SkullbonezSource/Rendering/Text.cpp` by giving text rendering explicit
  render-resource and shader-creation services.
  Decision: retained as owned by future render-resource context for the text
  renderer.
- [x] Audit direct `Gfx()` and shader-factory access from
  `SkullbonezSource/UI/UI.cpp` and
  `SkullbonezSource/UI/UIBackdropBlur.cpp` through explicit UI render contexts.
  Decision: `UI.cpp` is retained as future UI render-context debt;
  `UIBackdropBlur.cpp` has zero current hits, so its stale allowlist and
  renderer-service classification were removed now.
- [x] Audit `SkullbonezSource/Runtime/RunRender.cpp` direct `Gfx()` and
  `GfxRayTracing()` use. If it remains composition-root wiring, record that
  classification; otherwise move the borrow to a narrower runtime render
  context.
  Decision: accepted composition-root borrow; pass bodies stay on narrower
  contexts.
- [x] Audit scene/runtime renderer setup access in
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, including `Gfx()`,
  `GfxRayTracing()`, and renderer-name/capability queries, through explicit
  runtime render services.
  Decision: retained as future scene render-resource context and future scene
  runtime services context debt.
- [x] Audit world render-resource globals in
  `SkullbonezSource/World/Terrain.cpp`,
  `SkullbonezSource/World/SkyBox.cpp`, and
  `SkullbonezSource/World/WorldEnvironment.cpp` through
  `RenderResourceContext`, `IRenderCommandContext`, or `EngineServices`.
  Decision: retained as future world render-resource context debt.
- [x] Audit editor transient render globals in
  `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` and
  `SkullbonezSource/Runtime/Editor/RunEditorTracer.inl` through explicit
  tool/render contexts, or record a diagnostics-only exception if retained.
  Decision: current classification marks these rows as `test/tool`; they are
  outside the requested target audit and remain tool/debug compatibility debt.

### Asset, Texture, Camera, Window, Skybox Services

- [x] Audit `ActiveAssetSystem()` and
  `CreateShaderFromActiveAssets()` compatibility access in
  `SkullbonezSource/Assets/AssetSystem.cpp` and
  `SkullbonezSource/Assets/AssetSystem.h`; replace normal-path callers with an
  explicit `AssetSystem` plus render-resource/shader factory context.
  Decision: retained as accepted asset/shader compatibility debt owned by a
  future asset/shader service context.
- [x] Audit `TextureCollection::Instance()` and direct backend use in
  `SkullbonezSource/Assets/TextureCollection.cpp`; use the existing
  `BindRenderContexts(IRenderResourceFactory*, IRenderCommandContext*)` path and
  prove `m_renderResources`/`m_renderCommands` cover create, bind, and delete.
  Decision: retained as accepted asset texture compatibility debt owned by a
  future texture service context.
- [x] Audit normal-path camera singleton access in
  `SkullbonezSource/Runtime/CameraCollection.cpp`,
  `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`, and
  `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`; pass
  `EngineServices::cameras` or a narrower scene camera context.
  Decision: no current `CameraCollection::Instance()` target rows remain after
  the service-singleton cleanup; retained `Camera.cpp` and
  `CameraCollection.cpp` `Cfg()` rows are future camera settings-context debt.
- [x] Audit `SkullbonezSource/Runtime/Run.cpp` singleton setup rows for
  `TextureCollection::Instance()`, `CameraCollection::Instance()`,
  `Window::Instance()`, and `SkyBox::Instance()`. Reclassify true composition
  root/bootstrap rows, and remove any normal runtime lookup by borrowing from
  `RunSubsystemState`/`EngineServices`.
  Decision: retained `Run.cpp` target rows are accepted composition-root
  borrows; true bootstrap rows stay classified as bootstrap rather than normal
  runtime-path debt.
- [x] Audit skybox singleton lifetime in
  `SkullbonezSource/World/SkyBox.cpp` and
  `SkullbonezSource/World/SkyBox.h`; keep skybox owned by runtime/world state
  rather than `pInstance`.
  Slice evidence: removed unused `SkyBox::Instance()`, `SkyBox::Destroy()`, and
  `pInstance`; Run already value-owns skybox through
  `RunSubsystemState::skyBoxOwner`. Remaining `SkyBox.cpp` render/config rows
  are deferred to the world render-resource context work.
- [x] Audit `SkullbonezSource/Runtime/Window.cpp` for callback/window bridge
  rows. Keep only OS callback bridge or bootstrap access; route normal resize
  behavior through a bound window/render service.
  Decision: current `Window.cpp` rows remain classified as OS callback bridge;
  no normal runtime-path window singleton row remains in the target audit.

### Worker, Config, And Profiler Services

- [x] Group `Cfg()` rows by owner before changing code. Current clusters:
  physics (`SkullbonezSource/Physics/*`, `SkullbonezSource/GameObjects/GameModel.cpp`),
  runtime/input (`SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`), camera
  (`SkullbonezSource/Runtime/Camera.cpp`), scene
  (`SkullbonezSource/Runtime/Scene/RunScene.cpp`), and world
  (`SkullbonezSource/World/*`).
  Evidence: final decision table groups `Cfg()` rows under Phase 3 physics
  compatibility, Phase 5 replay compatibility, future runtime settings,
  future scene runtime services, future camera settings, future render settings,
  and future world render-resource contexts.
- [x] Audit runtime-owned `Cfg()` access by passing config snapshots or
  explicit settings contexts, especially in
  `SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Camera.cpp`, and
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`.
  Decision: retained as future runtime settings, frame diagnostics/settings,
  camera settings, and scene runtime services debt.
- [x] Defer or coordinate physics `Cfg()` rows in
  `SkullbonezSource/Physics/PhysicsWorld.cpp`,
  `SkullbonezSource/Physics/PersistentContactSolver.cpp`,
  `SkullbonezSource/Physics/RigidBody.cpp`,
  `SkullbonezSource/Physics/BoundingSphere.cpp`, and
  `SkullbonezSource/GameObjects/GameModel.cpp` with Phase 3 before changing
  solver behavior.
  Decision: owned by Phase 3 physics compatibility / future physics
  config-worker context because changing solver/body config reads risks physics
  determinism and scheduling behavior.
- [x] Audit `WorkerPool::Instance()` normal-path access in
  `SkullbonezSource/Physics/PhysicsWorld.cpp`,
  `SkullbonezSource/Rendering/GameModelRenderer.cpp`,
  `SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl`,
  `SkullbonezSource/Runtime/RuntimeTuning.cpp`,
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, and
  `SkullbonezSource/Runtime/RunUiTextPass.cpp` through a borrowed worker
  service or explicit scheduling context.
  Decision: grouped by owner as Phase 3 physics compatibility, Phase 5 replay
  compatibility, future renderer worker/config context, future tuning/worker
  service context, future scene runtime services context, and future UI text
  render/diagnostics context.
- [x] Audit direct `Profiler::Instance()` normal/runtime UI access in
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunUiTextPass.cpp`,
  `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`, and
  `SkullbonezSource/UI/UITabProfiler.cpp` through `DiagnosticsRuntime` or a
  diagnostics view. Record any macro-level exception separately.
  Decision: `RuntimeDiagnostics.cpp` and `UITabProfiler.cpp` are diagnostics
  exceptions; `RunFrame.cpp` and `RunUiTextPass.cpp` are retained as future
  frame diagnostics/settings and UI text render/diagnostics context debt.

### Guardrails And Records

- [x] Lower affected entries in
  `tools/check_runtime_boundaries.py` `GLOBAL_SERVICE_ACCESS_ALLOWLIST` after
  each removal.
  Evidence: removed the camera and skybox singleton/pInstance allowances from
  `tools/check_runtime_boundaries.py`; boundary checker reports 0 errors. Final
  closure additionally removed stale `SkullbonezSource/UI/UIBackdropBlur.cpp`
  `CreateShaderFromActiveAssets()` and `Gfx()` allowlist entries. Live groups
  and allowlist groups now both equal 102.
- [x] Remove or narrow entries in
  `tools/check_runtime_boundaries.py`
  `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` when a file no longer owns a
  direct renderer-service compatibility role.
  Evidence: removed the stale `SkullbonezSource/UI/UIBackdropBlur.cpp`
  renderer-service classification because the current source has zero matching
  renderer global rows.
- [x] Add or update synthetic self-tests in `tools/check_runtime_boundaries.py`
  for any new rejected pattern, especially renamed renderer, asset, worker, or
  diagnostics service access.
  Evidence: no new rejected pattern was added in this closure slice; only
  existing counted entries and a stale renderer-file classification were
  lowered. The existing runtime-boundary self-tests still run as part of
  `tools/check_runtime_boundaries.py`.
- [x] Run `python tools\check_runtime_boundaries.py` after guardrail edits and
  record the log path.
  Evidence:
  `TestOutput\validation\agent_logs\carmack_phase2_service_singletons_runtime_boundaries.log`;
  JSON:
  `TestOutput\validation\runtime_boundaries\carmack_phase2_service_singletons_runtime_boundaries.json`.
  Final closure path:
  `TestOutput\validation\agent_logs\carmack_phase2_final_runtime_boundaries.log`;
  JSON:
  `TestOutput\validation\runtime_boundaries\carmack_phase2_final_runtime_boundaries.json`.
  Result: `PASS: Runtime boundary validation passed.`,
  `PHASE2_FINAL_RUNTIME_BOUNDARIES_EXIT=0`, elapsed 4.88s.
- [x] Record before/after CSV and summary paths in this file, then copy final
  closure evidence back to the authoritative plan only when the phase is
  genuinely complete and the worker is allowed to update it.
  Evidence: final paths are recorded here and in the Phase 2 section of
  `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`.

## Likely Files And Tools To Inspect

- `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`
- `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification.csv`
- `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
- `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md` as historical
  context only
- `Agentic/Plans/global-service-context-plan.md`
- `SkullbonezSource/Runtime/EngineContext.h`
- `SkullbonezSource/Runtime/EngineContext.cpp`
- `SkullbonezSource/Runtime/RunState.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderInputs.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp`
- `SkullbonezSource/Runtime/RunPasses.cpp`
- `SkullbonezSource/Runtime/RunRender.cpp`
- `SkullbonezSource/Rendering/Helper.cpp`
- `SkullbonezSource/Rendering/Text.cpp`
- `SkullbonezSource/Assets/AssetSystem.cpp`
- `SkullbonezSource/Assets/TextureCollection.cpp`
- `tools/check_runtime_boundaries.py`
- `tools/validate_runtime_boundaries.bat`

Useful commands:

```powershell
codegraph status .
codegraph explore "EngineContext EngineServices RuntimeRenderInputs TextureCollection global service lifetime"
rg -n "Gfx\(|GfxRayTracing\(|Cfg\(|ActiveAssetSystem\(|CreateShaderFromActiveAssets\(|::Instance\(|pInstance|g_[A-Za-z_]" SkullbonezSource
Import-Csv -LiteralPath 'Agentic\Reports\2026-06-29\carmack-handoff\global-service-hit-classification.csv' | Group-Object classification,label,file
python tools\check_runtime_boundaries.py
tools\validate_runtime_boundaries.bat
```

## Dependencies

- Phase 0 must provide a regenerated classification CSV/summary before Phase 2
  counts can be trusted.
- Phase 3 owns the physics standalone boundary. Coordinate before changing
  physics `Cfg()` or `WorkerPool::Instance()` rows that may affect solver
  determinism, worker scheduling, or physics store ownership.
- Phase 4 overlaps direct renderer capability access. Coordinate any
  `IRenderBackend`, `Gfx()`, or `GfxRayTracing()` capability changes that are
  more than borrowed-context plumbing.
- Phase 6 owns final comment audit and validation evidence for touched source.
- Do not edit unrelated progress files or the authoritative plan while another
  worker owns them.

## Evidence To Collect

- Regenerated classification CSV path and summary path:
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-classification-after-service-singletons.csv`
  and
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-classification-after-service-singletons-summary.md`;
  final current-source closure files are
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-classification-final.csv`
  and
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-classification-final-summary.md`.
- A per-row or per-file decision table for all remaining `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` rows:
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-decision-table-final.md`
  and
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-decision-table-final.csv`.
- Before/after `GLOBAL_SERVICE_ACCESS_ALLOWLIST` counts from
  `tools/check_runtime_boundaries.py`: before final ratchet 104 allowlist
  groups with 2 stale `UIBackdropBlur.cpp` groups; after final ratchet 102
  allowlist groups, 102 live groups, 0 stale groups, 0 over-budget groups.
- `python tools\check_runtime_boundaries.py` output and log path after any
  guardrail update:
  `TestOutput\validation\agent_logs\carmack_phase2_final_runtime_boundaries.log`
  and
  `TestOutput\validation\runtime_boundaries\carmack_phase2_final_runtime_boundaries.json`.
- Comment-style audit result for every touched source-bearing file: checked
  `CameraCollection.cpp`, `CameraCollection.h`, `SkyBox.cpp`, `SkyBox.h`, and
  `tools/check_runtime_boundaries.py`; 5 checked, 0 deferred for the
  service-singleton slice. Final closure slice touched
  `tools/check_runtime_boundaries.py` and
  `tools/validate_project_filters.py`; 2 checked, 0 deferred.
- Phase 2 rubber-duck review:
  Phase2-duck-01 (`019f13c0-0a1f-7242-8239-62156b06c9a6`) initially blocked
  commit on the stricter `Runtime/*` validation requirement and a stale
  singleton comment on `CameraCollection::Reset()`. Follow-up fixed the comment,
  reran the focused audit, reran runtime-boundary logging with the explicit exit
  marker, and ran `tools\validate_full.bat`.
- Validation logs selected by touched area: `tools\validate_dx12_renderer.bat`
  for renderer/render-context changes, `tools\validate_physics.bat` for physics
  config or worker changes, `tools\validate_perf.bat` for hot-path worker or
  allocation-sensitive changes, and `tools\validate_full.bat` at final
  PR/commit handoff.

## Validation Note

The earlier service-singleton source/tool slice touched `Runtime/*`, so the
commit gate was `tools\validate_full.bat`. Evidence:
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_validate_full.log`
reports `VALIDATE_FULL: DEFAULT GATE PASSED` and exit 0. A preliminary
`tools\validate_fast.bat` pass is in
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_validate_fast.log`.
The changed boundary checker was also run directly in
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_runtime_boundaries.log`.

Final closure slice touches documentation/reports, a guardrail ratchet inside
`tools/check_runtime_boundaries.py`, the project-filter validator, and project
metadata for the Phase 5 DX12 transient header. Per `tools/*` mapping,
`tools\validate_fast.bat` is the commit gate. Evidence:
`TestOutput\validation\agent_logs\carmack_phase2_final_runtime_boundaries_rerun.log`
reports `PASS: Runtime boundary validation passed.` with 0 errors, and
`TestOutput\validation\agent_logs\carmack_phase2_final_validate_fast.log`
reports project filters 0 errors, runtime boundaries 0 errors, Profile and
Debug builds succeeded with 0 warnings/errors, and
`VALIDATE_FAST: ALL PASSED`. `git diff --check` was clean for the final
handoff.

## Residual Debt And Owners

- The final classification is current for `b9323782`; stale
  `UIBackdropBlur.cpp` allowlist/classification entries were removed.
- No standalone CSV generator exists in `tools/`; the exact inline generator
  method is recorded above and in the final summary report.
- `TextureCollection.cpp` remains accepted asset texture compatibility debt
  owned by a future texture service context.
- `Run.cpp` and `RunRender.cpp` target rows are explicitly accepted
  composition-root borrows, not unauthorized ordinary callers.
- Physics `Cfg()` and `WorkerPool::Instance()` rows are owned by Phase 3 physics
  compatibility / future physics config-worker context.
- Replay helper rows are owned by Phase 5 replay compatibility / future replay
  services context.
- Profiler rows are either diagnostics exceptions or future frame/UI
  diagnostics context debt as recorded in the decision table.
