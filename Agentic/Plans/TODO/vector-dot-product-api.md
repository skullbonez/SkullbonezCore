# Vector Dot Product API

Date: 2026-07-28
Status: ACTIVE — 1/3 phases complete
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
- [ ] **VD1 — Add `Dot` and migrate without arithmetic reshaping.** Prefer a
  named free/member operation consistent with existing Maths ownership. Replace
  one expression at a time without reassociation, temporary aggregation, or
  compatibility macros; delete `operator*` if authorized.
- [ ] **VD2 — Prove readability and determinism.** Add focused Maths coverage,
  run source/deletion proofs, comment audit, byte-exact Physics, performance,
  and broad validation.

## VD0 Evidence

- Census: [vector-dot-product-api-vd0-census.md](../../Reports/2026-07-28/vector-dot-product-api-vd0-census.md)
- 171 true vector-vector dot calls exist across 34 tracked first-party files:
  163 production and 8 tests. The migration surface is 96 Physics, 56 Runtime,
  10 Maths, 1 Gameplay, and 8 test calls.
- `Vector3` is the only owned vector type; the tree has no owned `Vector2` or
  `Vector4` API.
- The only current `Dot` spelling is a file-local OrbitalMechanics adapter that
  delegates to the ambiguous overload. VD1 will move the named API to the
  `Math::Vector` owner, delete that adapter, migrate every row without
  reassociation, and delete the vector-vector overload.
- All Physics rows are byte-exact-sensitive. The census records every
  expression and the focused/broad test map.
- The 21 `PersistentContactSolver.cpp` rows do not overlap the protected
  warm-start hunks; VD1 must partial-stage that file so those hunks remain
  uncommitted.
- No owner question remains for VD1.

## Acceptance

Solver and collision dot products read explicitly, numeric evaluation remains
byte-exact, and no ambiguous vector-vector multiplication survives unless the
owner explicitly retains it with a deletion condition.

## Validation

`tools\validate_tests.bat`, `tools\validate_physics.bat`,
`tools\validate_perf.bat`, and `tools\validate_full.bat`.
