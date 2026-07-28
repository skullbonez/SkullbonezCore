# Broadphase Canonical-Order Guard

Date: 2026-07-29
Owner: skullbonez
State: In progress (BG0 complete)
Ledger tasks: 2 (BG0-BG1)
Branch: nightrunner-29th-JUL-26
PR: TBD

## Goal

Bind the broadphase candidate-pair radix sort to `MAX_SCENE_OBJECTS` at compile
time so raising the scene ceiling cannot silently de-canonicalize pair emission.

This is a guard, not a behavior change. No pair order, physics value, or
baseline byte may move.

## Problem And Evidence

Measured on 2026-07-29 against `main` tip `90e4d52f`.

`SpatialGrid::GetFilteredCandidatePairsImpl` and `GetCandidatePairs` sort each
body's pair list with a two-pass LSD radix over a 7-bit low digit and a 6-bit
high digit:

- `SkullbonezSource/Physics/SpatialGrid.cpp:1240` — `candidatePairSortKeys[...] & 0x7f`
- `SkullbonezSource/Physics/SpatialGrid.cpp:1259` — `( ... >> 7 ) & 0x3f`

7 + 6 = 13 bits addresses values `0..8191`, which is exactly
`Scene::Capacity::MAX_SCENE_OBJECTS - 1`. Nothing ties those literals to that
constant.

Failure mode if `MAX_SCENE_OBJECTS` is raised: body indices above 8191 alias to
a lower digit pair, the emitted list stops being ascending, and narrowphase
receives pairs in bucket-discovery order. There is no assert, no crash, and no
allocation failure — only silent loss of the canonical ordering that
`SpatialGrid.h`'s own invariant block promises, and therefore silent physics
baseline drift.

The repository already has the correct pattern for exactly this coupling:
`SkullbonezSource/Physics/PersistentContactSolver.h:50` static-asserts
`MAX_SCENE_OBJECTS - 1 <= PERSISTENT_CONTACT_BODY_MASK`. The broadphase sort has
no equivalent.

Related unguarded couplings to confirm or guard in the same pass:

- `SpatialGrid::MarkCandidatePairFirstSeen` triangular index
  (`SpatialGrid.cpp:1071`) — `int pairIndex = b * ( b - 1 ) / 2 + a` overflows
  signed 32-bit above roughly 65,536 bodies.
- `Bucket::ix/iy/iz` are `int16_t` (`SpatialGrid.h:172`) while cell coordinates
  are computed as `int`; `MAX_WORLD_COORDINATE / MIN_CELL_SIZE` is 200,000,
  which does not fit `int16_t`.

## Non-Goals

- Raising `MAX_SCENE_OBJECTS`.
- Replacing the radix sort with a different algorithm.
- Changing cell-coordinate storage width. BG1 records the bound; any widening
  is separate work with its own physics gate.

## Ledger

- [x] BG0 — Derive the radix digit widths from `Scene::Capacity::MAX_SCENE_OBJECTS`
  as named `constexpr` values, replace the four `0x7f`/`0x3f`/`>> 7` literals
  and the `128`/`64` bucket-array extents with those values, and add the
  `static_assert` that the two digits jointly address every valid body index.
  Confirm the emitted instruction sequence and pair output are unchanged.
  Evidence: `../../Reports/2026-07-29/broadphase-canonical-order-guard-bg0.md`.
- [ ] BG1 — Add the triangular-index and cell-coordinate range guards, extend
  `TestSpatialGrid.cpp` with a case that pins ascending canonical emission at
  the current ceiling, and record the `int16_t` cell-coordinate bound as a
  stated `Invariant:`/`Hazard:` in `SpatialGrid.h` with its numeric limit.

## Acceptance

- No literal digit width, mask, or bucket extent in the candidate-pair sort is
  independent of `Scene::Capacity::MAX_SCENE_OBJECTS`.
- Changing `MAX_SCENE_OBJECTS` to a value the sort cannot address fails
  compilation rather than producing unsorted output. Prove this with a
  temporary local edit; do not commit the raised constant.
- `TestSpatialGrid.cpp` fails if the sort stops emitting ascending pairs.
- Physics output is byte-exact against committed baselines. A single differing
  byte means BG0 changed evaluation, not just spelling — revert rather than
  refresh.
- No new count budget, ratchet, or allowance is introduced.

## Validation

- Iteration: focused Profile build, `TestSpatialGrid` and `TestDeterminism` only.
- BG0: `tools\validate_physics.bat` — byte-exact CSV diff is the acceptance
  oracle.
- BG1: `tools\validate_tests.bat`, then `tools\validate_physics.bat` and
  `tools\validate_perf.bat` (mapped: `SpatialGrid*` requires both).

## Comment-Audit Checklist

- [ ] `SkullbonezSource/Physics/SpatialGrid.h`
- [ ] `SkullbonezSource/Physics/SpatialGrid.cpp`
- [ ] `SkullbonezTests/TestSpatialGrid.cpp`
