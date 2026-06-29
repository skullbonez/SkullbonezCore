# Global Service Hit Classification Summary

Generated from current source using `tools/check_runtime_boundaries.py` matching logic and the Carmack global-service classification table.

Total classified hits: 611

| Classification | Label | Count |
|----------------|-------|-------|
| OS callback bridge | `Cfg()` | 6 |
| OS callback bridge | `Gfx()` | 1 |
| OS callback bridge | `Window::Instance()` | 6 |
| OS callback bridge | `g_*` | 43 |
| OS callback bridge | `pInstance` | 5 |
| asset lookup | `ActiveAssetSystem()` | 2 |
| asset lookup | `CreateShaderFromActiveAssets()` | 2 |
| asset lookup | `Gfx()` | 5 |
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
| normal runtime path | `CameraCollection::Instance()` | 4 |
| normal runtime path | `Cfg()` | 164 |
| normal runtime path | `CreateShaderFromActiveAssets()` | 2 |
| normal runtime path | `Gfx()` | 25 |
| normal runtime path | `GfxRayTracing()` | 1 |
| normal runtime path | `Profiler::Instance()` | 4 |
| normal runtime path | `SkyBox::Instance()` | 1 |
| normal runtime path | `TextureCollection::Instance()` | 1 |
| normal runtime path | `Window::Instance()` | 1 |
| normal runtime path | `WorkerPool::Instance()` | 12 |
| normal runtime path | `g_*` | 8 |
| normal runtime path | `pInstance` | 7 |
| render pass | `Cfg()` | 45 |
| render pass | `CreateShaderFromActiveAssets()` | 9 |
| render pass | `Gfx()` | 94 |
| render pass | `GfxRayTracing()` | 3 |
| render pass | `Profiler::Instance()` | 2 |
| render pass | `SkyBox::Instance()` | 1 |
| render pass | `TextureCollection::Instance()` | 1 |
| render pass | `WorkerPool::Instance()` | 3 |
| render pass | `pInstance` | 8 |
| test/tool | `Cfg()` | 6 |
| test/tool | `CreateShaderFromActiveAssets()` | 1 |
| test/tool | `Gfx()` | 23 |
