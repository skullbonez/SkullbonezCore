# 05 — Behavioral Test Coverage

Date: 2026-07-08
Status: In Progress
Priority: P1
Owner: Test / all subsystems
Source issue: audit iss-10 (severity 4)

## Problem

Test effort is inverted: the code most likely to harbor bugs has the least
coverage, while enormous effort polices code *shape*.

Verified evidence:

- **47 `TEST_CASE`s across 19 files (~2,194 lines total)**, still clustered on
  math / geometry / parser / focused physics storage checks.
- Zero unit assertions reference `RunFrame` / `RunInput` / `RunRender` /
  `RenderBackendDX12` / `UI`; the ~18K-line replay tree has focused recorder
  and solver-snapshot coverage, but not a full record->restore behavioral test.
- **4 of 19** test files are pure link stubs
  ([TestDiagnosticsLinkStubs.cpp](../SkullbonezTests/TestDiagnosticsLinkStubs.cpp),
  `TestReplayRecorderLinkStubs.cpp`, `TestSceneParserLinkStubs.cpp`,
  `TestTerrainLinkStubs.cpp`).
- The DX12 net is 2 scenes / 2 PNG baselines; physics rests on byte-exact CSV
  baselines whose prescribed remedy ("regenerate, then re-run") structurally
  cannot distinguish an intended change from a regression — the regenerated
  numbers *become* the oracle, silently baking any co-introduced regression
  green.

## Goal

Add behavioral coverage to the highest-risk untested subsystems, and add
invariant/property checks to physics so intended changes are distinguishable
from bugs (not only byte-exact goldens).

## Approach

- [x] **Phase 0 — Coverage map.** Rank subsystems by (risk × inverse-coverage);
  publish the target list.
- [ ] **Phase 1 — Input command table** (from plan 01) is directly unit-testable
  — add key→action mapping tests as that extraction lands.
- [ ] **Phase 2 — Replay round-trip.** Record→restore determinism tests: a
  restored state must reproduce the recorded frames bit-for-bit.
- [ ] **Phase 3 — Physics invariants.** Add property tests (no
  inter-penetration beyond tolerance, energy bounded under damping, sleep/wake
  transitions) *alongside* the byte-exact baselines, so a baseline change can be
  classified as intended vs regression.
- [ ] **Phase 4 — Kill link stubs.** Convert each `*LinkStubs.cpp` into a real
  test or delete it.

## Risks

- Behavioral tests on `Run*`/DX12 may need a headless harness; reuse the
  existing interaction-automation and capture paths rather than building new
  infrastructure.

## Step-by-step implementation

This plan is **continuous**: do 0.1 first, then add each test as its paired plan
lands. Adding a test file means editing `SKULLBONEZ_TESTS.vcxproj` and
`.filters`. Gate every step with `validate_tests`; commit per step.

- [x] **0.1** Coverage map: rank subsystems by (risk × inverse-coverage) and
  write the target list here. No code change. Commit.

  Completed 2026-07-09. Current inventory from tracked files:
  - 47 `TEST_CASE`s across 19 `SkullbonezTests/*.cpp` files.
  - Four link-stub files remain: `TestDiagnosticsLinkStubs.cpp`,
    `TestReplayRecorderLinkStubs.cpp`, `TestSceneParserLinkStubs.cpp`, and
    `TestTerrainLinkStubs.cpp`.
  - High-risk source size is still concentrated in unlaunched or lightly covered
    runtime code: `Runtime` 146 files / 67,803 lines, `Runtime/Replay` 24 files /
    18,135 lines, `Physics` 53 files / 21,560 lines, `Rendering/DX12` 26 files /
    10,289 lines, `Scene` 5 files / 4,402 lines, and `Assets` 5 files /
    1,147 lines.
  - Focused test searches show no unit references to `RunFrame`, `RunInput`,
    `RunRender`, `RenderBackendDX12`, `IRenderCommandContext`, or `TakeInput`.

  Coverage target list:

  | Rank | Target | Risk x inverse-coverage reason | Current tests | Next step |
  |------|--------|--------------------------------|---------------|-----------|
  | 1 | Runtime input/frame dispatch (`RunInput`, `RunFrame`, `TakeInput`) | Large user-facing coordinator path, historically god-object heavy, and zero direct unit references. | None. | Step 1.1: key + context to expected action tests for the extracted command table. |
  | 2 | Replay record -> restore behavior | Replay owns long-lived debugging and prediction state; regressions can look green in visual gates if state hashes are not compared. | `TestReplayRecorder` covers ring/cursor contracts; `TestDeterminism` covers solver snapshot plus body restore, not the full record -> restore frame path. | Step 2.1: record a small deterministic window, restore it, and compare reproduced frame/body hashes bit-for-bit. |
  | 3 | Physics solver invariants | `PhysicsWorld` and solver state remain determinism-critical and large; CSV baselines prove byte equality, not physical correctness. | `TestPhysicsHandles`, `TestSpatialGrid`, `TestBounds`, `TestConvexHull`, and `TestDeterminism` cover handles/broadphase/math/snapshot basics. | Step 3.1: add property-style micro-world checks for penetration tolerance, bounded damping energy, and sleep/wake transitions. |
  | 4 | DX12 render state / command abstraction | Barrier/resource-state regressions are high severity and mostly protected by screenshot/InfoQueue gates rather than unit behavior. | No unit reference to `RenderBackendDX12` or `IRenderCommandContext`; Plan 11 relies on `validate_dx12_renderer`. | Add a unit-testable resource-state/command-record helper only if a later render slice extracts one without initializing DX12. |
  | 5 | Scene and asset load boundaries | Scene parsing is externally supplied data and now owns Lane R recoverable failures; asset-library lookup remains hidden behind a link stub. | `TestSceneParserUnit` has four parser tests. | Step 4.1: replace `TestSceneParserLinkStubs.cpp` with real asset lookup coverage or a narrow fixture owner. |
  | 6 | Diagnostics, replay integration hooks, and terrain fixtures | Stubs satisfy linking and loudly fail, but they are not behavioral assertions. | `TestDiagnosticsLinkStubs.cpp`, `TestReplayRecorderLinkStubs.cpp`, and `TestTerrainLinkStubs.cpp` still exist. | Step 4.1: turn each stub into a real fixture/test boundary or delete it by narrowing dependencies. |
  | 7 | Runtime allocation reserve policy | Allocation behavior is policy-sensitive but already has focused unit coverage. | `TestReserveAllocator` has five tests. | Maintain unless Plan 07 changes scope. |
  | 8 | Math, geometry, hulls, and simple bounds | Lower integration risk and comparatively well covered. | Vector, matrix, quaternion, geometric math, bounds, convex hull tests exist. | Add only regression-specific tests. |
- [ ] **1.1** (with plan 01) Add input command-table tests — key+context →
  expected action. Gate: `validate_tests`. Commit.
- [ ] **2.1** (with plan 09) Add replay record→restore determinism tests: a
  restored state reproduces the recorded frames bit-for-bit. Gate:
  `validate_tests`. Commit.
- [ ] **3.1** (with plan 02) Add physics invariant/property tests (no
  inter-penetration beyond tolerance, bounded energy under damping, sleep/wake)
  **alongside** the byte-exact baselines. Gate: `validate_tests`. Commit.
- [ ] **4.1** Convert each `*LinkStubs.cpp` into a real test or delete it. Gate:
  `validate_tests`. Commit.

## Validation

`tools\validate_tests.bat` (build + console runner).

## Acceptance (measurable)

- [ ] Unit tests exist that reference input dispatch, replay record/restore, and
  scene logic — not only math types.
- [ ] Physics has invariant/property tests in addition to byte-exact baselines.
- [ ] `*LinkStubs.cpp` count is 0.
- [ ] The coverage map shows the top-risk subsystems moved off zero.
