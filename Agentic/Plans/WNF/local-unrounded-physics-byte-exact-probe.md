# Local Unrounded Physics Byte-Exact Probe

Date: 2026-08-20
Status: WNF - owner requested short experiment plan only; 0/3 tasks complete
Impact area: Physics tests and diagnostics
Owner: Physics determinism envelope
Priority: Parked until explicitly moved out of `WNF/`
Commit name: `PHYSICS_RAW_BITS_PROBE`

## Owner Direction

Run a bounded local experiment to answer whether the canonical Physics bench is
actually byte-exact when float rounding is removed. Do not register this plan in
`MASTER-PLAN.md`, `WORK_LEDGER.csv`, or `SessionState.md`.

The existing committed Windows CSV remains useful, but its `%.4f`/`%.6f`
formatting can hide lower-bit differences. Matching that file cannot answer this
experiment's question.

## Experiment

### RBP0 - Define Exact State Bytes

- [ ] Reuse the headless bench setup from the Linux comparison plan, or the
  current Windows product path until that seam exists; do not build a second
  scene-to-Physics mapping.
- [ ] Serialize deterministic state field-by-field in stable body order using
  explicit little-endian integers and `std::bit_cast<uint32_t>` for every float.
  Never `memcmp` or dump a C++ struct because padding, `bool` layout, and ABI are
  not simulation state.
- [ ] Include a versioned header with scene SHA-256, fixed-`dt` bits, frame
  count, body identity table, compiler/build contract, and field schema. At
  minimum record position, orientation, linear/angular velocity, awake/sleep
  state, and the solver/replay state required to resume the same next tick.
  Omit derived `speed` and `omegaMag`; recomputing them would add another
  platform math operation rather than expose stored state.

### RBP1 - Run The Local Exactness Probe

- [ ] Produce two independent Windows runs of
  `physics_bench_varied.scene.json` for 1,200 fixed ticks and require identical
  raw artifacts. Then compare a local Linux/WSL GCC run and, when available, a
  Clang run directly with that Windows artifact.
- [ ] Report the first differing frame, body identity, field, Windows hex bits,
  Linux hex bits, and ULP distance. Retain the complete artifacts only under
  ignored `TestOutput/validation/`; the bounded summary is the review evidence.
- [ ] Prove the checker with negative controls that flip one float bit, one
  sleep-state bit, one identity, and one frame boundary.

### RBP2 - Record The Result Without Moving The Goalposts

- [ ] If every byte matches, report the exact scene, ticks, state schema,
  Windows compiler, Linux compiler(s), floating-point flags, and artifact hashes.
  Claim byte-exactness only for that measured envelope.
- [ ] If bytes diverge, keep the first-difference evidence and classify whether
  divergence begins during scene initialization or a simulation tick. Do not
  round, tolerate ULPs, reorder bodies, or refresh any committed baseline to
  manufacture a pass.

## Acceptance

This experiment succeeds by producing a trustworthy yes/no answer, not by
forcing the answer to be yes. Completion requires repeatable same-platform
artifacts, a direct Windows-to-local-Linux raw comparison, useful first-field
diagnostics, and passing negative controls. It does not by itself create a CI
gate or authorize a new committed raw-state baseline.

## Reactivation Condition

Move this file from `WNF/` to `TODO/` only when the owner explicitly asks to run
the raw-bit experiment. Reconcile its setup with the headless Linux plan before
editing source so both efforts share one canonical bench path.
