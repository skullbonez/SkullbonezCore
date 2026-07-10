# DX12 Failure Propagation And Command-State Safety

Date: 2026-07-10
Status: Planned — 0/6 phases complete
Impact area: DX12 renderer, recoverable-result policy, device-loss handling,
renderer tests
Owner: DX12 backend

## Problem

The repository reached zero engine `throw` sites, but that is not sufficient
evidence that recoverable failures are handled. The 2026-07-10 review found:

- `SbResult` is not `[[nodiscard]]`, so a failed operation can be discarded
  without a compiler diagnostic.
- `RenderBackendDX12::WaitForGpu()` has ten ignored call results.
- Eight command-list `Close()` calls and four allocator/list `Reset()` calls
  ignore their HRESULT.
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

- [ ] **D0 — Complete operation inventory.** Inventory every HRESULT-returning
  DX12 call and every `SbResult` call in `Rendering/DX12`. Classify each as
  checked, infallible-by-contract, Lane R, or Lane F. Record file, operation,
  owner, caller, and required test. Acceptance: zero unclassified rows.
- [ ] **D1 — Make ignored results visible.** Mark `SbResult` `[[nodiscard]]`
  after inventorying intentional discards repository-wide. Add a named
  `IgnoreResultWithReason(...)` only where discard is genuinely harmless and
  record the reason in source. Acceptance: `/W4 /WX` builds with no implicit
  `SbResult` discard.
- [ ] **D2 — Repair command-list state transitions.** Make
  `EnsureCommandListOpen()` return `SbResult`; check allocator `Reset`, command
  list `Reset`, and `Close`; set `m_commandListOpen` only after success. Propagate
  failure through barriers, clear, draw preparation, upload flush, readback,
  DXR, finish, and present. Acceptance: no caller records or submits after a
  failed open/close/reset.
- [ ] **D3 — Repair waits and submissions.** Check every `WaitForGpu`, fence,
  query-frequency, map, present, and resize result. Resource release after a
  failed wait must not assume GPU completion. Acceptance: wait failure cannot
  reach resource destruction or allocator reuse.
- [ ] **D4 — Cover partial initialisation and optional features.** Apply the same
  rules to DXR, readback, textures, dynamic geometry, framebuffer, BLAS/TLAS,
  and SBT creation. Preserve optional-feature fallback only when all consumers
  are guarded. Acceptance: one owner/message is retained for each failure.
- [ ] **D5 — Fault-injection and closure.** Add CPU-side fault-injection tests
  for command open/close/reset/wait state transitions and one Debug runtime
  probe that injects an operation failure before submission. Acceptance: the
  probe exits nonzero with a bounded diagnostic, emits zero subsequent command
  submissions, and produces no DX12 validation error before shutdown.

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
