# SbResult Frame Path Cost SR2 Closure

Date: 2026-07-27
Plan phase: `sbresult-frame-path-cost` SR2
Result: PASS — ZERO BLOCKERS

## Reconciliation

The final representation preserves the 528-byte Win64 `SbResult` carrier and
its 512-byte inline failure message. Success construction now initializes only
the observable empty owner and `message[0]` sentinel. Failure construction
still initializes the complete buffer before the unchanged bounded
`vsnprintf`, and copy/move operations copy the complete buffer only for a
failure.

The corrected census contains 176 named definitions plus one explicit
trailing-return lambda: 57 frame-reachable callables, 51 scene-load or
resource-build callables, and 69 cold or on-demand callables.

## Independent Review

The first review blocked closure because the initial census missed 15 named
inline/static-inline definitions and one trailing-return lambda. The census was
corrected and independently re-reviewed.

Final review: PASS — ZERO BLOCKERS.

- no success copy or move reads the uninitialized failure-only tail;
- no failure message can dangle;
- no call site lost `[[nodiscard]]`;
- no message is truncated relative to the prior 511-byte payload capacity;
- default success remains observably `{ok, "", ""}`.

## Measurements And Gates

`sizeof(SbResult)` is 528 bytes before and after.

| Scenario | Before frame avg | After frame avg | Before p99 | After p99 |
|---|---:|---:|---:|---:|
| DX12, 1,940 frames | 0.8486 ms | 0.8517 ms | 1.3123 ms | 1.3028 ms |
| Physics bench, 2,340 frames | 0.4700 ms | 0.4588 ms | 0.8471 ms | 0.8206 ms |

- `tools\validate_tests.bat`: PASS; 421 tests and 2,410,274 assertions.
- `tools\validate_perf.bat`: PASS; all allocation, absolute-budget, and
  committed comparison gates.
- `tools\validate_full.bat`: PASS in 337 seconds; CPU/coverage umbrella, DX12
  renderer, runtime lanes, and byte-exact 44,401-line physics baseline.
- Comment audit: PASS; `SbResult.h` records the sentinel-only success and
  fully initialized inline failure invariants, and `Run.h` records the frame
  path ruling.
