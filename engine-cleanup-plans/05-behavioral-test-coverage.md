# 05 — Behavioral Test Coverage

Date: 2026-07-08
Status: Proposed
Priority: P1
Owner: Test / all subsystems
Source issue: audit iss-10 (severity 4)

## Problem

Test effort is inverted: the code most likely to harbor bugs has the least
coverage, while enormous effort polices code *shape*.

Verified evidence:

- **44 `TEST_CASE`s across 19 files (~2,455 lines total)**, clustered on pure
  math (`Vector3`/`Matrix4`/`Quaternion`/`Bounds`).
- Zero unit assertions reference `RunFrame` / `RunInput` / `RunRender` /
  `RenderBackendDX12` / `UI`; the ~17K-line replay tree has only
  `TestReplayRecorder`.
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

- [ ] **Phase 0 — Coverage map.** Rank subsystems by (risk × inverse-coverage);
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

- [ ] **0.1** Coverage map: rank subsystems by (risk × inverse-coverage) and
  write the target list here. No code change. Commit.
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
