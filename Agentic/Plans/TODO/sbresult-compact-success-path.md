# SbResult Compact Success Path

Date: 2026-07-28
Status: ACTIVE — 3/4 phases complete
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

## Owner Rulings

1. A bounded owner-managed diagnostic store is approved; failure detail does
   not need to remain inline in every success carrier.
2. SR0/SR1 must select the shortest lifetime that satisfies the observed
   consumers. Cross-thread or queued uses require an owner-held immutable entry
   valid through consumption and stale-handle detection.
3. Migrate callers in one deliberate API change. Do not add a source-compatible
   forwarding wrapper around the compact result.

## Phases

- [x] **SR0 — Measure value flow and lifetime.** Census all construction,
  return, copy/move, queue, persistence, and UI/log consumers; benchmark the
  current frame paths and record the exact 511-byte failure witness.
- [x] **SR1 — Select the compact diagnostic ownership model.** Compare bounded
  side table/handle, owner-local diagnostic slot, and split status/detail
  designs against thread safety, allocation policy, stale-handle detection, and
  the owner answers above. Record one invariant owner and focused tests.
  **Decision:** one App-composed `Core::SbDiagnosticStore` owns 256 fixed
  immutable slots. A failed 16-byte `SbResult` leases one packed
  slot/generation token; copies retain, moves transfer, and the last release
  reclaims the slot. Success is the null lease and never touches the store.
  Explicit `store.Failure(...)` production plus `Ok()`/diagnostic accessors
  replace the old static failure and public inline-error API without a wrapper.
  The store copies the complete bounded owner and 511-byte message, detects
  stale generations, synchronizes publication/leases without allocation, and
  makes capacity/lease/generation defects Lane F. SR2 corrected the
  spelling-sensitive SR0 census: the decision-tip bound is 220 factory
  expressions plus 30 retained/aggregate sites, or 250/256, under the verified
  no-recursion/re-entry, no worker publication, and no result-container/queue
  assumptions; SR2/SR3 must rerun that census.
  Direct diagnostic pointers last only for the same unmoved, unassigned live
  result lease; escaping consumers use bounded status-returning copy-out.
  The focused matrix includes test-only concurrent failure publication within
  the fixed capacity and an explicit double-release Lane F child probe.
  Evidence:
  [`../../Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md`](../../Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md).
- [x] **SR2 — Implement and migrate.** Replace the 528-byte success carrier with
  the selected compact value, migrate callers without a forwarding
  compatibility type, and preserve complete failure formatting and Lane R/F/P
  separation. **Implemented:** `SbResult` is the 16-byte store/token lease; the
  single 159,760-byte App-composed store owns synchronized immutable entries;
  every producer/consumer uses the explicit API; `ApplicationExitState`
  retains the compact lease; focused lifetime, bound, concurrency, copy-out,
  and Lane F probes pass. The final multiline-aware census is 221 producer
  expressions plus 29 result-member sites, conservatively 250/256 live entries
  with no result containers/queues or hidden store. Evidence:
  [`../../Reports/2026-07-28/sbresult-compact-success-path-sr2-implementation.md`](../../Reports/2026-07-28/sbresult-compact-success-path-sr2-implementation.md).
- [ ] **SR3 — Prove size, behavior, and cost.** Pin `sizeof`, success/failure
  lifetime, maximum payload, stale-handle behavior, and frame-path performance;
  complete ownership review, comment audit, performance and broad gates.

## Acceptance

Success is a compact trivially understandable value; all failure detail remains
bounded, owned, and queryable for the approved lifetime; no allocation,
exception, stale-reference, or diagnostic truncation regression is introduced.

## Evidence

- SR0 census:
  [`../../Reports/2026-07-28/sbresult-compact-success-path-sr0-census.md`](../../Reports/2026-07-28/sbresult-compact-success-path-sr0-census.md)
- SR1 ownership decision:
  [`../../Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md`](../../Reports/2026-07-28/sbresult-compact-success-path-sr1-decision.md)

## Validation

Focused Core/Runtime tests, `tools\validate_tests.bat`,
`tools\validate_perf.bat`, and `tools\validate_full.bat`.
