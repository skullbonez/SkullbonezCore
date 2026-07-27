# SbResult Compact Success Path

Date: 2026-07-28
Status: TODO — 0/4 phases complete
Impact area: Core error handling, per-frame Runtime phases, diagnostics lifetime
Owner: Core + App composition
Priority: Medium-high

## Problem And Evidence

`SbResult` is 528 bytes on Win64 because every value owns a 512-byte inline
failure message. Sentinel-only success construction avoids clearing the tail,
but frame-reachable phases such as `RenderOperatorUiPhase` and
`PresentFramePhase` still use the large return type for an overwhelmingly
successful result.

## Goal

Make the success-path value compact while preserving every current Lane R
owner/message byte, deterministic formatting, no exceptions, and no dangling
diagnostic lifetime.

## Owner Questions Before SR1

1. How long must a recoverable diagnostic remain queryable: until the immediate
   caller consumes it, until end-of-frame, until overwritten by the same owner,
   or in a retained history?
2. Is a process/frame-owned bounded error table acceptable, or must failures
   remain self-contained values across thread/queue boundaries?
3. Must existing callers keep source-compatible `.ok` / `.error`, or is a
   deliberate one-shot API migration preferred? Proposed default: no
   compatibility wrapper; migrate to the honest compact contract in one plan.

## Phases

- [ ] **SR0 — Measure value flow and lifetime.** Census all construction,
  return, copy/move, queue, persistence, and UI/log consumers; benchmark the
  current frame paths and record the exact 511-byte failure witness.
- [ ] **SR1 — Select the compact diagnostic ownership model.** Compare bounded
  side table/handle, owner-local diagnostic slot, and split status/detail
  designs against thread safety, allocation policy, stale-handle detection, and
  the owner answers above. Record one invariant owner and focused tests.
- [ ] **SR2 — Implement and migrate.** Replace the 528-byte success carrier with
  the selected compact value, migrate callers without a forwarding
  compatibility type, and preserve complete failure formatting and Lane R/F/P
  separation.
- [ ] **SR3 — Prove size, behavior, and cost.** Pin `sizeof`, success/failure
  lifetime, maximum payload, stale-handle behavior, and frame-path performance;
  complete ownership review, comment audit, performance and broad gates.

## Acceptance

Success is a compact trivially understandable value; all failure detail remains
bounded, owned, and queryable for the approved lifetime; no allocation,
exception, stale-reference, or diagnostic truncation regression is introduced.

## Validation

Focused Core/Runtime tests, `tools\validate_tests.bat`,
`tools\validate_perf.bat`, and `tools\validate_full.bat`.
