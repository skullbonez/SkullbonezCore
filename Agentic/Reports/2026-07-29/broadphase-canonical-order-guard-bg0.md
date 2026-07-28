# Broadphase Canonical-Order Guard — BG0

Date: 2026-07-29
Branch: `nightrunner-29th-JUL-26`
Plan: closed by `broadphase-canonical-order-guard-closure.md`; the completed
TODO was removed under inventory rule 4.

## Result

BG0 binds both canonical candidate-pair radix passes to
`Scene::Capacity::MAX_SCENE_OBJECTS` without changing the Profile instruction
stream or Physics-visible pair order.

`SpatialGrid.cpp` now derives the total index width, low/high digit widths,
bucket counts, masks, and jointly addressable maximum index from the scene
ceiling. One layout assertion deliberately requires review when the ceiling
changes; the second proves the two digits address every valid body index. Both
unfiltered and production-filtered pair collectors consume the same named
constants.

## Compile-Time Failure Proof

A temporary, uncommitted change from `MAX_SCENE_OBJECTS = 8192` to `8193`
failed the Profile build at the new layout assertion:

```text
SpatialGrid.cpp(95,51): error C2338: static assertion failed:
'MAX_SCENE_OBJECTS changed: review the canonical pair radix layout and instruction footprint.'
```

The ceiling was restored to 8192 immediately after the proof.

## Behavior-Neutral Proof

- Pre-change Profile build: PASS, zero warnings/errors.
- Post-change Profile build: PASS, zero warnings/errors.
- `dumpbin /DISASM:NOBYTES` comparison of
  `Profile/SKULLBONEZ_PHYSICS/SpatialGrid.obj`: zero instruction differences.
  The only whole-file dump difference was the `.debug$S` section size caused by
  the additional source/debug metadata.
- Focused `SpatialGrid:*`: 15/15 cases and 8,507/8,507 assertions pass.
- Focused `*determinism*`: 3/3 cases and 30,897/30,897 assertions pass.

## Comment Audit

Touched source scope: 1 file.

- [x] `SkullbonezSource/Physics/SpatialGrid.cpp`

The existing learning header remains accurate. The new `Invariant:` block
explains the capacity/layout coupling, review boundary, and determinism hazard;
its permanent `Related:` entry resolves to this report. Checked: 1. Deferred:
0.

## Formal Validation

`tools\validate_physics.bat` passes in 71.80 seconds. Debug and Profile
binaries are ready, the engine lifecycle smoke passes, and
`physics_regression_varied.csv` matches all 44,401 rows byte-for-byte
(`output runs=2`, `baseline runs=1`).

No baseline refresh is authorized or expected.
