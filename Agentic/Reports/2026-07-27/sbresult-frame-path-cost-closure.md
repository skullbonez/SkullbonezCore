# SbResult Frame Path Cost Closure

Date: 2026-07-27
Plan: `sbresult-frame-path-cost`
Result: COMPLETE — 3/3 phases, ZERO BLOCKERS

SR0 inventoried 176 named `SbResult` definitions and one trailing-return
lambda, retained the protected 511-byte failure payload, and selected
sentinel-only success construction. SR1 implemented that ruling without heap
ownership, API migration, failure-message changes, or loss of `[[nodiscard]]`.
SR2 reconciled the inventory after an independent-review blocker, then passed
the repeat review and every required gate.

The Win64 carrier remains 528 bytes. A success now initializes only the empty
owner and leading message sentinel; a failure still owns and completely
initializes the same 512-byte buffer. Final performance and full validation
show no regression.

Evidence:

- `sbresult-frame-path-cost-sr0-census.md`
- `sbresult-frame-path-cost-sr1-closure.md`
- `sbresult-frame-path-cost-sr2-closure.md`
