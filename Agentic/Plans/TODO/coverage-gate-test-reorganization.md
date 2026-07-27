# Coverage Gate Test Reorganization

Date: 2026-07-26
Status: IN PROGRESS — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 10 of the Architecture Follow-Up Campaign
Round 5. 1/3 phases complete; CG1 is binding.
Impact area: `SkullbonezTests/TestCoverageFloorContracts.cpp`,
`tools/coverage_floors.json`, `tools/check_coverage.py`, `SKULLBONEZ_TESTS.vcxproj`
Owner: test
Priority: Low-Medium — the tests inside are honest and behavioral. The problem is
that a file organized around the *metric* teaches the next agent to write tests
for the gate instead of for the subsystem.

## Problem And Evidence (measured 2026-07-26)

`SkullbonezTests/TestCoverageFloorContracts.cpp` (549 lines) states its own
purpose as:

> Exercise high-value physics and replay owner contracts needed by the armed
> coverage gate.

Its organizing principle is the gate, not a subsystem. The file's includes span
`Physics/BoundingBox.h`, `BoundingSphere.h`, `ConvexHullShape.h`, `ColliderStore.h`,
`ObjectContactManifold.h`, `PhysicsApi.h`, `PhysicsBodyStore.h`, `PhysicsEngine.h`,
`PhysicsWorldForces.h`, `TerrainContactManifold.h`, `Assets/AssetSystem.h`,
`Core/Config.h`, `Runtime/Replay/ReplayV2Artifact.h`, `ReplayTimeline.h`,
`Runtime/Scene/SceneEntityStore.h`, and `World/Terrain.h` — sixteen headers across
six subsystems in one translation unit, bound together only by which lines the
coverage instrumentation needed to reach.

To the author's credit the contents are good: the header's Invariants section
commits to "assert finite, directionally meaningful results rather than golden
internal values" and to "Swapping shape order preserves contact existence without
reusing a hard-coded private feature identifier." These are the right
commitments and the assertions honour them. This plan is not a criticism of the
test bodies.

The structural problem is precedent. `AGENTS.md` requires a new standalone CPU
test executable to join `validate_all_cpu_tests.bat` and the file-to-gate mapping,
and requires bug fixes in covered subsystems to add a regression test in the same
commit. Both rules point tests at subsystems. A file named for the gate
contradicts that, and it is the file an agent will find when told "raise
coverage" — which is precisely how a coverage number gets satisfied without
subsystem coverage improving. Goodhart's law, expressed in the directory
structure.

`tools/coverage_floors.json` and `tools/check_coverage.py` are the ratified
enforcement mechanism and stay. This plan does not touch the floors.

## Goal

Every test lives in the file named for the subsystem whose behavior it pins. The
coverage gate keeps its floors and keeps passing, but no test file is organized
around it.

## Non-Goals

- **No coverage floor changes.** No floor is lowered, raised, or excluded. The
  gate must pass at the same floors after the move as before, and that is the
  primary acceptance proof.
- No test deletions. Every existing assertion survives the move. If a test is
  genuinely redundant with an existing subsystem test, it is *recorded* as a
  candidate and left in place; deleting coverage during a reorganization is how
  coverage silently drops.
- No new test target. The tests move into existing files in the main doctest
  project; no standalone executable is added, so no `validate_all_cpu_tests.bat`
  registration is required.
- No behavior change in engine source. This plan touches tests and project files
  only.
- No renaming the file and calling it done. Moving 549 lines from
  `TestCoverageFloorContracts.cpp` to `TestCoverageContracts.cpp` changes nothing.

## Phases

- [x] **CG0 — Map every test to its owning subsystem.**
  For each `TEST_CASE` in the file, record the subsystem whose behavior it pins,
  the existing test file that owns that subsystem, and the coverage lines it is
  currently responsible for reaching. Candidate destinations from the current tree:
  `TestObjectContactManifold.cpp`, `TestConvexHull.cpp`, `TestPersistentContactSolver.cpp`,
  `TestPhysicsHandles.cpp`, `TestReplayArtifact.cpp`, `TestReplayRecorder.cpp`,
  `TestSceneEntityStore.cpp`, and `TestTerrain.cpp`. Where a test pins a subsystem
  with no existing file, name the new subsystem-named file. Capture the current
  per-subsystem coverage percentages from `tools\validate_coverage.bat` as the
  before-measurement. Acceptance: every `TEST_CASE` has one named destination; the
  before-coverage table is recorded; no test is unassigned.

- [ ] **CG1 — Move the tests and delete the file.**
  Move each `TEST_CASE` and its supporting fixtures to its destination, preserving
  assertions exactly. Fixtures shared by two destinations move to the existing
  shared test support rather than being duplicated. Delete
  `TestCoverageFloorContracts.cpp` and update `SKULLBONEZ_TESTS.vcxproj` and its
  filters in the same commit. Each destination file's header comment gains the moved
  invariants — the receiving file must explain what it now pins, per the comment
  style guide. Acceptance: `TestCoverageFloorContracts.cpp` does not exist;
  `tools\validate_coverage.bat` passes at unchanged floors; total assertion count
  is equal to or greater than the pre-move count; `validate_tests.bat` passes with
  no skipped or renamed cases lost.

- [ ] **CG2 — Close the precedent and hand off.**
  Add the rule that closes the loop: a test file is named for the subsystem it
  pins, never for a gate, a metric, or a plan. Place it in
  `Agentic/Reference/code-style-guide.md` and in the `AGENTS.md` Reviews section
  beside the existing same-commit regression-test requirement, so it reaches an
  agent told to raise coverage. Obtain one independent review asking: did any
  assertion change meaning in the move, did coverage drop in any subsystem, and
  does any remaining test file name a gate. Acceptance: review clear; the
  before/after per-subsystem coverage table is published in the closure report with
  no subsystem lower than before; `tools\validate_coverage.bat` and
  `tools\validate_all_cpu_tests.bat` pass.

## Dependencies And Decisions

- Depends on `governance-shape-to-judgment-conversion` only for CG2's placement of
  the new rule; if that plan has already amended `AGENTS.md`, CG2 appends to the
  amended text rather than the original.
- Sequence after `scene-sized-store-capacity` and `extraction-scar-remediation`,
  both of which change physics source these tests cover. Moving tests first would
  force two coverage re-measurements.
- No open owner decisions. The coverage floors are ratified and untouched.

## Acceptance

- No test file is named for a gate, metric, or plan.
- Every subsystem's coverage is equal to or above its pre-move figure.
- Coverage floors unchanged and passing.
- Assertion count not reduced; no test lost or weakened.

## Validation

Per the File To Validation Mapping, `SkullbonezTests/*` and the test project
require `validate_tests`, and coverage-scope changes require the coverage gate run
directly:

- `tools\validate_tests.bat`
- `tools\validate_coverage.bat` — run directly, because instrumentation scope
  changes
- `tools\validate_all_cpu_tests.bat` — required at the closure gate
