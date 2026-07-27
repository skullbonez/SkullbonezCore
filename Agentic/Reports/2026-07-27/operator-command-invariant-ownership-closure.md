# Operator Command Invariant Ownership — Closure

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Phases: OC0–OC3 — complete
Result: CLOSED

## Outcome

`OperatorCommandTransaction` is the single owner of one normalized
operator-command packet, the exact eight-edge mutation order, same-frame
arbitration, and the accepted-action ledger. The transaction is non-copyable,
stores values only, and borrows each concrete runtime owner only for the
duration of one phase call.

The binding walk is:

`Idle -> DeviceAndMode -> PhysicsControl -> RuntimePresentation ->
SimulationPolicy -> PhysicsMaterial -> WorldPolicy -> CinematicPolicy ->
Complete`.

The owner encodes every OC0 winner: explicit tornado-shell control follows
auto-sync; explicit water mode follows reflection cycling; save intents precede
the tuning they eventually sample; cinematic mode follows the master toggle and
precedes feature then parameter tuning. The header states these rules and the
complete 10-by-10 cursor test proves all eight legal edges and all 82 illegal
reachable transitions.

## Deleted Shape

- Seven command result records became one
  `OperatorCommandAcceptanceLedger`, retaining exactly the facts with named
  `InputFrame` or replay consumers.
- Seventeen operator apply entry points became seven transaction phases or
  focused concrete-owner kernels.
- `OperatorCommandApplier.h` and its test were deleted;
  `OperatorCommandApplier.cpp` became the transaction command implementation.
- All 71 `RunInternal` rows across 29 files were removed. No replacement
  `*Internal`, context, service, callback, inheritance, or type-erasure bag was
  introduced.
- General world mutation moved to `WorldEnvironment::ApplyOverride`.
  Cinematic UI policy is shared through `SceneCinematicPolicy`, and the pure
  sun-direction helper now lives beside that policy.

`InputFrame` preserves every interleaved OC0 barrier and consumes the ledger at
the same action-recording sites, including worker recording after model-count
handling and scene-request submission only after `Complete`.

## Independent Review And Comment Audit

The independent read-only review returned **CLEAR** with no blocker. It
confirmed the single invariant owner, explicit header contract, exhaustive
cursor proof, zero sibling-family or replacement-bag rows, value-only retained
state, complete OC0 barrier/consumer preservation, and the correct
sun-direction policy move.

Its sole non-blocking observation is that same-frame winner values are covered
through explicit production source order and runtime lanes rather than isolated
pair fixtures.

The OC3 learning-header audit checked 37/37 current source-bearing files with
zero deferred or unchecked rows. All repository-relative `Related` paths
resolve and no current-source comment names a deleted family.

Evidence:

- `operator-command-invariant-ownership-oc3-independent-review.md`
- `operator-command-invariant-ownership-oc3-comment-audit.md`

## Final Validation

- `tools\validate_full.bat` — PASS:
  - all six CPU/coverage lanes;
  - 418/418 doctests and 2,410,159 assertions;
  - Automation build-boundary, replay/prediction, and development-UI smoke;
  - Debug and Profile builds with zero warnings/errors;
  - DX12 run `20260727T043606Z`, zero InfoQueue errors, all three committed
    screenshot baselines matched;
  - Physics `physics_regression_varied.csv`, 44,401 lines byte-exact.
- `tools\run_graphics_stress.bat 1` — PASS with seed `3235774467`: 15,007
  frames, 413 scene loads, clean timed shutdown, empty stderr, no fatal,
  device-removed, or DX12 error marker, and final memory artifacts written.
- `tools\validate_format.bat` — PASS; 569 implementations and 316 headers clean.
- Aggregate ownership inventory — 1,167 candidates, zero signalled, 84/84
  reviewed, zero unruled.
- Legacy-family and `RunInternal` scans — zero rows.
- `git diff --check` — PASS.

Logs:

- `TestOutput/validation/oc3_validate_full.stdout.log`
- `TestOutput/validation/oc3_validate_full.stderr.log`
- `TestOutput/validation/oc3_graphics_stress.stdout.log`
- `TestOutput/graphics_stress/latest_stdout.txt`
- `TestOutput/graphics_stress/latest_stderr.txt`

No baseline, golden, schema, configuration, or automation report-format artifact
changed.

## Ledger Handoff

Plan 9 closes at 4/4 and leaves the active/future inventory under rule 4. Its
four completed phases leave the Round 5 live ledger at 1/21 (5%). Plan 5 remains
blocked at FV1; plan 10 `coverage-gate-test-reorganization` is the next
non-blocked item, beginning with CG0.
