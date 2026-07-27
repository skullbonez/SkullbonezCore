# Operator Command Invariant Ownership — OC2 Owner Migration

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Phase: OC2 of 4 — complete

## Outcome

Every operator-command mutation now runs behind the seven ordered
`OperatorCommandTransaction` phase methods. `InputFrame` constructs one
transaction from the normalized command packet, walks the OC0 phase order around
the existing concrete-owner barriers, consumes one
`OperatorCommandAcceptanceLedger`, and calls `Complete` immediately before scene
request submission.

The migration preserves the OC0 same-frame winners:

- explicit tornado-shell control follows tornado auto-enable synchronization;
- explicit water-reflection mode follows reflection cycling;
- ordinary and cinematic save intents are queued before later tuning;
- cinematic mode follows the master toggle and precedes feature and parameter
  edits;
- worker reconfiguration and physics material application still precede the
  generated-control transactions.

`WorldEnvironment::ApplyOverride` now owns the generalized atomic world mutation
used by UI, load, and stress paths. Cinematic parameter, feature, shadow, and sun
policy are exposed through `SceneCinematicPolicy`; stress and rendering call the
same domain path as the transaction.

## Deleted Families

`OperatorCommandApplier.h` and all seven legacy result records are gone.
`OperatorCommandApplier.cpp` became
`OperatorCommandTransaction.Commands.cpp`, and its seventeen command entry
points became transaction phases or focused owner kernels. The discarded
cinematic-mode Boolean identified by OC0 was not retained.

The repository-wide `RunInternal` cleanup flattened 71 rows across the App,
Automation, Editor, Render, Scene, Tools, and test surfaces. No replacement
`*Internal` namespace was introduced.

Measured closure scans:

- `rg -n 'RunInternal' SkullbonezSource SkullbonezTests` — zero rows;
- legacy result-record names — zero rows;
- legacy operator apply-function names — zero rows;
- production and test project/filter inventories — zero errors.

Line-anchored aggregate rulings were reconciled after the namespace deletion;
the ownership inventory reports 1,167 candidates, zero signalled aggregates,
84/84 review rulings, and zero unruled findings.

## Acceptance Ledger

The unified value-only ledger retains exactly the OC0-consumed facts:
device/camera acceptance; physics sleep and tornado actions; presentation,
fixed-step, render, and water actions; simulation and friction actions; one
atomic world before/after record; and cinematic actions. `InputFrame` records
the same `RuntimeInputAction` values at the same interleaved barriers, including
the delayed worker-thread action after model-count handling.

## Validation

- Profile production build — PASS, zero warnings and errors.
- `tools\validate_format.bat` — PASS; 569 implementation files and 316 headers
  clean.
- `tools\validate_tests.bat` — PASS; 418/418 doctests and 2,410,159 assertions,
  including all 82 isolated illegal phase transitions.
- `tools\validate_physics.bat` — PASS; Profile and Debug determinism lanes.
- `tools\validate_fast.bat` — PASS; all eight stages, ownership inventories,
  staged-size gate, Profile/Debug builds, and tests.
- `git diff --check` — PASS.

No baseline, golden, schema, configuration, or automation report-format artifact
changed.

## OC3 Binding

Audit every touched learning header and stale ownership reference, obtain the
required independent review of invariant ownership and retained authority, then
run the full, DX12/stress, Physics, and Automation closure gates.
