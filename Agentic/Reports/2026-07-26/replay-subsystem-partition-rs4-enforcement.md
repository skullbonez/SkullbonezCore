# Replay Subsystem Partition RS4 — Anti-Accretion Enforcement

Date: 2026-07-26
Branch: `nightrunner-25th-JUL-26`
Plan task: RS4

## Result

RS4 is complete. The standing Runtime package table now names Prediction and
Planning, updates every ratified consumer row, and carries complement proofs
for every Runtime package. The Replay boundary proof covers Replay,
Prediction, and Planning together.

The invariant-5 placement rule is now permanent governance: operator-facing
features built on recorded or predicted data belong in Planning or a future
named product package above it. The rule judges responsibility and dependency
direction; it does not use frozen counts, line budgets, or spelling ratchets.

## Mechanical Enforcement

`tools/dependency_graph_rules.json` now agrees with the standing table:

- Replay rejects Prediction and Planning.
- Prediction may consume lower Replay seams and rejects Planning.
- Planning may consume Prediction and Replay.
- App, Automation, DevelopmentTools, Render, Scene, and Runtime/UI carry the
  consumer edges ratified by the partition census.
- The lower-engine boundary rejects all three replay-family packages from
  Core, Physics, Rendering, Scene, and World.

The checker fixture schema now accepts multiple forbidden targets and exercises
the full cross-product of governed source prefixes and negative targets. Its
27 include rules produced 43 negative edge fixtures plus one project fixture.
This includes explicit:

- positive Prediction-to-Replay and Planning-to-Prediction fixtures;
- negative Replay-to-Prediction, Replay-to-Planning, and
  Prediction-to-Planning fixtures; and
- every lower-engine source prefix against Replay, Prediction, and Planning.

All 21 Runtime-table and Replay-boundary mirror commands returned zero rows.
The repository scan returned zero dependency findings.

## Comment Audit

Checklist: this report section.

- Checked: 1 touched source-bearing tool,
  `tools/check_dependency_graph.py`.
- Deferred: 0.
- Unchecked: none.

Its learning header remains complete, and the fixture-matrix vocabulary,
qualitative-rule invariant, and parser/resolver behavior match the
post-change implementation.

## Validation

- `tools\validate_dependency_graph.bat` — PASS; 27 include rules, 43 negative
  edge fixtures, one project fixture, zero findings.
- All 21 documented mirror proof commands — PASS with zero rows.
- `tools\validate_fast.bat` — PASS; Profile and Debug builds have zero
  warnings/errors.
- `tools\validate_all_cpu_tests.bat` — PASS; unit tests, coverage, runtime
  interaction policy, scene parser, UI boundary, and DX12 architecture lanes
  all pass.
- `git diff --check` — PASS.

No source behavior, golden, baseline, manifest, replay artifact, scene, config,
shader, or physics CSV changed.
