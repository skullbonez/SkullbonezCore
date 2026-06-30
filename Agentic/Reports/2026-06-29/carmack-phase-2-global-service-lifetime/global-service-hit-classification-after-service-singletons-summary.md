# Phase 2 Global Service Hit Classification Summary

Generated from current source using `tools/check_runtime_boundaries.py` matching logic and the Phase 0 classification table.

Regenerated: 2026-06-30 00:15:45 local time.

Total classified hits: 579

Generation command:

```powershell
@'...'@ | python -  # inline scanner imported tools/check_runtime_boundaries.py patterns and Phase 0 CSV classifications
```

## Phase 2 Delta From Phase 0 Snapshot

| Classification | Phase 0 | After service-singleton slice | Delta |
|----------------|---------|-------------------------------|-------|
| OS callback bridge | 56 | 56 | +0 |
| asset lookup | 12 | 12 | +0 |
| bootstrap | 30 | 30 | +0 |
| diagnostics | 79 | 79 | +0 |
| normal runtime path | 223 | 216 | -7 |
| render pass | 163 | 156 | -7 |
| test/tool | 30 | 30 | +0 |

## Phase 2 Scope Counts

| Classification | Count |
|----------------|-------|
| normal runtime path | 216 |
| render pass | 156 |
| asset lookup | 12 |
| diagnostics | 79 |

## Totals By Classification And Label

| Classification | Label | Count |
|----------------|-------|-------|
| OS callback bridge | `Cfg()` | 6 |
| OS callback bridge | `Gfx()` | 1 |
| OS callback bridge | `Window::Instance()` | 1 |
| OS callback bridge | `g_*` | 43 |
| OS callback bridge | `pInstance` | 5 |
| asset lookup | `ActiveAssetSystem()` | 2 |
| asset lookup | `CreateShaderFromActiveAssets()` | 2 |
| asset lookup | `Gfx()` | 2 |
| asset lookup | `TextureCollection::Instance()` | 1 |
| asset lookup | `g_*` | 5 |
| bootstrap | `Cfg()` | 18 |
| bootstrap | `EngineConfig::Instance()` | 2 |
| bootstrap | `Window::Instance()` | 1 |
| bootstrap | `WorkerPool::Instance()` | 3 |
| bootstrap | `g_*` | 6 |
| diagnostics | `CreateShaderFromActiveAssets()` | 1 |
| diagnostics | `Gfx()` | 28 |
| diagnostics | `LockOrderValidator::Instance()` | 5 |
| diagnostics | `Profiler::Instance()` | 21 |
| diagnostics | `g_*` | 24 |
| normal runtime path | `Cfg()` | 164 |
| normal runtime path | `CreateShaderFromActiveAssets()` | 2 |
| normal runtime path | `Gfx()` | 25 |
| normal runtime path | `GfxRayTracing()` | 1 |
| normal runtime path | `Profiler::Instance()` | 4 |
| normal runtime path | `WorkerPool::Instance()` | 12 |
| normal runtime path | `g_*` | 8 |
| render pass | `Cfg()` | 45 |
| render pass | `CreateShaderFromActiveAssets()` | 9 |
| render pass | `Gfx()` | 94 |
| render pass | `GfxRayTracing()` | 3 |
| render pass | `Profiler::Instance()` | 2 |
| render pass | `WorkerPool::Instance()` | 3 |
| test/tool | `Cfg()` | 6 |
| test/tool | `CreateShaderFromActiveAssets()` | 1 |
| test/tool | `Gfx()` | 23 |

## Reconciliation Notes

- Removed unused `CameraCollection::Instance()` / `CameraCollection::Destroy()` and `pInstance` storage from `SkullbonezSource/Runtime/CameraCollection.cpp` and `SkullbonezSource/Runtime/CameraCollection.h`.
- Removed unused `SkyBox::Instance()` / `SkyBox::Destroy()` and `pInstance` storage from `SkullbonezSource/World/SkyBox.cpp` and `SkullbonezSource/World/SkyBox.h`.
- Lowered the matching `GLOBAL_SERVICE_ACCESS_ALLOWLIST` entries in `tools/check_runtime_boundaries.py` so the removed singleton rows cannot return silently.
- Remaining `CameraCollection` global-service row is the existing `Cfg()` read for `minCameraHeight`; it is deferred to the broader camera config-context slice because `SkullbonezSource/Runtime/Camera.cpp` still owns the same config dependency.
- Remaining `SkyBox` render-pass rows are renderer/config/shader factory access in `SkyBox.cpp`; those belong to the broader render-resource context work instead of the dead singleton cleanup.
