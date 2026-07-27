# Operator Command Invariant Ownership — OC1 Transaction

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Phase: OC1 of 4 — complete

## Outcome

`OperatorCommandTransaction` now owns one normalized operator-command packet,
one acceptance ledger, and the binding OC0 phase cursor:

`Idle -> DeviceAndMode -> PhysicsControl -> RuntimePresentation ->
SimulationPolicy -> PhysicsMaterial -> WorldPolicy -> CinematicPolicy ->
Complete`.

The owner is non-copyable. Its stored members are copied command values,
acceptance values, and the cursor; it stores no runtime owner pointer or
reference. `WorldOverrideChange` moved beside the unified value ledger so OC2
does not need a second result owner.

The header names the same-frame arbitration contract explicitly:

- explicit water-reflection mode follows and wins over cycling;
- explicit tornado-shell control follows and wins over tornado auto-sync;
- render and cinematic save intents sample the later final tuning values;
- cinematic mode follows the master toggle and precedes feature then parameter
  tuning.

OC2 will move the existing synchronous owner borrows and mutation kernels behind
the installed phase calls. OC1 deliberately changes no command application or
runtime behavior.

## Exhaustive Phase Proof

`TestRuntimeContracts.cpp` checks the complete 10-by-10 phase matrix, including
the `Count` sentinel. It then launches one isolated fatal child for every
illegal target from every reachable state:

- 9 reachable states × 10 targets = 90 candidate calls;
- 8 adjacent legal edges;
- 82 illegal skips, repeats, regressions, and sentinel calls;
- all 82 terminate through
  `FATAL[Runtime/OperatorCommandTransaction]`;
- a separate in-process walk completes all eight legal edges;
- compile-time checks reject copy construction and copy assignment.

The child-process mechanism is the repository's existing Lane-F test harness;
production fatal semantics were not weakened or intercepted.

## Project Ownership

The new implementation/header are owned by the Runtime Settings filter in the
production project. The test project compiles the implementation directly for
the fatal probes. `validate_project_filters.py` now recognizes
`OperatorCommandTransaction` as a Runtime Settings prefix.

## Validation

- `tools\validate_format.bat` — PASS; 570 source files and 317 headers clean.
- `tools\validate_tests.bat` — PASS; 418/418 doctests and 2,410,156/2,410,156
  assertions, including the legal walk and all 82 isolated fatal probes.
- `tools\validate_fast.bat` — PASS; all 8 stages, Profile and Debug builds, zero
  warnings and errors.
- `git diff --check` — PASS.

No baseline, golden, schema, configuration, or report-format artifact changed.

## OC2 Binding

Move every operator-command mutation behind the installed phase methods,
populate and consume the one acceptance ledger, delete the seven legacy result
records, and remove every `RunInternal` row repository-wide while preserving the
OC0 winner table byte-for-byte.
