# DX12 Failure Propagation And Command-State Safety

Date: 2026-07-10
Status: Complete — 6/6 phases complete
Impact area: DX12 renderer, recoverable-result policy, device-loss handling,
renderer tests
Owner: DX12 backend

## Problem

The repository reached zero engine `throw` sites, but that is not sufficient
evidence that recoverable failures are handled. The 2026-07-10 review found:

- `SbResult` is not `[[nodiscard]]`, so a failed operation can be discarded
  without a compiler diagnostic.
- `RenderBackendDX12::WaitForGpu()` has ten ignored call results.
- Nine command-list `Close()` calls (eight backend operations plus the device's
  initial close) and four allocator/list `Reset()` calls ignore their HRESULT.
- `EnsureCommandListOpen()` logs a fence-wait failure and returns `void`; callers
  can continue recording barriers, drawing, or presenting with a command list
  that was not reopened successfully.

These are cold failure paths, so normal screenshot and validation runs can pass
while device removal, allocator reset failure, or fence failure remains unsafe.

## Goal

Every state-changing DX12 operation has one explicit outcome:

1. success and the backend state advances;
2. recoverable Lane R failure and the current render operation/frame aborts
   without further command recording; or
3. Lane F fatal invariant with owner and state diagnostics when continuing
   would be logically impossible.

No command-list state flag may change until the corresponding DX12 operation
succeeds.

## Non-goals

- No exception reintroduction.
- No new polymorphic error or device abstraction.
- No automatic device recreation until safe failure propagation is complete.
- No suppression of DX12 validation messages to make a fault-injection test pass.

## Phases

- [x] **D0 — Complete operation inventory.** Inventory every HRESULT-returning
  DX12 call and every `SbResult` call in `Rendering/DX12`. Classify each as
  checked, infallible-by-contract, Lane R, or Lane F. Record file, operation,
  owner, caller, and required test. Acceptance: zero unclassified rows.
  Evidence: [the 2026-07-10 DX12 failure-surface inventory](../../Reports/dx12_failure_inventory_20260710.md)
  reconciles 101 HRESULT call sites (70 captured/checked, 31 discarded), 115
  `SbResult` construction/invocation expressions (10 discarded
  `WaitForGpu()` calls), and 44 result-less API calls, with zero unclassified
  rows.
- [x] **D1 — Make ignored results visible.** Mark `SbResult` `[[nodiscard]]`
  after inventorying intentional discards repository-wide. Add a named
  `IgnoreResultWithReason(...)` only where discard is genuinely harmless and
  record the reason in source. Acceptance: `/W4 /WX` builds with no implicit
  `SbResult` discard. Evidence: `SbResult` is type-level `[[nodiscard]]`; the
  2026-07-10 Profile and Debug builds completed with zero warnings/errors, and
  the only newly exposed discard was converted to explicit sticky command-state
  ownership in `BuildTLAS`.
- [x] **D2 — Repair command-list state transitions.** Make
  `EnsureCommandListOpen()` return `SbResult`; check allocator `Reset`, command
  list `Reset`, and `Close`; set `m_commandListOpen` only after success. Propagate
  failure through barriers, clear, draw preparation, upload flush, readback,
  DXR, finish, and present. Acceptance: no caller records or submits after a
  failed open/close/reset. Evidence: allocation-free
  `Dx12CommandRecordingState` commits epochs only after successful DX12 calls,
  retains the first failure until device reset, and is exercised by the 17 new
  CPU command/drain/submission tests in `Dx12ArchUnitTests`; the architecture
  gate passed on 2026-07-10.
- [x] **D3 — Repair waits and submissions.** Check every `WaitForGpu`, fence,
  query-frequency, map, present, and resize result. Resource release after a
  failed wait must not assume GPU completion. Acceptance: wait failure cannot
  reach resource destruction or allocator reuse. Evidence: submitted work is
  tracked independently from the command-list epoch, `FlushGPU` proves
  close/submit/wait/reopen order before resource mutation, runtime callers
  propagate drain failures before scene/resource replacement, and
  `GetTimestampFrequency` now aborts initialization with timer cleanup on
  failure. Three consecutive `tools\validate_dx12_renderer.bat` runs passed
  with zero InfoQueue errors and matching baselines, followed by a passing
  `tools\validate_full.bat` (including byte-exact physics output) on 2026-07-10.
- [x] **D4 — Cover partial initialisation and optional features.** Apply the same
  rules to DXR, readback, textures, dynamic geometry, framebuffer, BLAS/TLAS,
  and SBT creation. Preserve optional-feature fallback only when all consumers
  are guarded. Acceptance: one owner/message is retained for each failure.
  Evidence: backend initialization now rejects frame-upload creation/Map and
  required pipeline warmup failures before publishing dimensions; shader
  reflection rejects every failed descriptor query; texture candidates are
  retired instead of released while recorded work may reference them; TLAS
  rejects unusable zero-sized prebuild output; readback and DXR check the one
  centralized submission result; and optional DXR capability/interface
  fallback retains and logs one bounded `Rendering/DX12Optional` result.
  Resize stages depth and every back buffer before publishing dimensions,
  frame index, access state, or recreation generation. Non-device
  `ResizeBuffers` failure reacquires the old buffers; incomplete restoration
  latches the first failure. Present/resize device removal is sticky across
  command epochs and terminal release abandons canceled submitted work without
  issuing another queue signal or Present.
- [x] **D5 — Fault-injection and closure.** Add CPU-side fault-injection tests
  for command open/close/reset/wait state transitions and one Debug runtime
  probe that injects an operation failure before submission. Acceptance: the
  probe exits nonzero with a bounded diagnostic, emits zero subsequent command
  submissions, and produces no DX12 validation error before shutdown.
  Evidence: CPU architecture coverage now exercises device-health reset and
  first-failure retention, removed-device terminal release, recreation
  publication/failure, and armed/unarmed submission accounting. The new
  `tools\validate_dx12_fault_injection.bat` Debug probe injects immediately
  before the only `ExecuteCommandLists` site and proved exit code 1, the exact
  first owner/message, `submissions=0`, `blocked_after_failure=0`, one bounded
  457-byte stderr diagnostic, and zero InfoQueue errors.

## Closure Evidence

- `tools\validate_fast.bat` passed in 28.0s with zero-warning Profile/Debug
  builds and clean format/project metadata.
- `tools\validate_dx12_arch_tests.bat` and
  `tools\validate_dx12_fault_injection.bat` passed from final source.
- Three consecutive final `tools\validate_dx12_renderer.bat` runs passed in
  23.7s, 22.5s, and 22.5s with zero InfoQueue errors and matching screenshots.
- Final `tools\validate_full.bat` passed in 49.5s, including the mandatory CPU
  umbrella, renderer lane, standalone physics smoke, and byte-exact 20,001-line
  physics baseline.
- `python tools\check_allocation_policy.py --repo .` scanned 306 files with
  zero allowlist errors.
- Touched-file comment audit: 12/12 source/substantial-tool files checked,
  zero deferred; no subsystem checklist was required for this bounded pass.
- The plan-level adversarial review found a blocking device-loss teardown that
  could issue a fence signal after removal and replace the Lane R exit with a
  destructor fatal. Terminal removed-device release and bounded stderr evidence
  were corrected, all owning gates were rerun, and the one required repeat
  review found no remaining material issue.

## Dependencies And Sequencing

- D0 precedes all source changes.
- D1 may expose non-renderer discarded results; fix or explicitly classify them
  before enabling the annotation globally.
- D2-D4 should land before the concrete-owner moves in
  `render-backend-decomposition.md`, so extraction does not copy unsafe state
  transitions into three owners.
- D5 depends on `validation-gate-integrity.md` adding the DX12 architecture test
  target to the default CPU-test umbrella.

## Validation

| Slice | Required gate |
|---|---|
| Result annotation / CPU state tests | `tools\validate_fast.bat` + `tools\validate_dx12_arch_tests.bat` |
| Command state, waits, resource lifetime | `tools\validate_dx12_renderer.bat` three consecutive runs |
| Device lifecycle or shutdown | previous gates + `tools\validate_full.bat` |
| Fault-injection runtime probe | focused probe, then renderer gate |

## Definition Of Done

- All inventory rows are classified and checked.
- `SbResult` cannot be silently discarded.
- No DX12 HRESULT that can change backend state is ignored.
- A failed wait/open/close/reset aborts the current operation before further GPU
  work or resource destruction.
- Fault-injection tests prove the failure path, not only the normal path.
- DX12 validation remains at zero and screenshots match committed baselines.
