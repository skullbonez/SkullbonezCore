# Render Interface And WorkerPool Slimming Closure

Date: 2026-07-12
Plan: `render-interface-and-workerpool-slimming`
Result: Complete — 5/5 phases

## Outcome

The worker pool no longer exposes allocating `std::function` submission or
parallel overloads. Replay prediction submits a typed, caller-owned
`AmortizedTask` operation into fixed 256-record rings; private type erasure is
confined to WorkerPool internals. Exhaustion is fatal with
owner/phase/count/capacity/high-water diagnostics, and the worker self-test
forces ring wraparound with 320 fenced submissions.

The representative DX12 run measured a 20-draw high-water. The interface
inventory found a real test, optional capability, or authority-narrowing
consumer for every reviewed seam. A concrete shader/mesh devirtualization
probe broke the existing null-resource test implementations, so its changes
were restored. The evidence-backed R2 result is therefore an empty collapse
list, with retention rationale recorded on the hot interfaces and backend.

Detailed measurement and per-interface decisions are in
`render-interface-workerpool-measurement.md`. The comment audit checked 10/10
touched source-bearing files with no deferrals.

## Independent Review

The first rubber-duck review blocked the initial draft because
`AmortizedTask` still invoked `std::function` and the public submission API
accepted raw state/callback pairs. The revised typed operation and private
dispatcher passed the second independent review. It also confirmed replay
lifetime safety, mutex-protected ring state, forced wraparound, complete
overflow diagnostics, and no new callback, adapter, bridge, or inheritance
debt.

## Validation

- `Profile\SKULLBONEZ_CORE.exe --worker-self-test --workers 2` — passed in
  1.15s; the 320-task fixed-ring wrap probe and deterministic parallel checks
  completed.
- `python tools\check_allocation_policy.py --self-test` plus
  `python tools\check_allocation_policy.py --repo .` — passed together in
  8.26s; 321 files scanned and `allowlist_errors=0`.
- `tools\validate_perf.bat` — passed in 62.44s; DX12 absolute budgets passed,
  DX12 showed no regression across 1,940 frames, and physics showed no
  regression across 2,340 frames.
- `tools\validate_full.bat` — passed in 85.47s; formatting and CPU lanes
  passed, Profile/Debug built with zero warnings, DX12 reported zero InfoQueue
  errors with all screenshots matching, and the 44,401-line physics baseline
  matched byte-for-byte.
- `tools\run_graphics_stress.bat 1` — passed in 60.94s; PID 9808 ran the
  bounded DX12 stress suite for one minute and was closed by the PID-scoped
  timeout without a crash.

Logs are under `TestOutput/validation/` and the stress artifacts are under
`TestOutput/graphics_stress/`.
