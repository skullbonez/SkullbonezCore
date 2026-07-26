# Allocator Foreign Pointer Safety

Date: 2026-07-26
Status: NOT STARTED — drafted from the 2026-07-26 from-source architecture
review of `nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 11 of the Architecture Follow-Up Campaign
Round 5. 0/3 phases complete.
Impact area: `Core/Allocation/RuntimeAllocationTracker.cpp`
Owner: core allocation
Priority: High severity, low frequency — this is the sharpest memory-safety edge
in the tree. It is reached only by a pointer the hook did not produce, but the
failure mode is a fault or a heap corruption rather than a diagnostic.

## Problem And Evidence (measured 2026-07-26)

`FreeTrackedMemory` (`Core/Allocation/RuntimeAllocationTracker.cpp:395`) is
installed behind the global `operator delete`. Its first action on a non-null
pointer is:

```cpp
auto* header = reinterpret_cast<AllocationHeader*>(
    reinterpret_cast<unsigned char*>( pointer ) - sizeof( AllocationHeader ) );

if ( header->magic != ALLOCATION_HEADER_MAGIC )
{
    // Hazard: shutdown or third-party code can call these global delete
    // overloads for storage not produced by our hook. ...
    std::free( pointer );
    return;
}
```

Two distinct hazards, both acknowledged by the surrounding comment as a known
trade-off, neither currently bounded:

1. **Out-of-bounds read before the allocation.** For a pointer the hook did not
   produce, `pointer - sizeof(AllocationHeader)` is memory outside the
   allocation. Reading `magic` there is undefined behavior. In practice CRT heap
   blocks carry their own headers so the bytes are mapped, but a large
   `VirtualAlloc`-backed or page-aligned allocation from another allocator can
   begin at a page boundary with the preceding page unmapped — an access
   violation inside `operator delete`, at shutdown, in a build where the
   allocation guard may already be off.

2. **`std::free` on memory that may not be `malloc`'s.** The fallback hands the
   pointer to the CRT. That is correct only if the foreign allocator ultimately
   used `malloc`. A `VirtualAlloc`, `HeapAlloc` on a private heap, or a
   separately-linked CRT produces a pointer `std::free` must not receive. The
   result is heap corruption, not a diagnostic.

The surrounding design is otherwise careful and clearly written by someone who
has met this problem: the header carries `raw` so the exact allocation base is
returned to `std::free` on the happy path (`:429-431`), `magic` is cleared before
freeing so a double-delete falls into the foreign path rather than
double-subtracting counters (`:427`), and `s_insideAllocationHook` prevents
recursive accounting. The gap is only the unvalidated read and the unqualified
fallback.

Related, same file: `AllocateTrackedMemory:338` computes
`totalSize = size + alignment - 1 + sizeof(AllocationHeader)` and derives the
user address by rounding up from `raw + sizeof(AllocationHeader)`. That
arithmetic is correct but unchecked for overflow on a hostile or absurd `size`,
and `FatalAllocationFailure` (`:433`) exists for exhaustion but not for an
overflowed request.

## Goal

`operator delete` never reads memory it does not own, and never hands a pointer
to an allocator that did not produce it. When the hook cannot prove ownership,
the outcome is a bounded, diagnosable decision rather than undefined behavior.

## Non-Goals

- No change to the tracking model, phase attribution, callsite capture, Tracy
  pairing, owner scoping, or the reserve-allocator integration. The design is
  sound; this plan hardens one path.
- No change to allocation policy, the allowlist, or any gate threshold.
- No removal of the global `operator new`/`delete` override. It is load-bearing
  for the allocation guard and for `store-capacity-memory-reporting`.
- No performance regression on the happy path. Validation is only for pointers
  that fail the ownership test, and the ownership test must not become more
  expensive for owned pointers.
- No new heap traffic inside the hook. `RuntimeAllocationTracker.cpp:25` already
  requires the hooks not to allocate.

## Phases

- [ ] **AF0 — Establish provable ownership before reading the header.**
  Make the ownership test safe for a pointer the hook did not produce. Options to
  evaluate and rule with evidence, not preference: a page-residency probe before
  the read (`VirtualQuery` on Windows, which is not an allocation);
  a registry or address-range record of hook-produced allocations; or a
  structured-exception-guarded read isolated to this one site. Whichever is ruled,
  the read must not be reachable on an unmapped page. Record why the rejected
  options were rejected. Acceptance: a focused test passes a page-aligned
  foreign pointer whose preceding page is unmapped and the hook does not fault;
  the owned-pointer path executes the same instructions as before, verified by
  the perf gate showing no regression.

- [ ] **AF1 — Bound the foreign-pointer fallback.**
  Replace the unconditional `std::free( pointer )` with a decision the hook can
  defend. At minimum: count foreign frees per process, report them with the
  pointer value and the current phase, and make the count visible in the
  allocation diagnostics so a real occurrence is discovered rather than absorbed.
  Rule whether the fallback stays `std::free` (documented as "the foreign
  allocator is assumed to be this CRT", with the assumption stated as an
  `Invariant:`/`Hazard:` comment and the counter as its tripwire) or becomes a
  lane-F fatal. In the same phase, add the overflow check to
  `AllocateTrackedMemory`'s `totalSize` computation and route an overflowed
  request through `FatalAllocationFailure`. Acceptance: foreign frees are counted
  and reported; the chosen fallback is documented with owner, reason, and hazard;
  an overflowing size request fails loud rather than wrapping; existing
  allocation-policy self-tests and repo scan pass.

- [ ] **AF2 — Reconcile, review, and hand off.**
  Confirm the counters read zero across a full validation run — a non-zero foreign
  free count in normal operation is itself a finding and must be investigated
  before closure, not recorded as expected. Complete the comment audit for the
  touched file. Obtain one independent review asking: can the header read still be
  reached on memory the hook does not own, can a non-`malloc` pointer still reach
  `std::free`, and does the happy path pay anything new. Acceptance: review clear;
  `validate_perf.bat` shows no allocation-path regression;
  `python tools\check_allocation_policy.py --self-test` and `--repo .` pass;
  `validate_full.bat` and `run_graphics_stress.bat 1` complete with zero foreign
  frees reported.

## Dependencies And Decisions

- No hard dependency on the other campaign plans, and no plan depends on this
  one. It can run at any point in the campaign.
- `store-capacity-memory-reporting` MR2 surfaces allocation diagnostics in the UI
  memory tab. If that plan closes first, AF1's foreign-free counter joins the same
  readout rather than inventing a second surface.
- Open decision for the owner, recorded not assumed: whether an unprovable
  foreign free should be lane-F fatal. AF1 produces the evidence — how many occur
  in practice across the full gate and at shutdown — and recommends. A fatal is
  the safer engineering choice but would turn a currently-silent third-party
  interaction into a crash, so the owner rules.
- The ImGui and Tracy development-tool allocation owners
  (`Core/Allocation/DevelopmentToolAllocation.h`) are the most likely real source
  of foreign frees. AF0 must confirm whether their allocations carry hook headers
  before assuming the foreign path is unreachable in development builds.

## Acceptance

- No read of memory the hook does not own.
- No pointer reaches an allocator that did not produce it without a documented,
  counted, owner-ruled decision.
- Allocation size arithmetic cannot overflow silently.
- Zero foreign frees observed across the full validation run, or the occurrences
  explained.
- No happy-path cost added.

## Validation

Per the File To Validation Mapping, `Core/Allocation/*` requires the perf gate,
and allocation-policy tooling requires its self-test plus repo scan:

- `tools\validate_perf.bat`
- `python tools\check_allocation_policy.py --self-test`
- `python tools\check_allocation_policy.py --repo .`
- `tools\validate_tests.bat` — new focused foreign-pointer and overflow coverage
- `tools\validate_full.bat` and `tools\run_graphics_stress.bat 1` — exercise the
  hook under real allocation traffic including shutdown
