# Global Service Hit Classification Summary

Generated from current source using `tools/check_runtime_boundaries.py` matching logic and the Carmack global-service classification table.

Regenerated: 2026-06-29 23:46:59 local time.

Total classified hits: 593

## Totals By Classification

| Classification | Count |
|----------------|-------|
| OS callback bridge | 56 |
| asset lookup | 12 |
| bootstrap | 30 |
| diagnostics | 79 |
| normal runtime path | 223 |
| render pass | 163 |
| test/tool | 30 |

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
| normal runtime path | `CameraCollection::Instance()` | 1 |
| normal runtime path | `Cfg()` | 164 |
| normal runtime path | `CreateShaderFromActiveAssets()` | 2 |
| normal runtime path | `Gfx()` | 25 |
| normal runtime path | `GfxRayTracing()` | 1 |
| normal runtime path | `Profiler::Instance()` | 4 |
| normal runtime path | `WorkerPool::Instance()` | 12 |
| normal runtime path | `g_*` | 8 |
| normal runtime path | `pInstance` | 6 |
| render pass | `Cfg()` | 45 |
| render pass | `CreateShaderFromActiveAssets()` | 9 |
| render pass | `Gfx()` | 94 |
| render pass | `GfxRayTracing()` | 3 |
| render pass | `Profiler::Instance()` | 2 |
| render pass | `SkyBox::Instance()` | 1 |
| render pass | `WorkerPool::Instance()` | 3 |
| render pass | `pInstance` | 6 |
| test/tool | `Cfg()` | 6 |
| test/tool | `CreateShaderFromActiveAssets()` | 1 |
| test/tool | `Gfx()` | 23 |

## Reconciliation Notes

- `SkullbonezSource/Assets/TextureCollection.cpp` now reports no direct `Gfx()` hits; texture create/delete/bind paths use the borrowed `m_renderResources` and `m_renderCommands` contexts.
- Remaining `SkullbonezSource/Assets/TextureCollection.cpp` hits: 1 total, including 1 `TextureCollection::Instance()` hit(s).
- This report is evidence-only; it does not change runtime-boundary allowlists or approve remaining normal-path debt.
