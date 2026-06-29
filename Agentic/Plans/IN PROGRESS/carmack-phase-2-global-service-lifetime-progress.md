# Carmack Phase 2 - Global Service Lifetime Progress

Source plan: `Agentic/Plans/IN PROGRESS/carmack-remaining-work-authoritative-plan.md`

Assigned phase: `Phase 2 - Global Service Lifetime`

Current status: progress scaffold created only. No source, tool, validation, or
authoritative-plan files have been changed. Phase 2 should not start source
edits until Phase 0 regenerates the global-service classification from the final
current source tree.

Current provisional input: `Agentic/Reports/2026-06-29/carmack-handoff/global-service-hit-classification-summary.md`
reports 611 classified hits. Treat this as handoff context, not final evidence,
because the authoritative plan requires regeneration before closure.

## Checklist

### Classification And Scope

- [ ] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification.csv`
  from the final current source tree using the same matching logic as
  `tools/check_runtime_boundaries.py`.
- [ ] Regenerate `Agentic/Reports/<date>/carmack-phase-2-global-service-lifetime/global-service-hit-classification-summary.md`
  from that CSV and record the exact generation command in this file.
- [ ] Record the Phase 2 starting totals for `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` from the regenerated summary.
  The 2026-06-29 provisional summary shows:
  `normal runtime path`: 271 hits, `render pass`: 166 hits,
  `asset lookup`: 17 hits, `diagnostics`: 92 hits.
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

- [ ] Lower affected entries in
  `tools/check_runtime_boundaries.py` `GLOBAL_SERVICE_ACCESS_ALLOWLIST` after
  each removal.
- [ ] Remove or narrow entries in
  `tools/check_runtime_boundaries.py`
  `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` when a file no longer owns a
  direct renderer-service compatibility role.
- [ ] Add or update synthetic self-tests in `tools/check_runtime_boundaries.py`
  for any new rejected pattern, especially renamed renderer, asset, worker, or
  diagnostics service access.
- [ ] Run `python tools\check_runtime_boundaries.py` after guardrail edits and
  record the log path.
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

- Regenerated classification CSV path and summary path.
- A per-row or per-file decision table for all remaining `normal runtime path`,
  `render pass`, `asset lookup`, and `diagnostics` rows.
- Before/after `GLOBAL_SERVICE_ACCESS_ALLOWLIST` counts from
  `tools/check_runtime_boundaries.py`.
- `python tools\check_runtime_boundaries.py` output and log path after any
  guardrail update.
- Comment-style audit result for every touched source-bearing file.
- Validation logs selected by touched area: `tools\validate_dx12_renderer.bat`
  for renderer/render-context changes, `tools\validate_physics.bat` for physics
  config or worker changes, `tools\validate_perf.bat` for hot-path worker or
  allocation-sensitive changes, and `tools\validate_full.bat` at final
  PR/commit handoff.

## Validation Note

This progress file is documentation-only, so no repository validation script is
required for creating it. Future source or tool changes must follow the
validation map in `AGENTS.md`; do not claim validation success without command
output and log paths.

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
