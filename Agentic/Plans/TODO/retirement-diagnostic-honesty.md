# Retirement Diagnostic Honesty

Date: 2026-07-30
Status: NOT STARTED — 0/2 phases complete
Impact area: `SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp`,
`SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h`
Owner: Rendering/DX12 retirement quarantine
Priority: Low

## Problem And Evidence

Source-only review at tip `91a8403d` on 2026-07-30 found that the DX12
retirement quarantine's exhaustion diagnostic reports a value it does not have.

`Dx12DeferredReleaseOwner::Quarantine` fatals at
`Dx12DeferredReleaseOwner.cpp:37-42`:

```
if ( m_pendingCount >= MAX_PENDING_RETIREMENTS )
{
    SB_FATAL( "Dx12DeferredReleaseOwner",
              "Retirement capacity exhausted. owner=Rendering/DX12 capacity=%zu high_water=%zu",
              MAX_PENDING_RETIREMENTS, m_pendingCount );
}
```

`Dx12FrameOwner.h:84-96` declares only `MAX_PENDING_RETIREMENTS`, the
`m_pending` array, and `m_pendingCount`. There is no high-water member. Because
the branch condition is `m_pendingCount >= MAX_PENDING_RETIREMENTS`, the value
printed as `high_water` is always equal to the value printed as `capacity`, so
the field carries no information at the one moment it is read.

`AGENTS.md` Runtime Static Allocation Policy requires pool exhaustion to fail
"with owner, capacity, high-water, and phase diagnostics". This satisfies the
letter and not the purpose. The genuinely useful facts at exhaustion are how
close the queue normally runs, whether the last successful
`ReleaseCompleted` freed anything, and whether the fence was ready — none of
which the message carries.

`Dx12DeferredReleaseOwner::ReleaseCompleted` at
`Dx12DeferredReleaseOwner.cpp:87-154` is otherwise correct: it observes the
completed fence, compacts survivors with a read/write index pair, clears the
tail, and releases only on `empty || canReleaseUnfenced || (fenceAssigned &&
fenceReady && fenceValue <= completedFence)`. The fence proof is sound. This plan
changes diagnostics only.

`PhysicsFixedList` already implements the pattern this owner is missing:
`PhysicsFixedList.h:642-652` tracks a monotonic high-water and
`PhysicsFixedList.h:654-669` reports owner, requested, runtime capacity,
compile capacity, count, high-water, ceiling, and phase.

## Goal

Make the retirement exhaustion diagnostic report facts that let a reader
diagnose the exhaustion without a debugger, using the accounting
`PhysicsFixedList` already establishes.

## Non-Goals

- No change to the fence proof, release ordering, quarantine capacity, or any
  DX12 resource lifetime. This is a diagnostics change and must be visually and
  behaviorally identical.
- No new allocation, no growth path, and no raising of
  `MAX_PENDING_RETIREMENTS`. If DH0 finds the current capacity is genuinely
  tight, that is a separate owner decision and a separate plan.
- No generic diagnostics framework, telemetry sink, or shared high-water mixin.
  One retained counter and a corrected format string is the whole change.

## Phases

- [ ] **DH0 — Add real retirement accounting and correct the diagnostic.** Add a
  monotonic high-water member to `Dx12DeferredReleaseOwner` updated on every
  successful `Quarantine`, following the `PhysicsFixedList.h:642-652` pattern.
  Extend the exhaustion message to report capacity, current count, true
  high-water, the count released by the most recent `ReleaseCompleted`, whether
  the frame fence was ready, and the last observed completed fence value —
  the facts that distinguish "the queue is undersized" from "the fence never
  advanced" from "nothing ever called `ReleaseCompleted`". Confirm the counter
  resets with `ResetForDevice` and `ResetAfterShutdown` so a device reset does
  not carry a stale peak forward, matching how
  `Dx12FrameOwner::ResetForDevice` already clears `m_frameFenceValues`. Audit
  the remaining `SB_FATAL` sites in `SkullbonezSource/Rendering/DX12/` for the
  same fabricated-field pattern and correct any found.
- [ ] **DH1 — Prove and close.** Add focused coverage that drives the quarantine
  to exhaustion and asserts the reported high-water differs from capacity when
  the queue peaked below capacity before filling, so the field is proven to
  carry information rather than restating the bound. Complete the touched-file
  comment audit, and run the mapped DX12 gates including the mandatory bounded
  graphics-stress run. Evidence:
  `Agentic/Reports/2026-07-30/retirement-diagnostic-honesty-closure.md`.

## Dependencies And Decisions

- No barrier in or out. This plan touches only `Rendering/DX12` retirement
  diagnostics and is independent of the other three campaign plans. It is
  sequenced last because it is the smallest and lowest-risk.
- The high-water counter is retained state on a DX12 owner, not a runtime
  allocation, so it needs no `RuntimeReserveAllocator` registration or
  allocation-policy allowlist row.
- If DH0's audit of other DX12 `SB_FATAL` sites finds more than two additional
  fabricated fields, register a follow-up plan rather than growing this one —
  a two-task diagnostics plan should not become a subsystem sweep.

## Acceptance

The retirement exhaustion diagnostic reports a high-water value that can differ
from capacity, plus enough fence and release state to identify the cause without
attaching a debugger. No DX12 resource lifetime, fence proof, barrier, or
rendered pixel changes. `dx12_validation.txt` reports zero errors and the
committed DX12 baselines compare clean.

## Validation

`tools\validate_dx12_renderer.bat` with `dx12_validation.txt` confirmed at zero
errors and a clean baseline comparison, then `tools\run_graphics_stress.bat 1`
with its command, measured runtime, and successful exit recorded per the
mandatory DX12 stress rule. `tools\validate_tests.bat` for the DH1 coverage.
