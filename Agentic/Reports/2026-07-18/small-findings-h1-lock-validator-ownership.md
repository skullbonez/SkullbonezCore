# Small Findings H1 — Lock Validator Ownership

Date: 2026-07-18  
Branch: `nightrunner-17th-july`  
Task elapsed: approximately 16 minutes

## Result

- `LockOrderValidator::Instance()` and the hidden function-local graph state
  are deleted.
- `Runtime/Init.cpp` owns the validator before the startup-owned `WorkerPool`,
  so reverse local destruction keeps the validator alive until all worker
  threads and the tracked queue mutex are gone.
- `WorkerPool` explicitly receives the validator. Debug uses
  `TrackedMutex` plus `condition_variable_any`; Profile/Release alias back to
  the original `std::mutex` plus `std::condition_variable` primitives.
- The Debug graph is fixed storage: 256 lock rows, a bit-matrix of observed
  edges, and a 64-entry thread-local held-lock stack. The old dynamic
  `.insert()` / `.push_back()` allocation-policy exception is deleted.
- Unit-test pools use explicit test-owned validators, and the test project now
  compiles the same concrete validator implementation as production.

There is no fallback singleton, service locator, nullable owner, dynamic graph
growth, or Release/Profile lock-path instrumentation.

## Validation

- Focused `Profile|x64` solution build: passed in 15.580s after the pool type
  integration was corrected.
- `python tools/check_allocation_policy.py --self-test`: passed.
- `python tools/check_allocation_policy.py --repo .`: passed; 391 files
  scanned, 0 allowlist errors.
- `tools\validate_fast.bat`: passed in 72.137s. Formatting clean; project
  filters 738/738; 291/291 doctests and 21,455/21,455 assertions; Profile and
  Debug builds both passed with zero warnings/errors.
- `tools\validate_perf.bat`: completed in 99.113s. Allocation guard recorded
  zero gameplay violations; selected-ball structural path passed; DX12 and
  physics absolute budgets and regression comparisons passed; measurement
  matrix completed; Profile and Debug ready builds passed.

The first formal attempts are not completion evidence: the first Debug link
correctly exposed the missing test-project source dependency, and the first
performance run correctly rejected the dynamic-map owner shape. Both defects
were fixed before the clean final reruns above.

No authored data, baseline, golden, screenshot, seven retained render consumer
interface, or `FRAME_COUNT = 2` change occurred.
