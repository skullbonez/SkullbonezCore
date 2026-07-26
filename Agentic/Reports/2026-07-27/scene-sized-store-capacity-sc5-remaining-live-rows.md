# Scene-Sized Store Capacity SC5 — Remaining Live Rows

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `../../Plans/TODO/scene-sized-store-capacity.md`

## Outcome

SC5 is complete. The 18 retained `PhysicsSleepController` rows, 2
`PhysicsTerrainStage` rows, 3 `PhysicsStepDiagnostics` rows, 2 `PhysicsWorld`
rows, and `PhysicsEngine::m_authoredBodyDescs` now use runtime-capacity
`PhysicsFixedList` storage. `PhysicsWorld` commits each owner during SceneLoad
from the exact body, contact, or pipeline capacity established by the scene.

The converted APIs expose borrowed spans where callers only inspect rows.
Replay snapshots remain cold owning vectors, but capture and restore now copy
through the fixed-list boundary and cannot enlarge live backing after the
scene-load commit.

## Preserved Semantics

- `PhysicsWorld::m_timeRemaining` remains the cross-stage continuous-collision
  clock, with the same row initialization and update order.
- Point-joint insertion, generation validation, removal compaction, and handle
  behavior are unchanged.
- Sleep support edges, union-find rows, wake queues, visual island IDs, and
  per-body flags preserve their existing enumeration and mutation order.
- Terrain detection candidates and committed manifolds preserve model and
  contact order.
- Debug collision rows and pipeline trace rows remain configuration-sensitive
  and are committed before the fixed tick.
- The exact sleep-island visual-ID memory subtotal remains separately
  observable for existing diagnostics.

Profile scene growth is now 89 unique owners. Debug reports 92 because its
three configuration-only broadphase transition-oracle owners remain present.
Both configurations retain monotonic no-op behavior when a later scene fits
the already committed backing.

## Fixed-Tick And Allocation Contract

No converted owner retains a `std::vector` member. The sole `std::vector`
occurrence in the converted owner headers is the cold
`PhysicsEngine::LoadBodyDescriptors` input parameter, not retained storage.
Replay snapshot data remains under its SC0 cold/unbounded ruling.

Calls named `assign`, `resize`, `push_back`, and `emplace_back` on the converted
live rows are `PhysicsFixedList` operations. They mutate only within
scene-committed backing and fail loudly on overflow; they are not raw-vector
growth. Stale vector and reserve allowlist rows were removed, and the explicit
cold replay copy sites remain registered.

## Comment Audit

All 39 touched source-bearing files were inspected against the final
implementation and the repository comment-style guide. Every file has a
learning header. Construction-reserve claims were corrected to scene-load
commit language, the support-edge overflow hazard now names the fixed-list
contract, and ownership, sequencing, replay lifetime, CCD, point-joint, sleep,
terrain, and diagnostics claims were verified against their post-change call
paths. Checked: 39. Deferred: 0. Unchecked: none. This was a touched-file audit,
so no subsystem checklist was required. No term needs human-approved wording.

## Validation

- Converted-owner retained-member scan: PASS; no converted live member remains
  a `std::vector`.
- Focused Debug scene-capacity doctest: PASS, 1 case / 4,393 assertions,
  including the exact 92-owner Debug contract.
- `python tools\check_allocation_policy.py --repo .`: PASS; 462 files scanned
  and zero allowlist errors.
- `tools\validate_fast.bat`: PASS; 410 cases / 2,406,661 assertions and zero
  formatting, project/filter, dependency, ownership, build, or test failures.
- `tools\validate_physics.bat`: PASS; Debug/Profile builds and byte-exact
  44,401-line deterministic physics regression.
- `tools\validate_physics_deep.bat`: PASS; all deep physics and diagnostics
  phases accepted.
- `tools\validate_perf.bat`: PASS; allocation, physics, selected-path, and DX12
  budgets accepted.
- `codegraph sync`: PASS; changed source and project files indexed before
  closure.

No baseline, golden, schema, scene, or configuration was refreshed.
