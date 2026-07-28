# SbResult Compact Success Path — SR0 Census

Date: 2026-07-28
Phase: SR0 — measure value flow and lifetime
Result: Complete

## Scope

This phase measured the current `SbResult` representation, every production
construction path, every returning definition, direct storage and transfer
shapes, queue/thread boundaries, and diagnostic consumers. It also ran the
existing performance gate in a clean detached worktree. No source, test,
oracle, or baseline changed.

## Representation And Maximum Failure Witness

The current Win64 MSVC representation is:

| Property | Measured value |
|---|---:|
| `sizeof(Core::SbError)` | 520 bytes |
| `alignof(Core::SbError)` | 8 bytes |
| `sizeof(Core::SbResult)` | 528 bytes |
| `alignof(Core::SbResult)` | 8 bytes |
| Standard-layout | yes |
| Trivially copyable | no |
| Trivially destructible | yes |
| Nothrow copy/move construction and assignment | yes |

An isolated compiled probe constructed
`SbResult::Failure("SR0", "%s", payload)` with exactly 511 `x` bytes. The
observed owner was `SR0`, message length was 511, the first and last payload
bytes were both `x`, and byte 512 was the terminating null. This preserves the
protected maximum-payload witness at the current tip.

## Construction And Returning-Definition Census

The current production tree contains:

| Shape | Count |
|---|---:|
| Named definitions returning `SbResult` | 176 |
| Explicit trailing-return lambda returning `SbResult` | 1 |
| Total returning definitions | 177 |
| Frame-reachable definitions | 57 |
| Scene-load/resource-build definitions | 51 |
| Cold/on-demand definitions | 69 |
| `SbResult::Success()` constructions | 200 |
| `SbResult::Failure()` constructions | 220 (SR2 multiline-aware correction; originally reported as 176) |
| Production source/header files mentioning `SbResult` | 116 |

Tests add four success constructions and 17 failure constructions across five
files. The 177-definition classification reconciles exactly: 57 + 51 + 69 =
177. The returning-definition classification remains unchanged by the
multiline factory-count correction below.

### SR2 census correction

SR2 reran the construction census with a whitespace/multiline-aware expression
matcher before relying on the 256-slot capacity proof. The original 176 count
was not complete. At the SR0/SR1 source tip there are 220 factory expressions:
178 have `SbResult::Failure(` on one line, while 42 are formatted with
`SbResult::` and `Failure(` on separate lines. The reported 176 is the
same-line set less the two `ApplicationExitState` reconstruction factories,
which the earlier evidence treated as storage/consumption rather than new
producers.

The corrected conservative SR1 input is therefore 220 factory expressions plus
30 result-member/aggregate sites, or 250/256. The decision still fits, but with
six entries of conservative headroom rather than the originally reported 50.
SR2's final source has 221 explicit store publication expressions plus 29
result-member sites, also 250/256. This correction changes only the capacity
census; the returning-definition, transfer, diagnostic-consumer, and
performance evidence above is unchanged.

## Storage, Copy, And Transfer Census

There are 30 direct production data members or aggregate fields of type
`SbResult`:

- Seven are retained owner/transaction state: `Run::m_lastSceneLoadResult`,
  `ImGuiEditorOwner::m_frameCommandStatus`,
  `Dx12RaytracingOwner::m_featureResult`, and four DX12 first-failure fields.
- The remaining 23 are returned aggregate or stack values used within the
  creating operation or frame.

There is no direct production function parameter that takes `SbResult` by
value. Explicit direct parameters are read-only references used for reporting,
matching, or retention, except
`SaveCompletePublication(..., Core::SbResult& outSaveResult)`, which writes a
synchronous output. A few frame-local aggregate wrappers that contain an
`SbResult`, such as `RuntimeUIFrameResult`, are passed by value.

No production path explicitly applies `std::move` to an `SbResult`, and no
`SbResult`-specific `vector`, `array`, `optional`, queue, or other container was
found. Ordinary return, aggregate, assignment, and retained-state copies
therefore account for the value transfers.

The retained-state inventory is:

| Owner | Retained lifetime |
|---|---|
| `Run::m_lastSceneLoadResult` | Startup/last-load owner lifetime, until overwritten |
| DX12 first-failure fields | Command/recreation/health/fault epoch, until reset, commit, abort, or reinitialization |
| `Dx12RaytracingOwner::m_featureResult` | Raytracing owner lifetime, until feature state changes |
| `ImGuiEditorOwner::m_frameCommandStatus` | One editor frame, copied out and reset by `EndFrame` |

`ApplicationExitState` is already an explicit lifetime bridge: it copies owner
and message bytes into owner-held storage, then reconstructs an `SbResult` at
the process boundary. Its owner pointer borrows the state lifetime. All other
aggregate results are consumed synchronously within a call or frame.

## Queue And Thread Boundaries

The census found zero CPU-thread handoffs and zero deferred queues that store
an `SbResult`.

- Scene, editor, and capture queues store request or command payloads, not
  result values; validation and drain statuses are consumed synchronously.
- `WorkerPool` callers consume results only after joined work; a result is not
  handed to a worker.
- Replay probe request values advance synchronously inside their owning
  startup/tick workflow.
- DX12 GPU queues do not carry `SbResult`; CPU DX12 owners retain their first
  failure across calls or command epochs.

This is a current-source fact, not permission to omit a thread-safety policy
from a future diagnostic store. A new queued or cross-thread carrier must add
an owner-held immutable entry valid through consumption and prove stale-handle
detection.

## Diagnostic Consumers

Outside `SbResult.h`, 74 production source lines in 32 files directly read
`.error.owner` or `.error.message`. Those consumers fall into five groups:

1. process-exit conversion and owned retention;
2. stderr, log, and diagnostic-event publication;
3. UI/status presentation;
4. status aggregation and propagation; and
5. eight direct Lane F conversions in the authored parser, convex hull, and
   `SceneController`.

Every current `Failure` producer supplies either a static owner literal or an
owner already copied into `ApplicationExitState` or `ReplayProbeState`.
Failure storage must therefore continue to copy both owner and message bytes;
retaining borrowed producer pointers is not sufficient.

## Current Performance Witness

`tools\validate_perf.bat` passed in a detached clean worktree at
`d3c76a4e` after the worktree was supplied with the repository's existing
ignored third-party source caches. The initial attempt stopped during setup
because those ignored ImGui/Tracy files were absent; it did not reach a
repository source failure. The passing run took 154 seconds and did not invoke
any baseline-refresh command.

| Workload / marker | Current average | Baseline average | Average delta | Current p50 | Current p99 |
|---|---:|---:|---:|---:|---:|
| DX12 / `Frame` | 0.8016 ms | 0.7948 ms | +0.86% | 0.7676 ms | 1.2721 ms |
| DX12 / `Frame/Input` | 0.0681 ms | 0.0476 ms | +43.07% | 0.0604 ms | 0.1702 ms |
| DX12 / `Frame/VsyncWait` | 0.4480 ms | 0.4624 ms | -3.11% | 0.2856 ms | 2.7106 ms |
| Physics / `Frame` | 0.4364 ms | 0.4189 ms | +4.18% | 0.4145 ms | 0.7945 ms |
| Physics / `Frame/Input` | 0.0651 ms | 0.0418 ms | +55.74% | 0.0572 ms | 0.1566 ms |
| Physics / `Frame/VsyncWait` | 0.4408 ms | 0.4325 ms | +1.92% | 0.2595 ms | 2.9448 ms |

The gate accepted the complete DX12, Physics, and scale matrix. Individual
short markers are noisy and are recorded as evidence rather than a new
baseline. SR3 must compare the final compact carrier against this same gate
without refreshing its baseline.

## SR1 Decision Inputs

SR0 rules out an owner-local call slot as the complete design: `Run` and DX12
copies remain live after the producer returns, and aggregate copies can extend
the lifetime further. A split status/detail API would require dual-value
plumbing across the complete consumer surface and could separate status from
its diagnostic; it is not the shortest safe migration.

The leading SR1 candidate is therefore a bounded owner-managed immutable
diagnostic store plus a compact success/failure carrier. SR1 must decide and
test all of these invariants before implementation:

1. A failure copies owner and up to 511 message bytes into the store before its
   compact handle is published; success requires no store access.
2. A handle identifies both slot and generation. Reuse cannot silently bind a
   live carrier to unrelated detail, and stale lookup has deterministic tested
   behavior.
3. Capacity, reclamation epoch or lease lifetime, high-water reporting, and
   exhaustion behavior are explicit. A fixed ring that can overwrite a live
   retained failure is invalid.
4. If the compact carrier is trivially copyable, reclamation must be controlled
   by owner epochs that outlive every possible carrier copy. If entries are
   reference-counted, the compact carrier becomes a non-trivial lease and must
   remain allocation-free.
5. The store either enforces one owning thread or synchronizes publication and
   lookup. Any future queue/thread handoff must extend the immutable entry
   lifetime through consumption.
6. Process, frame, startup, editor, and renderer lifetimes must be named
   explicitly. The longest current witnesses are the `Run` and DX12 retained
   states, not a single producer call.

No owner input is required to start SR1: the approved bounded owner-managed
store, no-wrapper migration, current lifetime census, and design questions
above are sufficient for the next agent to compare concrete models.

## Evidence Commands

- CodeGraph status and focused `SbResult` symbol/caller exploration.
- Current-source definition, factory, field, parameter, member-access, queue,
  and thread-boundary inventories.
- Temporary MSVC x64 compile/run of the size/trait/511-byte witness; the probe
  was removed after measurement.
- `tools\validate_perf.bat` in the detached clean worktree; pass, no baseline
  refresh.
