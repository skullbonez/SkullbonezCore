# Global Service Hit Classification - Final Phase 2 Audit

Generated: `2026-06-29T15:28:03.652689+00:00`
HEAD: `b9323782`

## Method

Inline Python was piped to `python -` from PowerShell. The generator imported
`tools/check_runtime_boundaries.py` with `importlib`, scanned
`SkullbonezSource/**/*` for `.cpp`, `.h`, `.hpp`, and `.inl` files, and used the
checker-owned `GLOBAL_SERVICE_ACCESS_PATTERNS`,
`GENERIC_INSTANCE_ACCESS_PATTERN`, `PROCESS_GLOBAL_POINTER_PATTERN`,
`MUTABLE_PROCESS_GLOBAL_PATTERN`, `strip_cpp_comments_and_string_literals()`,
and `line_for_offset()` matching logic. Classification was preserved from
`global-service-hit-classification-after-service-singletons.csv` by `(file,label)`
with deterministic fallback rules for any new group.

## Output

- CSV: `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-classification-final.csv`
- Decision table CSV: `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.csv`
- Decision table report: `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.md`

## Totals

Total current hits: `579`
Current `(file,label)` groups: `102`
Allowlist groups after Phase 2 ratchet: `102`
Stale allowlist groups: `0`
Over-budget groups: `0`

| classification | current_hits | phase0_hits |
| --- | --- | --- |
| OS callback bridge | 56 | 56 |
| asset lookup | 12 | 12 |
| bootstrap | 30 | 30 |
| diagnostics | 79 | 79 |
| normal runtime path | 216 | 223 |
| render pass | 156 | 163 |
| test/tool | 30 | 30 |

## Classification By Label

| classification | label | hits |
| --- | --- | --- |
| OS callback bridge | Cfg() | 6 |
| OS callback bridge | Gfx() | 1 |
| OS callback bridge | Window::Instance() | 1 |
| OS callback bridge | g_* | 43 |
| OS callback bridge | pInstance | 5 |
| asset lookup | ActiveAssetSystem() | 2 |
| asset lookup | CreateShaderFromActiveAssets() | 2 |
| asset lookup | Gfx() | 2 |
| asset lookup | TextureCollection::Instance() | 1 |
| asset lookup | g_* | 5 |
| bootstrap | Cfg() | 18 |
| bootstrap | EngineConfig::Instance() | 2 |
| bootstrap | Window::Instance() | 1 |
| bootstrap | WorkerPool::Instance() | 3 |
| bootstrap | g_* | 6 |
| diagnostics | CreateShaderFromActiveAssets() | 1 |
| diagnostics | Gfx() | 28 |
| diagnostics | LockOrderValidator::Instance() | 5 |
| diagnostics | Profiler::Instance() | 21 |
| diagnostics | g_* | 24 |
| normal runtime path | Cfg() | 164 |
| normal runtime path | CreateShaderFromActiveAssets() | 2 |
| normal runtime path | Gfx() | 25 |
| normal runtime path | GfxRayTracing() | 1 |
| normal runtime path | Profiler::Instance() | 4 |
| normal runtime path | WorkerPool::Instance() | 12 |
| normal runtime path | g_* | 8 |
| render pass | Cfg() | 45 |
| render pass | CreateShaderFromActiveAssets() | 9 |
| render pass | Gfx() | 94 |
| render pass | GfxRayTracing() | 3 |
| render pass | Profiler::Instance() | 2 |
| render pass | WorkerPool::Instance() | 3 |
| test/tool | Cfg() | 6 |
| test/tool | CreateShaderFromActiveAssets() | 1 |
| test/tool | Gfx() | 23 |

## Phase 2 Target Audit Coverage

Target classifications audited: `normal runtime path`, `render pass`, `asset lookup`, `diagnostics`.
Audited target hits: `463`
Audited target groups: `81`

| phase2_status | hits |
| --- | --- |
| accepted compatibility debt | 118 |
| deferred | 345 |

## Decision Summary

| decision | groups | hits |
| --- | --- | --- |
| accepted asset texture compatibility debt | 1 | 1 |
| accepted asset/shader compatibility debt | 6 | 11 |
| accepted backend accessor compatibility | 4 | 7 |
| accepted composition-root borrow | 6 | 22 |
| accepted service-owner compatibility | 2 | 10 |
| diagnostics exception | 13 | 79 |
| owned by Phase 3 physics compatibility | 6 | 64 |
| owned by Phase 5 replay compatibility | 4 | 8 |
| owned by future UI render context | 2 | 17 |
| owned by future UI text render/diagnostics context | 3 | 4 |
| owned by future camera settings context | 2 | 23 |
| owned by future frame diagnostics/settings context | 3 | 11 |
| owned by future render settings context | 2 | 10 |
| owned by future render-resource context | 7 | 78 |
| owned by future renderer worker/config context | 2 | 5 |
| owned by future runtime input/settings context | 2 | 26 |
| owned by future runtime settings context | 1 | 1 |
| owned by future scene render-resource context | 2 | 10 |
| owned by future scene runtime services context | 2 | 19 |
| owned by future tuning/worker service context | 2 | 2 |
| owned by future world render-resource context | 9 | 55 |

## Guardrail Ratchet

- Removed stale `GLOBAL_SERVICE_ACCESS_ALLOWLIST` entries for
  `SkullbonezSource/UI/UIBackdropBlur.cpp` labels
  `CreateShaderFromActiveAssets()` and `Gfx()` because the current source has
  zero matching rows.
- Removed the stale `GLOBAL_RENDERER_SERVICE_ACCESS_CLASSIFICATIONS` row for
  `SkullbonezSource/UI/UIBackdropBlur.cpp`; remaining `UI.cpp` render globals
  stay classified as future UI render-context debt.
- After the ratchet, live groups and allowlist groups both equal `102`;
  stale allowlist groups and over-budget groups are both zero.

## Interpretation

Phase 2 closure is an audit-and-ratchet closure, not a claim that global services
were eliminated. Remaining globals are retained as counted compatibility debt
with concrete owners in the decision table. Large migrations for config,
renderer resources, worker scheduling, asset/shader creation, and diagnostics
are intentionally deferred because they cross render, physics, replay, UI, and
scene lifecycle validation boundaries.
