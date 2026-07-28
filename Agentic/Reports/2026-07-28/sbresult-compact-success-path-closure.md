# SbResult Compact Success Path Closure

Date: 2026-07-28
Status: COMPLETE
Plan: `Agentic/Plans/DONE/sbresult-compact-success-path.md`
Branch: `nightrunner-28th-JUL-26`

## Outcome

`SbResult` is a 16-byte, pointer-aligned, no-throw lease over one immutable
diagnostic entry. Success is the null lease and does not touch the store. The
single App-composed `SbDiagnosticStore` remains a fixed 159,760-byte owner with
256 slots; the last result copy releases its slot.

The final implementation preserves the complete 511-byte failure payload,
owner text, immutable copy/move behavior, stale and foreign-store detection,
bounded copy-out, cross-thread consumption, and deterministic Lane F handling
for capacity, lease-count, generation, active-destruction, double-release, and
same-thread lock-re-entry defects. Diagnostic-store construction and lease
traffic allocate no guarded heap memory.

## Final Capacity Census

The final tracked production-source census reports:

- 221 multiline-aware `store.Failure(...)` publication expressions;
- 31 direct `SbResult` members;
- a deliberately conservative bound of 252/256 simultaneous entries, leaving
  four slots of headroom;
- one App-composed production store and no production global or thread-local
  store;
- no `SbResult` vector, array, deque, queue, list, map, or span storage; and
- no supported recursive/re-entrant publication, worker publication, deferred
  result queue, or CPU-thread result handoff.

Copies share one lease and therefore do not publish another entry. The SR2
report's 29-member/250-entry arithmetic was stale: the final declaration review
classifies 31 production members. This is a census correction, not an increased
capacity allowance. Any future multiplicative lifetime path must reopen the
capacity ruling.

## Boundary Proofs

- Core tests pin `sizeof(SbResult) == 16`, pointer alignment,
  `sizeof(SbDiagnosticStore) == 159760`, and no-throw copy, move, assignment,
  and destruction.
- Exact-byte tests cover the maximum 511-byte message, alias-to-alias
  assignment, store identity, stale generations, full 256-slot occupancy,
  reclamation, cross-thread copy, allocation guard, and session high-water.
- Five DX12 architecture tests prove returned diagnostics survive command,
  device, recreation, fault, and optional-feature epoch reset/destruction.
- The operator-editor frame-status witness proves owner reset releases only the
  owner's lease while a returned copy remains valid.
- The same-thread store re-entry probe Lane F fails before mutating the slot
  table, so the store remains fixed-size and non-recursive.

The Automation build exposed stale migration seams that ordinary Profile
compilation cannot see. App now injects its store once into
`InteractionAutomationController`, which constructs the report writer and its
persistent tracer with the same authority. Redundant store parameters were
removed from controller/report operations. The authority-free
`InteractionAutomationReportInputs` courier and its ruling were deleted;
`Write` now takes eleven synchronous operands, below the twelve-parameter
qualitative-review trigger.

Repeated `ConfigureInteractionAutomation` is proved by direct reset review:
the controller resets enable/load/finish flags, script path, actions, status,
and input state; `InteractionAutomationReportWriter::Configure` clears every
report, draw, capture, archive, selection, counter, and path epoch in place.
Store-bound owners and their reserves retain stable identity.

## Performance

`tools\validate_perf.bat` passed on the final source without refreshing a
baseline.

| Witness | Frames | Frame avg / p50 (ms) | Physics avg / p50 (ms) |
|---|---:|---:|---:|
| DX12 | 1,940 | 0.7892 / 0.7562 | 0.2180 / 0.2031 |
| Physics bench | 2,340 | 0.3816 / 0.3562 | 0.0727 / 0.0713 |
| Physics scale 200 | 1,140 | 0.4137 / 0.4062 | 0.1138 / 0.1147 |
| Physics scale 520 | 1,140 | 1.4675 / 1.4687 | 0.8520 / 0.8455 |
| Physics scale 1,000 | 1,140 | 2.0707 / 2.0781 | 1.2124 / 1.2071 |
| Physics scale 2,000 | 1,140 | 3.5977 / 3.5667 | 2.2041 / 2.1808 |
| Physics sleepy 5,000 | 1,140 | 5.0620 / 3.9862 | 2.6979 / 1.4535 |

DX12 frame average was 0.7% below its comparison baseline. Physics-bench frame
average was 8.9% below its comparison baseline. Allocation guard, structural
selected-path proof, absolute budgets, and both comparison gates passed.

## Final Validation

| Proof | Result |
|---|---|
| Formatting and `Related:` resolution | PASS; 571 source files, 1,519 repository paths |
| Project/filter metadata | PASS; 787/787 production items |
| Dependency graph | PASS; 27 include rules, 46 negative edge fixtures, one content rule |
| Ownership inventories | PASS; 85/85 gated aggregates ruled, 1/1 extraction scar ruled, every 12+ signature ruled |
| Allocation-policy scan | PASS; 463 files, zero allowlist errors |
| Automation x64 build | PASS; zero warnings/errors |
| `tools\validate_tests.bat` | PASS; 436/436 cases, 2,419,127/2,419,127 assertions |
| DX12 architecture tests | PASS; 66/66, including five epoch-lease witnesses |
| `tools\validate_perf.bat` | PASS; allocation and performance comparisons clean |
| `tools\run_graphics_stress.bat 1` | PASS; 60.576 seconds, exit 0, descriptor requests 131/131 acknowledged, empty stderr |
| `tools\validate_full.bat` | PASS in 355.5 seconds; CPU/coverage, Automation boundary, DX12 baselines, and byte-exact 44,401-line Physics CSV |

The touched-source comment audit is 18/18 with zero deferred files. Independent
rubber-duck review initially reopened copy-assignment, allocation-first-use,
Automation store ownership, courier ownership, reset, formatting, and lifetime
proofs; after remediation the final verdict was ACCEPTED with no remaining
ownership, behavior, test, comment, or formatting blocker.

## Excluded Owner-Review Diff

The three protected Physics warm-start files were intentionally excluded from
this closure's clean validation source and remain uncommitted for owner
evaluation. No Physics, replay, visual, DX12, or performance baseline was
refreshed.
