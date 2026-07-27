# Scene-Sized Store Capacity SC4 — Hot Stage Lists

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-sized-store-capacity` SC4

## Outcome

SC4 is complete. The 10 retained contact-solver rows, 7 narrowphase rows, 5
broadphase rows, and 2 mutual-gravity rows now use runtime-capacity
`PhysicsFixedList` storage. A shared `PhysicsStageCapacity.h` defines the body
(B), candidate-pair (P), contact (K), pipeline, collision-visual, and triangular
mutual-gravity bounds used by every converted owner.

`PhysicsWorld` commits those lists during SceneLoad through each concrete stage.
The fixed step clears, assigns, compacts, and appends only within retained
backing. `PhysicsFixedList` gained the vector-compatible `emplace_back` and
erase-range operations needed by broadphase without restoring an allocating
container.

## Fixed-Tick Contract

- The two persistent-contact reserves and the Debug contact reserve were
  deleted from `PersistentContactSolver`.
- Debug contact capacity is committed before the first tick, so overrun reaches
  the fixed-list/fatal capacity contract rather than reallocating.
- Candidate pairs retain their original enumeration and normalization order.
- Narrowphase disjoint-set rows borrow spans and preserve island compaction
  order.
- Mutual gravity retains model-order accumulation and the capped
  `choose2(min(B, 512))` triangular pair layout.
- Replay restore may grow converted lists only beneath the already-approved
  outer Replay owner and growth scope.
- Converted stage headers contain no retained `std::vector` members. The two
  remaining vector parameters in those headers are SC5-owned sleep/terrain and
  point-joint borrows.

Profile scene growth remains 86 unique owners. Debug correctly reports 89
because its three broadphase transition-oracle lists are compiled and committed
only in that configuration. Both configurations retain monotonic no-op behavior
when a later scene fits existing capacity.

## Allocation Policy

Stale vector and reserve exceptions were removed for `DisjointSet`, the
converted stage headers, and the deleted tick reserves. Replay snapshot copying
uses explicit bounded appends instead of `vector::assign`. The allocation-policy
self-test and repository scan report zero allowlist errors.

## Comment Audit

All 22 touched source-bearing files, including the new shared-capacity header,
were checked against the final implementation. Construction-reserve claims were
updated to scene-load reservation language, the borrowed disjoint-set contract
was corrected, and replay lifetime comments now describe governed backing.
Checked: 22. Deferred: 0. Unchecked: none. This was a touched-file audit, so no
subsystem checklist was required.

## Validation

- Converted-path reserve scan: PASS; no `reserve` remains in the contact,
  narrowphase, broadphase, or force stage paths or in
  `PersistentContactSolver.cpp`.
- Retained-member scan: PASS; no converted stage member remains a
  `std::vector`. SC5-owned borrowed parameters remain explicitly visible.
- Focused Debug scene-capacity doctest: PASS, 1 case / 4,114 assertions,
  including the exact 89-owner Debug contract.
- `python tools\check_allocation_policy.py --self-test` and `--repo .`: PASS;
  462 files scanned and zero allowlist errors.
- `tools\validate_fast.bat`: PASS; 410 cases / 2,406,382 assertions and zero
  project/filter, dependency, ownership, build, or test failures.
- `tools\validate_physics.bat`: PASS; Debug/Profile builds and byte-exact
  44,401-line deterministic physics regression.
- `tools\validate_perf.bat`: PASS; allocation, physics, selected-path, and DX12
  budgets accepted.
- `codegraph sync`: PASS; changed source and project files indexed before
  closure.

No baseline, golden, schema, scene, or configuration was refreshed.
