# SbResult Compact Success Path - SR1 Ownership Decision

Date: 2026-07-28
Phase: SR1 - select the compact diagnostic ownership model
Result: Complete

## Decision

Use one App-composed `Core::SbDiagnosticStore` with 256 fixed immutable slots.
A failed `SbResult` is an allocation-free lease on one slot; a successful
`SbResult` is the null lease and never touches the store.

On Win64 the target carrier is exactly 16 bytes:

| Field | Size | Meaning |
|---|---:|---|
| `SbDiagnosticStore*` | 8 bytes | Null for success; process-composed store for failure |
| packed diagnostic token | 8 bytes | Eight-bit slot index plus 56-bit generation |

`SbResult` remains `[[nodiscard]]`, nothrow copyable/movable, and
allocation-free, but it is deliberately non-trivial. A failed copy retains the
same entry, a move transfers the lease and makes the source successful, and
destruction releases the lease. The slot becomes reusable immediately after
the last result copy releases it. This is the shortest lifetime that satisfies
every current consumer; it does not keep diagnostics until the end of a frame,
scene, device epoch, or process when no result still refers to them.

The producer API becomes explicit:

```cpp
return diagnostics.Failure( "Rendering/DX12", "CreateFence failed (0x%08X)", code );
```

Success remains `SbResult::Success()`. Consumers migrate from public `.ok` and
`.error` fields to `Ok()`, `ErrorOwner()`, and `ErrorMessage()`. The old static
`SbResult::Failure` producer and public inline `SbError` representation are
deleted in the same implementation slice. There is no forwarding overload,
compatibility result type, global store accessor, service bag, or callback
facade.

## Why This Model Fits The Measured Lifetimes

SR0 found 176 production failure-factory sites and 30 direct production result
fields. Seven fields are retained state and 23 are synchronous
stack/returned aggregates. Copies of one failure share one slot, so capacity
measures distinct simultaneously live failures rather than the number of result
objects. The current call graph has no recursive or re-entrant failure
publication, no worker-thread failure publication, and no result containers,
deferred queues, or CPU-thread handoffs.

The longest current witnesses are not producer calls:

- `Run::m_lastSceneLoadResult` survives startup and remains available to the
  process boundary until it is overwritten or `Run` is destroyed.
- `Dx12CommandRecordingState`, `Dx12DeviceHealthState`,
  `Dx12RecreationTransaction`, and `Dx12FaultInjectionState` retain the first
  failure until their named reset/commit/reinitialization boundary.
- `Dx12RaytracingOwner::m_featureResult` survives the capability/setup epoch.
- `ImGuiEditorOwner::m_frameCommandStatus` survives until `EndFrame` copies it
  out and resets the owner.
- `ApplicationExitState` preserves the first owned failure until the platform
  exit code is resolved.

A lease follows all of those real copies without imposing one coarse epoch on
unrelated owners. Resetting an owner releases only that owner's copy; a result
already returned to a caller keeps the immutable entry alive through its own
consumption.

The conservative current-source simultaneous-live bound is 206 entries:

- at most 176 active failure-factory sites when the verified call graph has no
  recursion, re-entry, or worker-thread publication; plus
- at most 30 retained/aggregate storage sites, counted as if each held a
  distinct additional failure even though legal propagation copies an existing
  lease and therefore does not consume another slot.

The 256-slot store covers that hard bound. The proof intentionally
over-counts ordinary propagation rather than relying on typical fail-fast
depth. Last-lease reclamation prevents cumulative consumption, so repeated
recoverable failures do not exhaust the store merely because the process runs
for a long time.

This capacity ruling is conditional on the measured shapes. A new recursive or
re-entrant failure producer, worker-thread failure publication, result
container, deferred queue, or other multiplicative retention path reopens the
bound and must update this decision before landing. SR2 and SR3 must rerun the
post-migration producer, call-graph, retained-field, container/queue, and thread
handoff censuses and reject the 256-slot proof if any assumption changed.
Capacity is a fixed resource contract, not an allowance or count ratchet.

With a 96-byte owner, 512-byte message, 64-bit generation, lease count, and
alignment, each implementation entry is expected to occupy about 624 bytes.
The fixed array therefore costs about 156 KiB; including its lock, counters,
and alignment, the complete process-owned store is approximately 160 KiB.
SR2 must record the compiled `sizeof(SbDiagnosticStore)` rather than treating
this design estimate as measured output.

## Slot And Token Contract

Each slot owns:

- a 96-byte owner buffer;
- a 512-byte message buffer;
- its generation and lease count; and
- immutable published bytes until the lease count reaches zero.

The owner bound reuses the existing `ApplicationExitState` owner capacity.
Current literal owners have a measured maximum of 32 bytes; other producers
use named internal owner constants or existing bounded owner state. SR2 must
reject, not truncate, an owner that cannot fit 95 bytes plus the null
terminator. Owner overflow is an internal attribution defect and therefore
Lane F.

Message formatting preserves the current behavior: zero the 512-byte
destination, call bounded `vsnprintf`, retain all 511 payload bytes, and keep
the terminating null. A null format retains the current
`"recoverable operation failed"` fallback. Owner and message bytes are copied
before the lease is published; no result borrows producer storage.

Token zero is the success sentinel. Failure tokens pack the slot index into
eight low bits and a nonzero generation into the remaining 56 bits. A slot
increments its generation before each publication. A released token can
therefore never resolve to a later entry in the same slot. Generation wrap
retires/fatals before publication rather than aliasing. Lease-count overflow,
double release, an impossible stale access through a live result, and store
exhaustion are also deterministic Lane F failures.

The opaque `SbDiagnosticIdentity` contains both the store pointer and packed
token. The store pointer is part of identity: equal token bits from two stores
do not identify the same diagnostic.

The copy-out API returns:

```cpp
enum class SbDiagnosticCopyStatus
{
    Copied,
    SuccessIdentity,
    ForeignStore,
    Stale
};
```

`CopyDiagnostic` accepts an identity plus fixed 96-byte owner and 512-byte
message destinations, validates and copies under the store lock, and returns
one status above. Non-`Copied` results leave deterministic empty strings. It
never returns an `SbResult` and never exposes an unleased slot pointer.

`ErrorOwner()` and `ErrorMessage()` are direct borrowed accessors. A returned
pointer is valid only while that same `SbResult` object remains alive, remains a
failed lease, and has not been moved from or assigned. Another copy keeping the
slot alive does not extend a pointer borrowed from an object whose own lease
ended. Code that needs bytes after a move, assignment, destruction, queueing,
or other escape must keep the original result lease alive or use
`CopyDiagnostic` into owner-held bounded buffers.

## Thread And Allocation Policy

One allocation-free `atomic_flag` lock serializes failure publication, failed
result retain/release, generation checks, and diagnostic copy-out. Entry bytes
are immutable while leased. A result copy may therefore cross a future CPU
thread safely, provided ordinary object-transfer synchronization is used; the
entry remains valid until that consumer releases its copy. Concurrent access
to the same `SbResult` object without synchronization remains a normal C++ data
race and is not made legal by the store.

Success construction, success copy/move, `Ok()`, and success destruction do
not access the store or acquire the lock. The fixed slot array is constructed
once before runtime owners and performs no heap allocation. The store exposes
capacity, active-entry, and session high-water counters; shutdown reports the
high-water value without changing allocation policy.

No code may log, invoke an engine callback, publish an event, or enter another
result-producing operation while holding the store lock. Formatting and fixed
byte copies stay inside the critical section; every Lane F report is selected
under the lock, then emitted only after unlocking. This prevents recursive
failure publication and lock-order inversion through diagnostics.

Lane separation is explicit:

- Lane R uses a successfully published immutable diagnostic slot.
- Lane F handles capacity, generation, lease, owner-bound, and stale-live-result
  invariant failures.
- Lane P tests probe the contract but do not become production error handling.

## App Composition And Migration Path

SR2 must make the store the first relevant `WinMain` local in
`Runtime/App/Init.cpp`: it is constructed before `HandleGenAtlas`, config and
argument setup, standalone Physics handling, `Window`, `RenderBackendDX12`, and
`Run`. Reverse destruction order then keeps the store alive after every
startup, Physics, `Run`, renderer, window, and worker-owned result copy has been
released.

Long-lived concrete owners receive `SbDiagnosticStore&` in their constructors.
Free or short-lived operations receive the same concrete reference directly as
their first ownership operand. The migration must not hide it in a Runtime
context, service registry, process singleton, thread-local binding, callback
pack, or one-field courier. Adding the reference to a 12-or-more-parameter
operation requires the standing qualitative wide-signature review; a parameter
bag is not a remedy.

The composition path is:

1. `WinMain` constructs `SbDiagnosticStore` before `HandleGenAtlas`, config,
   standalone Physics, and every other migrated Lane R producer.
2. Startup/config/standalone helpers that can produce Lane R receive it.
3. `Window` and `RenderBackendDX12` receive it before window/backend init.
4. `RunApp` and `Run` receive it before Runtime, UI, scene, and replay owners
   are constructed.
5. DX12 child owners receive the same store through concrete constructors.
6. `ApplicationExitState` retains a compact failed result directly instead of
   copying into a second owner/message buffer and reconstructing a new result.
7. Store shutdown/high-water reporting occurs only after all owners are gone.

This preserves Core as the infrastructure floor: the store lives under
`Core/`, while App owns its instance and higher layers receive only the concrete
Core reference.

## Rejected Alternatives

### Owner-local diagnostic slot

Rejected as the complete model. One slot per producer call would dangle after
return, while one mutable slot per subsystem could overwrite a still-live
`Run`, DX12, UI, or returned aggregate copy. Distributing generation/retention
rules across every owner would create several partial stores rather than one
invariant owner.

### Split status and detail values

Rejected. It requires parallel plumbing across all 177 returning definitions
and lets status escape without the diagnostic that explains it. It does not
shorten the real retained lifetime and gives `ApplicationExitState` and DX12
first-failure owners two values whose coherence must be maintained by caller
discipline.

### Monotonic process table or overwriting ring

Rejected. A monotonic table makes capacity depend on process duration; an
overwriting ring can silently retarget a live failure. Last-lease reclamation
and generation checking remove both defects.

### Global or thread-local current store

Rejected. Hidden lookup makes failure-producing authority invisible, creates
startup/binding order hazards, and avoids the deliberate API migration the
owner requested.

## Focused SR2/SR3 Test Matrix

| Area | Required proof |
|---|---|
| Representation | Win64 `sizeof(SbResult) == 16`, success token is zero, `[[nodiscard]]`, nothrow copy/move/destruction |
| Success cost | construct/copy/move/destroy success without lock, slot, allocation, or high-water change |
| Message | exact owner bytes; null owner fallback; exact 511-byte payload and terminator; null-format fallback |
| Owner bound | 95-byte owner succeeds byte-exact; 96-byte owner is a child-process Lane F |
| Copy lifetime | copy survives source destruction; multiple copies share one slot; self-copy and same-entry copy assignment preserve exactly one lease per result |
| Move lifetime | move transfers without retain/release churn and leaves source successful; self-move is a no-op; move-over-live releases the destination's old entry exactly once |
| Assignment matrix | success-to-failure, failure-to-success, failure-to-distinct-failure, and same-entry assignments preserve status, bytes, active count, and lease counts |
| Reclamation | last release decrements active count and makes the slot reusable |
| Borrow lifetime | direct owner/message pointers are used only while the same result remains alive, failed, and unmoved/unassigned; retained bytes use bounded copy-out |
| Copy-out | exact owner/message bytes, deterministic empty outputs for non-copied statuses, and no returned `SbResult` or slot pointer |
| Stale handle | released identity copy-out reports `Stale`; reused slot has a new generation and never returns new bytes for the old identity |
| Cross-store identity | equal packed token values from two stores remain distinct; lookup through the other store reports `ForeignStore` |
| Capacity | 256 distinct live failures succeed; the 257th is a deterministic child-process Lane F; releasing one permits one new failure |
| Capacity assumptions | post-migration census proves no recursive/re-entrant or worker-thread failure publication and no result containers/queues; an introduced shape blocks the bound |
| Counter safety | lease-count and generation overflow seams fail Lane F rather than wrap/alias; an explicit child-process double-release probe fails Lane F |
| Threading | test-only concurrent failure publication stays within the 256-slot capacity, serializes complete immutable entries, and drains active count to zero; cross-thread result copies preserve bytes; no current production worker publishes a failure |
| Lock re-entry | fatal/log/callback paths run after unlock; test instrumentation proves no callback or result publication occurs inside the critical section |
| App exit | first owned failure wins and survives producer destruction without duplicate inline buffers |
| Runtime/UI | last scene-load and editor-frame failure copies survive owner reset until caller consumption |
| DX12 epochs | command, device-health, recreation, fault, and optional-feature failures survive their named epochs; reset releases only the owner copy |
| Allocation | runtime allocation guard observes zero allocations for store construction, success traffic, and failure publication/copy/release |
| API deletion | old static failure producer and public `.ok`/`.error` result surface are absent; no compatibility overload/type/global accessor exists |
| Performance | focused success benchmark plus unchanged `validate_perf` baseline; no refresh |

## Validation

Documentation-only decision phase. No repository validation was required and
no baseline was refreshed. SR2 owns implementation and focused compilation;
SR3 owns the complete test, performance, full-gate, comment-audit, and
independent-ownership-review evidence.

No owner question remains for implementation.
