# New Aggregate Ruling Gate — NA0

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: NA0

## Current-Source Measurement

The suffix-free scanner discovers 1,167 data-bearing types. The bounded strict
set is defined in one place as a legacy-suffix family name with no per-type
`Invariant:` block. It contains 84 rows, all 84 already carry CA0 owner rulings,
and zero are unruled.

CA4 closed before this phase began, so the historical estimate of roughly 86
unreviewed rows is obsolete and no repository row needs the transitional
`pre-existing-unreviewed` verdict. Its printed count is zero.

## Gate Contract

`--strict` now fails when any bounded row lacks a ruling. Suffix-free discovery,
structural signals, and lexical usage counts remain wider review context and do
not expand the gated set into every data record.

The residual is deliberate and explicit: a new bag named `FooFrameData` evades
the name-scoped mechanical gate. This phase shrinks the evasion surface; the
aggregate ownership review question remains responsible for deliberately
renamed bags.

The temporary verdict is accepted only for the migration window. Every such row
must resolve to its exact declaration site, is counted on every text run, and
will become invalid in NA2. This is a visibility measure, not proof of
ownership.

## Validation

- `python tools/inventory_authority_free_aggregates.py --self-test`: passes.
- `python tools/inventory_authority_free_aggregates.py --repo . --strict`:
  passes with 1,167 discovered, 84 gated, 84 ruled, zero unruled, and zero
  `pre-existing-unreviewed`.
- Planted fixtures prove an unruled multi-member suffix candidate fails, both
  `retain` and `retain-prior` pass, the transitional verdict reports a count of
  one, and a moved transitional declaration site fails.
