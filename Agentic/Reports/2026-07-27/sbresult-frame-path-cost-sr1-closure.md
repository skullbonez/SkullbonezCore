# SbResult Frame Path Cost SR1 Closure

Date: 2026-07-27
Plan phase: `sbresult-frame-path-cost` SR1
Result: PASS

## Implementation

`SbError` now has a no-throw default constructor that writes only the observable
success sentinels:

- `owner = ""`;
- `message[0] = '\0'`.

The remaining 511 failure-only bytes are not cleared on success. `SbResult`
copy/move construction and assignment branch on `ok`, so a success copy also
writes only those sentinels. `Failure` clears its failure-only buffer before
formatting through the same `vsnprintf` call, and failure copies copy the
complete initialized buffer. Owner/message lifetime, capacity, truncation, and
emitted C strings are unchanged. `SbResult` remains `[[nodiscard]]`, owns no
heap memory, and is still 528 bytes on Win64.

The frame-path declaration in `Run.h` records that its Lane R returns use this
success representation. `TestRuntimeContracts` pins the 512-byte message
capacity and the 528-byte Win64 carrier size while retaining the existing empty
success-string and formatted-failure checks. It also covers success assignment
over a prior failure and failure copying.

## Measurement

The post-change `tools\validate_perf.bat` run passed all absolute and committed
comparison gates:

| Scenario | Before frame avg | After frame avg | Before input avg | After input avg |
|---|---:|---:|---:|---:|
| DX12, 1,940 frames | 0.8486 ms | 0.8517 ms | 0.0723 ms | 0.0699 ms |
| Physics bench, 2,340 frames | 0.4700 ms | 0.4588 ms | 0.0716 ms | 0.0663 ms |

DX12 frame average moved by +0.4%, while physics-bench frame average improved
2.4% and both input averages improved. DX12 p99 improved from 1.3123 ms to
1.3028 ms; physics-bench p99 improved from 0.8471 ms to 0.8206 ms. These
whole-frame numbers include normal run noise and are not claimed as an isolated
`SbResult` benchmark; the binding conclusion is no regression.

## Validation

- `tools\validate_tests.bat`: PASS.
- `tools\validate_perf.bat`: PASS in 151.2 seconds.
- Existing 511-byte bounded-message coverage: PASS.
- Existing success empty-string, formatted failure, default failure, owner
  lifetime, and independent copied-message coverage: PASS.

No message text, call-site API, heap ownership, baseline, budget, or threshold
changed.
