# Unit Test Pyramid Plan

Date: 2026-07-06
Status: Implemented 2026-07-07
Impact area: build system, tests, tools; no runtime behavior change
Validation for this document: none (documentation-only)

## Problem

The repository has **no unit test layer**. No test framework exists anywhere in
the tree (no gtest/catch2/doctest hits). The entire verification story is
end-to-end golden files:

- Build the whole engine, launch the exe, diff DX12 screenshots.
- Byte-exact physics CSV comparison (`TestOutput/baselines/physics_regression_solver.csv`).
- Regex lint scripts (`tools/check_runtime_boundaries.py`, allocation policy checks).

Consequences:

- Feedback loops are minutes long and binary. A failing gate says "something
  changed," never *what* or *where*.
- The oracle is brittle: byte-exact CSV cannot distinguish "physics is wrong"
  from "float operations were reordered." Legitimate refactors and real bugs
  fail identically.
- Solver math, collision primitives, quaternion math, the scene parser, the
  allocation ratchet, and replay ring buffers have **zero** isolated coverage.
- The heavy validation bureaucracy in `AGENTS.md` (file-to-script mapping,
  danger-zone table) is a compensating control for the missing pyramid layer.

## Goal / Definition of Done

- A `SKULLBONEZ_TESTS` project exists in `SKULLBONEZ_CORE.sln`, builds in the
  normal Debug/Profile configurations, and runs in **under 10 seconds**.
- `tools\validate_tests.bat` runs it; `tools\validate_fast.bat` runs it as its
  first rung, before any engine launch.
- Math, physics primitives, stores/handles, allocation policy, replay ring
  buffers, and the scene parser each have a test file with meaningful cases.
- A fast determinism property test exists that does not require the CSV gate.
- New-code norm documented in `AGENTS.md`: bug fixes in covered subsystems add
  a regression test in the same commit.

## Non-goals

- Replacing the end-to-end gates. Screenshot diffs, the CSV determinism gate,
  and interaction proofs remain the integration layer. This plan adds the
  missing layer *under* them.
- Testing DX12 GPU behavior in unit tests.
- Refactoring subsystems purely to make them testable (that work belongs to
  plans 02/06 and the authoritative set; this plan starts with what is already
  testable).

## Framework choice

**doctest** (single header, MIT, fastest compile-time of the single-header
frameworks, MSVC /W4 clean, zero dependencies). Rationale: the repo is
dependency-light by policy — one vendored header under
`SkullbonezSource/ThirdParty/doctest/` matches the existing style better than
NuGet/vcpkg plumbing. Catch2 is acceptable if doctest is vetoed; gtest is not
worth the dependency footprint here.

## Phased slices

### Phase 0 — harness bootstrap (one sitting)

1. Vendor `doctest.h`; add `SKULLBONEZ_TESTS.vcxproj` (x64, same toolset/W4)
   with a `main.cpp` and one smoke test.
2. Compile-in the source files under test directly (no library split needed
   yet; plan 04 later replaces this with linking `SkullbonezMaths.lib` etc.).
3. Add `tools\validate_tests.bat` (build tests project + run). Wire it as the
   first step of `tools\validate_fast.bat`.
4. Update `AGENTS.md` validation tables: unit-test-only changes map to
   `validate_tests`.

### Phase 1 — pure math (no globals, no engine state)

- `Maths/Vector3`, `Quaternion` (normalize/renormalize drift, slerp endpoints,
  axis-angle round-trip), `Matrix4` (inverse, TRS composition), `GeometricMath`
  (ray/sphere, ray/box, plane distances — including the degenerate inputs the
  current code `throw`s on).

### Phase 2 — physics primitives and stores

- `BoundingSphere`/`BoundingBox` overlap/containment truth tables.
- `ConvexHullShape`: face/edge/mass/inertia invariants on baked hulls (this
  file has 41 `throw` sites of validation logic that currently only executes
  in production).
- `SpatialGrid`: insert/remove/query round-trips, boundary cells, the missed
  collision class from the danger-zone table.
- `PhysicsBodyStore` / `ColliderStore`: generational handle semantics — stale
  handle rejected after free/reuse, dense-index remap on removal,
  handle-to-model-index resolution (direct support for plan 06).

### Phase 3 — engine-adjacent units

- `RuntimeReserveAllocator`: cap enforcement, growth accounting, phase gates,
  denial paths (the allocation gate is core policy and currently only tested
  by launching the engine with `--allocation-guard`).
- `ReplayRecorder` ring buffers: wrap behavior, restore cursor (documented as
  "part of the replay ABI" in `RuntimeTools.h` — an ABI with no test).
- `TestSceneParser`: minimal-scene round-trip, unknown-key rejection, the
  malformed-input paths that currently `throw`.

### Phase 4 — fast determinism property

- A micro-world (3–5 bodies, fixed seed) stepped N ticks twice in-process;
  assert bit-identical store state. Runs in milliseconds; catches the
  "accidentally nondeterministic" class instantly, long before the CSV gate.
- Same micro-world stepped through snapshot/restore
  (`CaptureReplaySolverSnapshot`/`RestoreReplaySolverSnapshot`) asserting
  restore is lossless — this is the invariant plan 03 leans on.

## Guardrails / ratchets

- `validate_fast` fails if the test project fails to build or any test fails.
- Add a test-count floor to `tools/check_runtime_boundaries.py`-style ratchet
  (optional, later): count of test files may not decrease.

## Risks

- Compile-in-the-sources (phase 0) duplicates object code and will surface
  hidden global coupling; that surfacing is a feature — each blocker found is
  a concrete work item for plan 02.
- Determinism tests must run with the exact fastmath/fp flags of the Debug
  physics build; copy the compiler flag block from the main vcxproj verbatim.

## Validation map for implementation slices

| Slice | Validation |
|-------|-----------|
| Phase 0 harness | `tools\validate_fast.bat` (proves no engine impact) + run new tests |
| Phases 1–4 | `tools\validate_tests.bat`; no engine launch needed |
| `AGENTS.md`/tools edits | `validate_fast`, then run the changed script |
