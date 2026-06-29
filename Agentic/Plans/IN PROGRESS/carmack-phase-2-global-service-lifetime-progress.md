# Carmack Phase 2 - Global Service Lifetime Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: `Phase 2 - Global Service Lifetime`

Current status: service-singleton cleanup slice in progress. This is not full
Phase 2 closure; broad render/config/worker/diagnostics rows remain.

Phase 0 input:
`Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
reports 593 classified hits: `normal runtime path` 223, `render pass` 163,
`asset lookup` 12, and `diagnostics` 79.

Current after-slice evidence:
`Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons-summary.md`
reports 579 classified hits: `normal runtime path` 216, `render pass` 156,
`asset lookup` 12, and `diagnostics` 79.

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

## Checklist

### Classification And Scope

- [x] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification.csv`
  from the final current source tree using the same matching logic as
  `tools/check_runtime_boundaries.py`.
  Evidence: `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons.csv`.
- [x] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification-summary.md`
  from that CSV and record the exact generation command in this file.
  Evidence: `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-after-service-singletons-summary.md`.
  Command: inline PowerShell here-string piped to `python -`; the script imports
  `tools/check_runtime_boundaries.py`, scans `SkullbonezSource/**/*.{cpp,h,hpp,inl}`
  with `GLOBAL_SERVICE_ACCESS_PATTERNS`,
  `GENERIC_INSTANCE_ACCESS_PATTERN`, `PROCESS_GLOBAL_POINTER_PATTERN`, and
  `MUTABLE_PROCESS_GLOBAL_PATTERN`, then classifies by `(file,label)` from the
  Phase 0 CSV.
- [x] Record the Phase 2 starting totals for `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` from the regenerated summary.
  Phase 0 totals: `normal runtime path` 223, `render pass` 163,
  `asset lookup` 12, `diagnostics` 79. After the service-singleton slice:
  `normal runtime path` 216, `render pass` 156, `asset lookup` 12,
  `diagnostics` 79.
- [ ] Audit every regenerated `normal runtime path`, `render pass`,
  `asset lookup`, and `diagnostics` row and record one decision per row:
  `remove now`, `borrow context now`, `authorized bootstrap/shutdown/callback`,
  `diagnostics exception`, `test/tool exception`, or `defer to Phase 3/4/5`.
- [ ] Record any rows intentionally deferred out of Phase 2 with the owning
  phase and file path, not a generic note.

### Render-Pass Backend Access

- [ ] Remove/change direct `Gfx()` render-pass access from
  `SkullbonezSource/Rendering/Helper.cpp` by routing material-table texture
  binding, instanced mesh creation/destroy, dynamic VB creation/destroy, and
  draw calls through borrowed `IRenderCommandContext` and
  `IRenderResourceFactory` services.
- [ ] Remove/change `CreateShaderFromActiveAssets()` in
  `SkullbonezSource/Rendering/Helper.cpp` by passing an explicit asset/shader
  creation context from runtime-owned services.
- [ ] Remove/change direct `Gfx()` and shader-factory access from
  `SkullbonezSource/Rendering/Text.cpp` by giving text rendering explicit
  render-resource and shader-creation services.
- [ ] Remove/change direct `Gfx()` and shader-factory access from
  `SkullbonezSource/UI/UI.cpp` and
  `SkullbonezSource/UI/UIBackdropBlur.cpp` through explicit UI render contexts.
- [ ] Audit `SkullbonezSource/Runtime/RunRender.cpp` direct `Gfx()` and
  `GfxRayTracing()` use. If it remains composition-root wiring, record that
  classification; otherwise move the borrow to a narrower runtime render
  context.
- [ ] Remove/change scene/runtime renderer setup access in
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, including `Gfx()`,
  `GfxRayTracing()`, and renderer-name/capability queries, through explicit
  runtime render services.
- [ ] Remove/change world render-resource globals in
  `SkullbonezSource/World/Terrain.cpp`,
  `SkullbonezSource/World/SkyBox.cpp`, and
  `SkullbonezSource/World/WorldEnvironment.cpp` through
  `RenderResourceContext`, `IRenderCommandContext`, or `EngineServices`.
- [ ] Remove/change editor transient render globals in
  `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` and
  `SkullbonezSource/Runtime/Editor/RunEditorTracer.inl` through explicit
  tool/render contexts, or record a diagnostics-only exception if retained.

### Asset, Texture, Camera, Window, Skybox Services

- [ ] Remove/change `ActiveAssetSystem()` and
  `CreateShaderFromActiveAssets()` compatibility access in
  `SkullbonezSource/Assets/AssetSystem.cpp` and
  `SkullbonezSource/Assets/AssetSystem.h`; replace normal-path callers with an
  explicit `AssetSystem` plus render-resource/shader factory context.
- [ ] Remove/change `TextureCollection::Instance()` and direct backend use in
  `SkullbonezSource/Assets/TextureCollection.cpp`; use the existing
  `BindRenderContexts(IRenderResourceFactory*, IRenderCommandContext*)` path and
  prove `m_renderResources`/`m_renderCommands` cover create, bind, and delete.
- [ ] Remove/change normal-path camera singleton access in
  `SkullbonezSource/Runtime/CameraCollection.cpp`,
  `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp`, and
  `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp`; pass
  `EngineServices::cameras` or a narrower scene camera context.
- [ ] Audit `SkullbonezSource/Runtime/Run.cpp` singleton setup rows for
  `TextureCollection::Instance()`, `CameraCollection::Instance()`,
  `Window::Instance()`, and `SkyBox::Instance()`. Reclassify true composition
  root/bootstrap rows, and remove any normal runtime lookup by borrowing from
  `RunSubsystemState`/`EngineServices`.
- [ ] Remove/change skybox singleton lifetime in
  `SkullbonezSource/World/SkyBox.cpp` and
  `SkullbonezSource/World/SkyBox.h`; keep skybox owned by runtime/world state
  rather than `pInstance`.
  Slice evidence: removed unused `SkyBox::Instance()`, `SkyBox::Destroy()`, and
  `pInstance`; Run already value-owns skybox through
  `RunSubsystemState::skyBoxOwner`. Remaining `SkyBox.cpp` render/config rows
  are deferred to the world render-resource context work.
- [ ] Audit `SkullbonezSource/Runtime/Window.cpp` for callback/window bridge
  rows. Keep only OS callback bridge or bootstrap access; route normal resize
  behavior through a bound window/render service.

### Worker, Config, And Profiler Services

- [ ] Group `Cfg()` rows by owner before changing code. Current likely clusters:
  physics (`SkullbonezSource/Physics/*`, `SkullbonezSource/GameObjects/GameModel.cpp`),
  runtime/input (`SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`), camera
  (`SkullbonezSource/Runtime/Camera.cpp`), scene
  (`SkullbonezSource/Runtime/Scene/RunScene.cpp`), and world
  (`SkullbonezSource/World/*`).
- [ ] Remove/change runtime-owned `Cfg()` access by passing config snapshots or
  explicit settings contexts, especially in
  `SkullbonezSource/Runtime/RunInput.cpp`,
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/Camera.cpp`, and
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`.
- [ ] Defer or coordinate physics `Cfg()` rows in
  `SkullbonezSource/Physics/PhysicsWorld.cpp`,
  `SkullbonezSource/Physics/PersistentContactSolver.cpp`,
  `SkullbonezSource/Physics/RigidBody.cpp`,
  `SkullbonezSource/Physics/BoundingSphere.cpp`, and
  `SkullbonezSource/GameObjects/GameModel.cpp` with Phase 3 before changing
  solver behavior.
- [ ] Remove/change `WorkerPool::Instance()` normal-path access in
  `SkullbonezSource/Physics/PhysicsWorld.cpp`,
  `SkullbonezSource/Rendering/GameModelRenderer.cpp`,
  `SkullbonezSource/Runtime/Replay/RunReplayPredictionHelpers.inl`,
  `SkullbonezSource/Runtime/RuntimeTuning.cpp`,
  `SkullbonezSource/Runtime/Scene/RunScene.cpp`, and
  `SkullbonezSource/Runtime/RunUiTextPass.cpp` through a borrowed worker
  service or explicit scheduling context.
- [ ] Remove/change direct `Profiler::Instance()` normal/runtime UI access in
  `SkullbonezSource/Runtime/RunFrame.cpp`,
  `SkullbonezSource/Runtime/RunUiTextPass.cpp`,
  `SkullbonezSource/Runtime/RuntimeDiagnostics.cpp`, and
  `SkullbonezSource/UI/UITabProfiler.cpp` through `DiagnosticsRuntime` or a
  diagnostics view. Record any macro-level exception separately.

### Guardrails And Records

- [x] Lower affected entries in
  `tools/check_runtime_boundaries.py` `GLOBAL_SERVICE_ACCESS_ALLOWLIST` after
  each removal.
  Evidence: removed the camera and skybox singleton/pInstance allowances from
  `tools/check_runtime_boundaries.py`; boundary checker reports 0 errors.
- [ ] Remove or narrow entries in
  `tools/check_runtime_boundaries.py`
  `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` when a file no longer owns a
  direct renderer-service compatibility role.
- [ ] Add or update synthetic self-tests in `tools/check_runtime_boundaries.py`
  for any new rejected pattern, especially renamed renderer, asset, worker, or
  diagnostics service access.
- [x] Run `python tools\check_runtime_boundaries.py` after guardrail edits and
  record the log path.
  Evidence:
  `TestOutput\validation\agent_logs\carmack_phase2_service_singletons_runtime_boundaries.log`;
  JSON:
  `TestOutput\validation\runtime_boundaries\carmack_phase2_service_singletons_runtime_boundaries.json`.
- [ ] Record before/after CSV and summary paths in this file, then copy final
  closure evidence back to the authoritative plan only when the phase is
  genuinely complete and the worker is allowed to update it.

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
  `Agentic\Reports\2026-06-29\carmack-phase-2-global-service-lifetime\global-service-hit-classification-after-service-singletons-summary.md`.
- A per-row or per-file decision table for all remaining `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` rows.
- Before/after `GLOBAL_SERVICE_ACCESS_ALLOWLIST` counts from
  `tools/check_runtime_boundaries.py`.
- `python tools\check_runtime_boundaries.py` output and log path after any
  guardrail update.
- Comment-style audit result for every touched source-bearing file: checked
  `CameraCollection.cpp`, `CameraCollection.h`, `SkyBox.cpp`, `SkyBox.h`, and
  `tools/check_runtime_boundaries.py`; 5 checked, 0 deferred.
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

This service-singleton source/tool slice touches `Runtime/*`, so the commit gate
is `tools\validate_full.bat`. Evidence:
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_validate_full.log`
reports `VALIDATE_FULL: DEFAULT GATE PASSED` and exit 0. A preliminary
`tools\validate_fast.bat` pass is in
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_validate_fast.log`.
The changed boundary checker was also run directly in
`TestOutput\validation\agent_logs\carmack_phase2_service_singletons_runtime_boundaries.log`.

## Open Risks And Questions

- The existing 2026-06-29 classification may be stale; Phase 2 should not close
  against it without regeneration.
- There is no standalone CSV generator command discovered in `tools/`; if Phase
  0 does not add one, the Phase 2 worker must record the exact one-off
  generation method used.
- `TextureCollection.cpp` already has bound render-context fields in
  `TextureCollection.h`, but the provisional CSV still reports direct `Gfx()`
  use. Confirm whether this is stale evidence or unfinished migration.
- Some `Run.cpp` global access may be legitimate composition-root/bootstrap
  wiring. Do not remove true ownership setup just to lower a count; classify it
  explicitly.
- Physics `Cfg()` and `WorkerPool::Instance()` rows may belong to Phase 3
  rather than Phase 2 if changing them would alter standalone physics behavior.
- `Profiler::Instance()` macro use may need a diagnostics exception or a broader
  diagnostics context plan; decide before touching macro-heavy headers.
