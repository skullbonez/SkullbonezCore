# 05 — Behavioral Test Coverage

Date: 2026-07-08
Status: Complete
Priority: P1
Owner: Test / all subsystems
Source issue: audit iss-10 (severity 4)

## Problem

Test effort is inverted: the code most likely to harbor bugs has the least
coverage, while enormous effort polices code *shape*.

Verified evidence:

- **59 `TEST_CASE`s across 19 files (~2,537 lines total)**, still clustered on
  math / geometry / parser / focused physics storage checks, but now including
  input-binding, replay-restore, physics-invariant, asset-library, terrain, and
  replay-boundary coverage.
- Zero unit assertions reference `RunFrame` / `RunInput` / `RunRender` /
  `RenderBackendDX12` / `UI`; the ~18K-line replay tree has focused recorder
  and solver-snapshot coverage plus replay solver-sample restore and physics
  invariant micro-world tests, but not a full loaded-artifact/Run restore test.
- **0 of 19** test files are pure link stubs. The former diagnostics, replay,
  scene-parser, and terrain link shims were deleted or replaced by real source
  links, focused unit coverage, or an explicitly named replay full-capture test
  boundary.
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
- [x] **Phase 1 — Input command table** (from plan 01) is directly unit-testable
  — add key→action mapping tests as that extraction lands.
- [x] **Phase 2 — Replay round-trip.** Record→restore determinism tests: a
  restored state must reproduce the recorded frames bit-for-bit.
- [x] **Phase 3 — Physics invariants.** Add property tests (no
  inter-penetration beyond tolerance, energy bounded under damping, sleep/wake
  transitions) *alongside* the byte-exact baselines, so a baseline change can be
  classified as intended vs regression.
- [x] **Phase 4 — Kill link stubs.** Convert each `*LinkStubs.cpp` into a real
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

  Completed 2026-07-09. Inventory at coverage-map time:
  - 52 `TEST_CASE`s across 20 `SkullbonezTests/*.cpp` files.
  - Four link-stub files remain: `TestDiagnosticsLinkStubs.cpp`,
    `TestReplayRecorderLinkStubs.cpp`, `TestSceneParserLinkStubs.cpp`, and
    `TestTerrainLinkStubs.cpp`.
  - High-risk source size is still concentrated in unlaunched or lightly covered
    runtime code: `Runtime` 148 files / 67,884 lines, `Runtime/Replay` 24 files /
    18,135 lines, `Physics` 53 files / 21,560 lines, `Rendering/DX12` 26 files /
    10,289 lines, `Scene` 5 files / 4,402 lines, and `Assets` 5 files /
    1,147 lines.
  - Focused test searches now cover the shared `InputController.Bindings`
    keyboard table. There are still no direct unit references to `RunFrame`,
    `RunInput`, `RunRender`, `RenderBackendDX12`, `IRenderCommandContext`, or
    `TakeInput`.

  Coverage target list:

  | Rank | Target | Risk x inverse-coverage reason | Current tests | Next step |
  |------|--------|--------------------------------|---------------|-----------|
  | 1 | Runtime input/frame dispatch (`RunInput`, `RunFrame`, `TakeInput`) | Large user-facing coordinator path, historically god-object heavy, and zero direct unit references. | `TestRuntimeInputBindings` now covers the shared keyboard binding data; full `RunInput` frame dispatch remains untested. | Step 1.1 complete; deeper Run-frame behavior belongs in future extracted helpers. |
  | 2 | Replay record -> restore behavior | Replay owns long-lived debugging and prediction state; regressions can look green in visual gates if state hashes are not compared. | `TestReplayRecorder` covers ring/cursor contracts; `TestDeterminism` now covers solver snapshot plus body restore and replay solver-sample restore of a recorded window. | Step 2.1 complete; full loaded-artifact/Run restore remains integration coverage. |
  | 3 | Physics solver invariants | `PhysicsWorld` and solver state remain determinism-critical and large; CSV baselines prove byte equality, not physical correctness. | `TestPhysicsHandles`, `TestSpatialGrid`, `TestBounds`, `TestConvexHull`, and `TestDeterminism` cover handles/broadphase/math/snapshot basics plus penetration tolerance, damping-energy, and sleep/wake invariant checks. | Step 3.1 complete; add future regression-specific invariants when solver behavior changes. |
  | 4 | DX12 render state / command abstraction | Barrier/resource-state regressions are high severity and mostly protected by screenshot/InfoQueue gates rather than unit behavior. | No unit reference to `RenderBackendDX12` or `IRenderCommandContext`; Plan 11 relies on `validate_dx12_renderer`. | Add a unit-testable resource-state/command-record helper only if a later render slice extracts one without initializing DX12. |
  | 5 | Scene and asset load boundaries | Scene parsing is externally supplied data and now owns Lane R recoverable failures; asset-library lookup also needs direct behavioral coverage. | `TestSceneParserUnit` has four parser tests; `TestAssetSystem` covers source lookup, logical ids/names, path resolution, and built-in asset-library registration. | Step 4.1 complete; future scene/load coverage should target integration fixtures or newly extracted helpers. |
  | 6 | Diagnostics, replay integration hooks, and terrain fixtures | Link-only shims hid missing test boundaries. Terrain and replay now have focused fixtures, and diagnostics link needs are satisfied by real source files. | `TestTerrain` covers the real flat-slope terrain contract; `TestReplayRecorderFullCaptureBoundary` covers solver mirror capture while naming the full-capture owner boundary; `FatalError.cpp` and `Log.cpp` are linked directly. | Step 4.1 complete; add behavior-specific diagnostics/replay/terrain tests when those paths change. |
  | 7 | Runtime allocation reserve policy | Allocation behavior is policy-sensitive but already has focused unit coverage. | `TestReserveAllocator` has five tests. | Maintain unless Plan 07 changes scope. |
  | 8 | Math, geometry, hulls, and simple bounds | Lower integration risk and comparatively well covered. | Vector, matrix, quaternion, geometric math, bounds, convex hull tests exist. | Add only regression-specific tests. |
- [x] **1.1** (with plan 01) Add input command-table tests — key+context →
  expected action. Gate: `validate_tests`. Commit.

  Completed 2026-07-09. The `RunInput` keyboard table moved into
  `InputController.Bindings`, preserving the same row order and dispatch
  consumers while exposing a static binding view for unit tests.
  `TestRuntimeInputBindings` asserts core keyboard shortcuts, contextual
  launcher/attached-camera/director rows, late/capture shortcut grouping, and
  uniqueness of each key+context pair. Gate evidence:
  `TestOutput\agent_logs\plan05_input_bindings_validate_tests_20260709_083801.log`
  (1.9s; project filters passed with 0 errors, `SKULLBONEZ_TESTS` Profile built
  with 0 warnings and 0 errors, and 51/51 doctest cases plus 1304/1304
  assertions passed). Because the shared table also touched `RunInput.cpp` and
  the production project, `tools\validate_full.bat` also passed in
  `TestOutput\agent_logs\plan05_input_bindings_validate_full_20260709_083509.log`
  (Profile/Debug builds passed, DX12 validation errors: 0, DX12 screenshots
  matched baselines, physics CSV matched byte-exactly).
- [x] **2.1** (with plan 09) Add replay record→restore determinism tests: a
  restored state reproduces the recorded frames bit-for-bit. Gate:
  `validate_tests`. Commit.

  Completed 2026-07-09. `TestDeterminism` now captures the micro-world into a
  `ReplaySolverFrameSample`, restores from that recorded sample, advances the
  same fixed-step window, recaptures the future sample, and compares replay body
  fields plus selected solver-world vectors byte-for-byte. The window stays
  pre-contact so this test isolates replay sample restore determinism; broader
  contact-cache invariants remain Phase 3 work. Gate evidence:
  `TestOutput\agent_logs\plan05_replay_restore_validate_tests_20260709_084459.log`
  (5.2s; project filters passed with 0 errors, `SKULLBONEZ_TESTS` Profile built
  with 0 warnings and 0 errors, and 52/52 doctest cases plus 1425/1425
  assertions passed).
- [x] **3.1** (with plan 02) Add physics invariant/property tests (no
  inter-penetration beyond tolerance, bounded energy under damping, sleep/wake)
  **alongside** the byte-exact baselines. Gate: `validate_tests`. Commit.

  Completed 2026-07-09. `TestDeterminism` now checks a settled micro-world
  against terrain penetration tolerance using both body bounds and terrain
  manifold diagnostics, verifies kinetic energy does not increase under
  no-gravity fluid damping, and proves an authored velocity wakes a sleeping
  body before the next integration step. Gate evidence:
  `TestOutput\agent_logs\plan05_physics_invariants_validate_tests_20260709_085357.log`
  (5.2s; project filters passed with 0 errors, `SKULLBONEZ_TESTS` Profile built
  with 0 warnings and 0 errors, and 55/55 doctest cases plus 1504/1504
  assertions passed).
- [x] **4.1** Convert each `*LinkStubs.cpp` into a real test or delete it. Gate:
  `validate_tests`. Commit.

  Completed 2026-07-09. Deleted `TestDiagnosticsLinkStubs.cpp`,
  `TestReplayRecorderLinkStubs.cpp`, `TestSceneParserLinkStubs.cpp`, and
  `TestTerrainLinkStubs.cpp`. Added focused coverage for asset-library source
  lookup/registration, real flat-slope terrain behavior, and the replay recorder
  solver-mirror/full-capture boundary. Shared render-resource doubles now let
  terrain-dependent unit tests link the real `Terrain.cpp`, and the test project
  links real `AssetSystem.cpp`, `Terrain.cpp`, `FatalError.cpp`, and `Log.cpp`
  instead of shim files. Current inventory: 59 `TEST_CASE`s across 19 test
  `.cpp` files, 0 `*LinkStubs.cpp` files. Gate evidence:
  `TestOutput\agent_logs\plan05_link_stubs_validate_tests_attempt3_20260709_0906.log`
  (5.09s; project filters passed with 0 errors, `SKULLBONEZ_TESTS` Profile
  built with 0 warnings and 0 errors, and 59/59 doctest cases plus 1532/1532
  assertions passed).

## Validation

`tools\validate_tests.bat` (build + console runner).

## Acceptance (measurable)

- [x] Unit tests exist that reference input dispatch, replay record/restore, and
  scene logic — not only math types.
- [x] Physics has invariant/property tests in addition to byte-exact baselines.
- [x] `*LinkStubs.cpp` count is 0.
- [x] The coverage map shows the top-risk subsystems moved off zero.
