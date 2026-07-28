# Vector Dot Product API

Date: 2026-07-28
Status: COMPLETE — 3/3 phases complete
Impact area: Maths API, Physics solver readability, deterministic expressions
Owner: Maths + Physics
Priority: Medium

## Problem And Evidence

`Vector3::operator*(const Vector3&)` means dot product while
`Vector3::operator*(float)` means scalar multiplication. Dense collision and
solver expressions therefore require the reader to infer whether `*` is a dot,
scale, matrix transform, or quaternion composition.

## Goal

Make dot products explicit at correctness-sensitive call sites without changing
floating-point evaluation order or byte-exact physics.

## Owner Ruling

Delete vector-vector `operator*` in the same campaign after all callers migrate
to the explicit dot-product API. No compatibility spelling or macro remains.

## Phases

- [x] **VD0 — Inventory and classify every vector-vector multiply.** Separate
  dot products from scalar, matrix, and component operations; identify
  determinism-sensitive solver expressions and tests.
- [x] **VD1 — Add `Dot` and migrate without arithmetic reshaping.** Prefer a
  named free/member operation consistent with existing Maths ownership. Replace
  one expression at a time without reassociation, temporary aggregation, or
  compatibility macros; delete `operator*` if authorized.
- [x] **VD2 — Prove readability and determinism.** Add focused Maths coverage,
  run source/deletion proofs, comment audit, byte-exact Physics, performance,
  and broad validation.

## VD0 Evidence

- Census: [vector-dot-product-api-vd0-census.md](../../Reports/2026-07-28/vector-dot-product-api-vd0-census.md)
- VD0's Profile-preprocessed census found 171 vector-vector dot calls across 34
  files. VD2's full Debug build exposed two additional `_DEBUG`-only rows in
  `SkullScope.cpp` and `LauncherTools.cpp`; the corrected configuration-complete
  census is 173 uses across 36 files: 165 production and 8 tests, comprising 97
  Physics, 57 Runtime, 10 Maths, 1 Gameplay, and 8 test uses.
- `Vector3` is the only owned vector type; the tree has no owned `Vector2` or
  `Vector4` API.
- The only pre-migration `Dot` spelling was a file-local OrbitalMechanics
  adapter that delegated to the ambiguous overload. VD1 moved the named API to
  the `Math::Vector` owner, deleted that adapter, migrated every row without
  reassociation, and deleted the vector-vector overload.
- All Physics rows are byte-exact-sensitive. The census records every
  expression and the focused/broad test map.
- The 21 `PersistentContactSolver.cpp` rows do not overlap the protected
  warm-start hunks; VD1 must partial-stage that file so those hunks remain
  uncommitted.
- No owner question remains for VD1.

## VD1 Evidence

- Implementation report:
  [vector-dot-product-api-vd1-implementation.md](../../Reports/2026-07-28/vector-dot-product-api-vd1-implementation.md)
- One shared inline `Math::Vector::Dot` now owns the exact established
  `lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z` arithmetic spelling.
- The 171-row VD0 Profile census reconciled as 170 final call-site rewrites plus
  deletion of the OrbitalMechanics adapter and its one overload call. VD2 then
  migrated the two conditional-only rows, yielding the corrected 173-use
  reconciliation: 172 final call-site rewrites plus the deleted adapter-body
  use. Every operand remains in its original left/right order and source
  subexpression.
- `Vector3::operator*( const Vector3& )`, the file-local adapter, and every
  compatibility spelling are deleted. Profile compilation, focused tests,
  project/filter metadata, dependency direction, and all three ownership
  inventories pass.
- Touched-source comment audit is 34/34 with zero deferred files. The protected
  warm-start hunk and both other warm files remain byte-identical; VD1 records
  the exact partial-stage boundary for `PersistentContactSolver.cpp`.
- VD2 passed the final byte-exact Physics, deep Physics, performance, replay
  visual-fidelity, and broad validation gates. No baseline, golden, config, or
  schema was refreshed during the campaign.

## VD2 Closure Evidence

- Closure report:
  [vector-dot-product-api-closure.md](../../Reports/2026-07-28/vector-dot-product-api-closure.md)
- Permanent direct `Dot` coverage pins the established x-then-y-then-z
  evaluation: the left-associated mixed-sign products yield exactly `3.25f`,
  while `x + ( y + z )` would lose the z term and yield zero.
- The final tracked-source proof contains 180 `Dot(` occurrences: one shared
  definition and 179 calls. The calls reconcile as 172 migrations, five
  pre-existing OrbitalMechanics named calls, and two permanent VD2 witnesses.
  No vector-vector overload, local helper, macro, alias, explicit member call,
  forwarding wrapper, or compatibility spelling remains.
- Full Profile and Debug compilation, 437/437 tests and 2,419,129 assertions,
  byte-exact 44,401-line Physics, deep Physics/SkullScope baselines, performance,
  Replay visual fidelity, DX12, coverage, and full validation pass without a
  baseline refresh.
- Touched-source comment audit is 36/36 with zero deferred files. Independent
  review found no source or ownership defect after the Debug-only corrections;
  its validation and documentation findings were completed before closure.

## Acceptance

Solver and collision dot products read explicitly, numeric evaluation remains
byte-exact, and no ambiguous vector-vector multiplication survives unless the
owner explicitly retains it with a deletion condition.

## Validation

`tools\validate_tests.bat`, `tools\validate_physics.bat`,
`tools\validate_physics_deep.bat`, `tools\validate_perf.bat`,
`tools\validate_replay_visual_fidelity.bat`, and `tools\validate_full.bat`.
