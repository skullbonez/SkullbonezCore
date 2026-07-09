# Plan 07 Allocation Gate Inventory

Date: 2026-07-10
Plan: `engine-cleanup-plans/07-allocation-gate-right-sizing.md` Step 1.1

## Requirement

Owner decision stands: runtime allocation policy is global zero allocation by
default. Replay is the only approved runtime allocation exception, and replay
growth must remain routed through registered owner/phase/cap/counter diagnostics.
The simplification target is the machinery, not the guarantee.

## Inventory Commands

- `codegraph status .` reported the index up to date.
- `codegraph explore "RuntimeAllocationTracker RuntimeReserveAllocator check_allocation_policy validate_perf allocation guard RuntimeAllocationScope RuntimeReserveOwnerHandle ReserveForReplayGrowth"`
- `python tools\check_allocation_policy.py --repo .`
- `python tools\check_allocation_policy.py --self-test`
- Targeted `rg` reads for allocation scopes, reserve owner/growth callsites,
  static checker findings, and broad STL growth vocabulary.

## Current Enforcement Surfaces

| Surface | Current coverage | Keep | Simplify / Delete candidate |
|---------|------------------|------|-----------------------------|
| `RuntimeAllocationTracker` (`.cpp` 715 lines, `.h` 77 lines) | Global C++ `operator new`/`delete` overloads, fixed allocation header, phase counters, gameplay violation counter, top callsite report, `--allocation-guard gameplay` fail path. | Keep the process-wide hook, phase scopes, fixed storage, pass/fail gameplay violation count, and end-of-process summary. | The 1024-slot callsite table, 5-frame `CaptureStackBackTrace`, image-base RVA normalization, and verbose per-callsite ranking are diagnostics-heavy for a gate whose product contract is pass/fail. Consider trimming to bounded owner/phase samples or making deep callsite capture opt-in. |
| `RuntimeReserveAllocator` (`.cpp` 695 lines, `.h` 158 lines) | Fixed owner registry, replay growth request gate, owner/growth scopes, policy-violation count, 256-entry growth ring, owner summaries. | Keep registered owners, owner scopes, `RequestGrowth`, replay-only approval, hard caps, growth counters, and policy violations feeding `RuntimeAllocationGuardHasGameplayViolations()`. | Duplicate `RuntimeReservePhase`/`RuntimeAllocationPhase` plus manual index mapping can collapse. Registry capacity 160 and growth-event ring 256 are larger than current use. `atomic_flag` ring bookkeeping is more diagnostic ceremony than policy enforcement. |
| `tools/check_allocation_policy.py` (429 lines) + `tools/allocation_policy_allowlist.json` (200 lines) | Scans 296 source files for direct heap APIs and reserve-growth calls; checks two hot store headers for dynamic STL members; enforces allowlist metadata and stale-pattern failures. | Keep direct heap API detection, reserve-growth call detection, metadata fields (`owner`, `phase`, `reason`, `cap`, `removal_or_wrapper_plan`), stale allowlist failures, and synthetic self-tests. | Dynamic STL/growth coverage is too narrow: current `DYNAMIC_STL_MEMBER_TARGETS` only names `PhysicsBodyStore.h` and `ColliderStore.h`. Static checker should become a broad pass/fail runtime allocation API/growth guard rather than a narrow spelling check or frozen count. |
| `tools/validate_perf.bat` | Builds Profile, runs `check_allocation_policy.py`, runs `SKULLBONEZ_CORE.exe --allocation-guard gameplay --frames 180 --scene perf_1000`, requires `[allocation-guard] PASS:`, then runs DX12 and physics perf comparisons. | Keep `validate_perf` as the runtime allocation PR gate for allocation policy changes. The allocation guard launch is the only current end-to-end runtime enforcement run. | Consider isolating the allocation guard smoke into a smaller reusable helper if Step 2/3 needs faster iteration, while keeping `validate_perf` as the pre-commit gate. |
| Memory UI / diagnostics (`UITabMemory`, `RunUiTextPass`) | Displays reserve growth count/recent events and memory diagnostics. | Keep user-visible evidence that replay growth is counted and capped. | If the growth ring is simplified, update UI to display the smaller evidence shape instead of preserving the ring solely for the UI. |

## Static Checker Evidence

`python tools\check_allocation_policy.py --repo .`:

- `scanned=296`
- `direct_heap_findings=30`
- `dynamic_stl_member_findings=0`
- `allowlist_errors=0`

Breakdown from the checker APIs:

| Finding kind | Count | Status |
|--------------|------:|--------|
| `make-unique` | 11 | all allowlisted |
| `free` | 7 | all allowlisted |
| `reserve-bump` | 5 | all allowlisted |
| `new` | 4 | all allowlisted |
| `malloc` | 2 | all allowlisted |
| `delete` | 1 | all allowlisted |

The 30 findings appear in 13 paths. Allowlist metadata has 17 rows over those
13 paths. The self-test reports:

```text
SELF_TEST_PASS: allocation policy checker synthetic cases passed
```

Broad vocabulary scan of current runtime/source roots shows why Step 3 must
expand static coverage instead of keeping the two-header dynamic member target:

| Pattern | Current hits |
|---------|-------------:|
| `std::vector` | 863 |
| `std::string` | 312 |
| `.push_back(` | 259 |
| `.reserve(` | 162 |
| `.resize(` | 64 |

Those numbers are not a ratchet and should not become one. They show that the
current static checker does not yet describe the global policy surface.

## Reserve Exception Evidence

Current registered reserve-owner creation sites are:

- `PhysicsWorld.cpp`: `replay_solver_snapshot`
- `ReplayPredictionReserve.cpp`: `replay_prediction_working_set`
- `ReplayRecorder.cpp`: `replay_recorder_samples`

Current `RequestGrowth` callers outside the allocator implementation are:

- `PhysicsWorld.cpp` for solver replay snapshots
- `ReplayPredictionReserve.cpp` for prediction working-set growth
- `ReplayRecorder.cpp` for retained replay sample payload growth

Replay growth consumers wrap approved allocations with `RuntimeAllocationScope`
`Replay`, `RuntimeReserveOwnerScope`, and `RuntimeReserveGrowthScope`. The
global allocation hook then treats only that owner/phase/depth tuple as approved
replay growth; unregistered replay heap traffic remains a violation.

## Keep / Simplify / Delete

Keep:

- Global `--allocation-guard gameplay` enforcement and process exit failure on
  gameplay policy violations.
- Phase scopes for startup, scene load, backend init, steady gameplay, physics,
  render, replay, capture, diagnostics, and shutdown.
- Replay-only `RuntimeReserveAllocator::RequestGrowth` with registered owner,
  hard cap, growth counter, and policy violation reporting.
- Static checker allowlist metadata and stale allowlist detection.
- `validate_perf` as the final allocation-policy gate.

Simplify:

- Trim runtime guard diagnostics to the minimum evidence needed to find and fix
  violations: phase, owner, count, bytes, and a bounded optional sample.
- Collapse duplicate allocation/reserve phase enums or make the mapping table
  explicit and self-tested.
- Reduce reserve registry/growth event capacities to values justified by the
  current replay-only exception, or document why the extra capacity is kept.
- Move repeated replay reserve growth ceremony behind fewer domain helpers where
  possible without hiding owner/phase/cap evidence.
- Expand static enforcement to broad runtime heap/STL growth APIs with
  allowlisted cold-phase exceptions, rather than two special store headers.

Delete / avoid:

- Do not reintroduce frozen `MAX_*` allocation budgets.
- Do not replace the old governance checker with vocabulary policing.
- Do not narrow policy to physics/render hot paths.
- Do not add non-replay runtime allocation exceptions without a new owner
  decision and matching allowlist/runtime evidence.

## Next Steps

Step 2.1 completed on 2026-07-10:

- Runtime reserve phases now alias the allocation guard phase type, deleting the
  duplicate enum and manual switch mapping.
- Runtime allocation callsite diagnostics now capture callsite plus parent
  frame only, deleting the extra grandparent/caller slots and verbose summary
  columns while preserving fixed storage and pass/fail counters.
- `ResetRuntimeAllocationCounters()` now resets per-callsite violation counts.
- `tools\validate_perf.bat` passed with allocation guard gameplay violations 0
  and reserve policy violations 0.

Next ordered work:

1. Step 3.1 should broaden static allocation enforcement so direct heap APIs,
   reserve growth, and runtime STL growth are caught through pass/fail rules and
   reviewed allowlist metadata.
2. Step 4.1 should update `AGENTS.md`, this plan, and `Agentic/SessionState.md`
   after the implementation proves the simplified shape.
