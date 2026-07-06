# Authoritative Plan 02: Physics Store Authority

Date: 2026-07-06
Status: Active authoritative plan
CSV: `Agentic/Plans/In_Progress/authoritative-plan-02-physics-store-authority.csv`
Impact area: physics, GameModelCollection, scene identity, replay restore, editor/tools, render projection
Validation for this documentation-only change: none required

## Goal

Make physics, collider, render-instance, replay identity, and diagnostics stores
the authoritative runtime data owners. `GameModelCollection` may remain as a
temporary model-order compatibility facade, but it must stop being the world
data source of truth.

## Non-Goals

- Do not change solver math while moving authority.
- Do not change physics baselines except through explicit behavior work.
- Do not hide model-order dependencies behind new generic compatibility names.
- Do not put callbacks or service interfaces in hot solver loops.

## First-Night Slice

1. Tighten `tools/check_runtime_boundaries.py` so new physics/runtime files
   cannot add `GameModelCollection` authority or model-index repair paths.
2. Move one cold edge from the CSV to explicit store/handle authority.
3. Update the CSV row status and include owner, reason, deletion condition, and
   checker budget in code comments or commit notes.

## Definition Of Done

- Runtime physics commands enter through body/collider handles.
- Scene identity allocation belongs to scene state and stored body rows.
- Render projection consumes body/collider/render stores and a narrow
  presentation sidecar, not `GameModel` directly.
- `GameModelCollection` no longer exposes broad `GetPhysicsEngine()`,
  model-count, body-store, collider-store, or topology repair authority to
  unrelated runtime features.

## Validation

Physics authority slices require `tools\validate_physics.bat`. Storage layout,
hot-path, or allocation-sensitive changes also require `tools\validate_perf.bat`.
Broad runtime integration uses `tools\validate_full.bat`.

