# SbResult Compact Success Path - SR2 Implementation

Date: 2026-07-28
Phase: SR2 - implement and migrate
Result: Complete

## Outcome

`SbResult` is now the selected 16-byte pointer/token lease. One
App-composed `Core::SbDiagnosticStore` owns 256 fixed immutable entries;
success is the all-zero result and never touches the store. Failed copies
retain one entry, moves transfer their lease, the final release reclaims the
slot, and an eight-bit slot plus 56-bit generation rejects stale reuse.

The old static `SbResult::Failure`, public `.ok`, and public `.error` API is
deleted. All production producers now receive the concrete diagnostic store
through an existing owner or an explicit function parameter. No forwarding
result, global/thread-local lookup, services aggregate, callback pack, or
one-field courier was added.

## Representation And Store Contract

The compiled MSVC x64 sizes are:

| Type | Size |
|---|---:|
| `SbResult` | 16 bytes |
| `SbDiagnosticStore` | 159,760 bytes |

The store contains 256 entries, each with a 96-byte owner, 512-byte message,
64-bit generation, and 32-bit lease count. One allocation-free
`atomic_flag` lock serializes publication, failed-result retain/release,
identity lookup, copy-out, and counters. Published bytes remain immutable
while any lease exists.

Failure formatting preserves all 511 message payload bytes plus the null
terminator. A 95-byte owner is accepted byte-exact; a 96-byte owner is an
internal attribution defect and takes Lane F. Capacity exhaustion, generation
exhaustion, lease overflow, stale live access, double release, and store
destruction with an active lease also take Lane F only after the store lock
has been released. The store destructor is intentionally `noexcept`: the
lifetime guard terminates rather than unwinding before any raw result pointer
can outlive its store.

`SbDiagnosticIdentity` includes both the store pointer and packed token.
`CopyDiagnostic` returns `Copied`, `SuccessIdentity`, `ForeignStore`, or
`Stale` and leaves deterministic empty destinations for every non-copied
result. `ErrorOwner()` and `ErrorMessage()` remain direct immutable borrows
whose lifetime ends when that exact result is moved from, assigned, or
destroyed. The App store is constructed before all result-producing owners and
outlives every failed lease.

## App Composition And Migration

`Runtime/App/Init.cpp` constructs the single production store as the first
relevant `WinMain` local. Startup parsing, config, standalone Physics, Window,
DX12, Run, Runtime, Scene, Replay, UI, and tool owners all receive that same
store through visible concrete seams. Reverse local and member destruction
keeps the store alive after every result-bearing owner.

Every normal `WinMain` return passes through the narrow
`ReportDiagnosticStoreSession` behavior owner while the App store is still
alive. It snapshots active and session-high-water counters through their
locked accessors, then reports both counters and fixed capacity to stdout and
the event log only after the lock has been released. The worker self-test
production exit reports `active_entries=0 session_high_water=0 capacity=256`.

`ApplicationExitState` now retains the first compact failed lease directly.
Resolving an exit returns another lease on the same immutable entry, so the
returned failure remains valid after the exit-state owner is destroyed without
copying owner/message bytes into a second buffer.

The migration updates the Core, Assets, Physics hull-loading, Rendering/DX12,
Scene, World, Runtime, UI, production and focused-test project manifests, and
all affected unit-test callers in one API slice.

## Post-Migration Capacity Census

The final-source conservative census reports:

- 221 production `store.Failure(...)` expressions, counted across line breaks;
- 29 direct `SbResult` members in production headers;
- a deliberately over-counted maximum of 250 distinct live entries when every
  producer expression and every result member is treated as a simultaneous
  independent failure;
- one production `SbDiagnosticStore` object definition, the App-composed
  `WinMain` local;
- zero production global or thread-local diagnostic stores;
- zero production `SbResult` vector, array, deque, queue, map, or span storage
  declarations; and
- no new recursion, re-entry, worker publication, deferred result queue, or
  CPU-thread result handoff.

The 256-entry contract therefore still covers the final migrated source with
six conservatively counted entries of headroom. Ordinary propagation copies an
existing lease and does not consume another entry, so this proof intentionally
over-counts the real live set. Any future recursive/re-entrant producer, worker
publication, deferred queue, result container, or other multiplicative
retention path reopens the capacity ruling.

### SR0/SR1 reconciliation

The earlier 176-producer input was incomplete. A multiline-aware scan of the
pre-SR2 tip finds 220 actual `SbResult::Failure(...)` expressions:

- 178 keep `SbResult::Failure(` on one line;
- 42 are clang-formatted as `SbResult::` followed by `Failure(` on a later
  line; and
- the reported 176 equals the same-line set less the two
  `ApplicationExitState` reconstruction factories that the earlier evidence
  treated as storage/consumption.

SR2 replaces the two exit-state reconstruction factories with one generic
nonzero-exit publication because a retained owned failure now returns the same
lease. Replay probe migration replaces one shared hidden static factory with
three visible concrete-store publication expressions. The final publication
count is therefore 220 - 1 + 2 = 221. The direct result-member count falls from
30 to 29, so both the corrected decision-tip bound and final bound are
250/256.

This is a census correction, not a capacity-budget increase. It leaves only six
conservatively counted entries of headroom and is explicitly flagged for the
SR3 independent reviewer. Tests and test-only concurrent publication are not
part of the production capacity sum.

## Focused Proof

The focused test target covers:

- the 16-byte success sentinel and zero store counter/high-water traffic;
- exact maximum owner and 511-byte message formatting plus null fallbacks;
- copy, copy assignment, move, move assignment, self-copy, self-move, and
  final-lease reclamation;
- success, foreign-store, live, and stale copy-out identities;
- slot generation advancement after reuse;
- all 256 live slots and reuse after final release;
- eight concurrent publishers retaining 128 immutable identities and checking
  every exact `thread=N` owner / `result=M` message pair before release;
- deterministic capacity, owner-overflow, double-release, and
  store-destruction-with-active-lease Lane F child probes; and
- `ApplicationExitState` lease survival after its owner is destroyed.

Affected DX12 architecture, Scene parser, and Runtime interaction policy test
executables also compile and pass through the explicit store API.

## Ownership Inventories

All current-source inventories pass:

- wide signatures: 32 rows at the qualitative 12-parameter review trigger,
  all with current rulings;
- authority-free aggregates: strict pass; and
- extraction scars: pass.

Five signatures crossed or entered the review trigger because the explicit
store borrow became visible: `BeginRuntimeUIFrame`, two Replay restore phases,
`RenderResourceLifecycle`, and `RuntimeRenderer`. Their current rulings retain
the concrete phase/owner because all borrows share one synchronous operation
or owner lifetime; collecting them into a parameter/services aggregate would
hide authority without creating an invariant.

## Comment Audit

The touched-file audit covers 166 source-bearing files, excluding only the
three protected warm-start files. All 166 have the required learning-header
structure and were checked against the post-migration ownership/lifetime
claims; zero files are deferred or unchecked.

The audit added the exact diagnostic-store/result lifetime and shutdown
high-water reporting rules, corrected
`ApplicationExitState` comments that still described an inline byte copy,
documented the fatal-child helper, and replaced two deletion-bound TODO
`Related:` links with their permanent unit-test closure report. All
repository-relative `Related:` paths resolve after this report is present.

## Validation

| Proof | Result |
|---|---|
| `SKULLBONEZ_CORE.vcxproj` Debug x64 build | Pass |
| `SKULLBONEZ_TESTS.vcxproj` Debug x64 build | Pass |
| `*SbResult*` focused cases | Pass: 3/3 cases, 38/38 assertions |
| `*SbDiagnosticStore*` focused cases | Pass: 6/6 cases, 8,435/8,435 assertions |
| `*Application exit*` focused case | Pass: 1/1 case, 3/3 assertions |
| DX12 architecture unit tests | Pass |
| Scene parser unit tests | Pass |
| Runtime interaction policy tests | Pass |
| Wide-signature strict inventory | Pass: 32/32 reviewed |
| Authority-free aggregate strict inventory | Pass: 86/86 gated rows ruled |
| Extraction-scar inventory | Pass: 1/1 finding ruled |
| `tools\validate_format.bat` in clean validation worktree | Pass: 571 source files, 317 headers, zero unresolved `Related:` paths |
| `tools\validate_project_filters.bat` | Pass: 787/787 production items and 118/118 test items |
| `tools\validate_dependency_graph.bat` | Pass: 27 include rules, 46 negative edge fixtures, one content rule, zero findings |
| `tools\validate_fast.bat` in clean validation worktree | Pass: 430/430 cases, 2,419,076/2,419,076 assertions, Profile and Debug builds |
| Debug worker self-test production exit | Pass: `active_entries=0 session_high_water=0 capacity=256` |

SR3 still owns the performance comparison and broad/full proof; no
performance, Physics, Replay, or visual baseline is refreshed in SR2.

## Protected Owner Work

The uncommitted warm-start experiment in
`PersistentContactSolver.cpp`, `PersistentContactSolver.h`, and
`PhysicsNarrowphaseStage.cpp` remained outside SR2 editing, formatting, and
validation ownership. It still awaits owner evaluation, and no baseline was
reset.

## Questions

None. SR2 required no new product or ownership ruling.
