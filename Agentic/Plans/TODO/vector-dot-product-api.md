# Vector Dot Product API

Date: 2026-07-28
Status: TODO — 0/3 phases complete
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

- [ ] **VD0 — Inventory and classify every vector-vector multiply.** Separate
  dot products from scalar, matrix, and component operations; identify
  determinism-sensitive solver expressions and tests.
- [ ] **VD1 — Add `Dot` and migrate without arithmetic reshaping.** Prefer a
  named free/member operation consistent with existing Maths ownership. Replace
  one expression at a time without reassociation, temporary aggregation, or
  compatibility macros; delete `operator*` if authorized.
- [ ] **VD2 — Prove readability and determinism.** Add focused Maths coverage,
  run source/deletion proofs, comment audit, byte-exact Physics, performance,
  and broad validation.

## Acceptance

Solver and collision dot products read explicitly, numeric evaluation remains
byte-exact, and no ambiguous vector-vector multiplication survives unless the
owner explicitly retains it with a deletion condition.

## Validation

`tools\validate_tests.bat`, `tools\validate_physics.bat`,
`tools\validate_perf.bat`, and `tools\validate_full.bat`.
