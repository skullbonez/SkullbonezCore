# Physics Body Hot Layout Evidence

Date: 2026-07-28
Status: TODO — 0/4 phases complete
Impact area: Physics hot storage, cache locality, allocation policy, performance
Owner: Physics
Priority: High — first principal-engineer finding and the binding first plan

## Problem And Evidence

`SkullbonezSource/Physics/PhysicsBodyStore.h` stores the hot body state in 18
separate `PhysicsFixedList<float>` streams plus fixed/awake flags. The current
header claims adjacent bodies can be loaded without gathering, but the
production body-stage kernels do not use vector intrinsics over these streams.
Scalar accessors reconstruct `Vector3`/`Quaternion` values by index, the member
`alignas(32)` aligns each list control block rather than its allocated payload,
and every list owns a separate allocation.

The owner added this binding direction on 2026-07-28: returning to an array of
structures (AoS) is allowed only if representative evidence shows that it does
not meaningfully degrade performance relative to the current structure of
arrays (SoA).

## Goal

Select and implement a measured hot-body layout whose comments match its real
consumers. Either the SoA has vectorized stage kernels that pay for its
complexity, or an AoS/hybrid layout proves no meaningful performance loss while
reducing allocation and scalar gather costs.

## Owner Questions Before BL1

1. What numeric threshold defines “no meaningful performance degradation” for
   an AoS candidate (for example, no repeatable regression beyond noise, or a
   specific median/P95 percentage)?
2. Which scene/body-count and worker-count witnesses are release-blocking for
   that decision? Proposed default: the existing 200, 520, 1,000, 2,000, and
   sleepy-5,000 witnesses at the worker counts already used by Physics
   performance validation.

BL0 may run without these answers. BL1 must record the answers before choosing
AoS.

## Phases

- [ ] **BL0 — Measure the current layout and real consumers.** Inventory every
  hot stream, payload alignment, allocation, scalar reconstruction, bulk
  consumer, and existing intrinsic. Capture final-source performance and cache/
  bandwidth evidence for the agreed scene matrix. Correct the header claim if
  it is already false; do not change storage yet.
- [ ] **BL1 — Compare viable layouts and make the owner decision.** Implement
  bounded experimental branches or harness variants for current SoA, a compact
  AoS, and any justified hybrid. Record memory, allocation count, frame/Physics
  median and tail timing, and deterministic output. AoS is selectable only
  under the owner threshold above; otherwise select vectorized SoA/hybrid work.
- [ ] **BL2 — Implement the selected layout.** Remove inert control-block
  alignment and warning suppression, collapse or retain allocations according
  to the decision, and make every layout claim name an actual consuming stage.
  Do not add a per-body field without the `PhysicsBodyRecord` owner ruling in
  `AGENTS.md`.
- [ ] **BL3 — Close determinism, performance, comments, and review.** Prove
  byte-exact physics, allocation-policy compliance, the agreed performance
  threshold, and touched-file comment quality. Run the three ownership
  inventories and an independent no-bag/hot-path review.

## Acceptance

The selected layout has measured consumers, honest alignment/allocation
comments, no inert `alignas` workaround, no unexplained performance regression,
and byte-exact deterministic physics. No baseline is refreshed to hide layout
divergence.

## Validation

Focused layout/performance harness while iterating; final
`tools\validate_physics.bat`, `tools\validate_perf.bat`, and
`tools\validate_full.bat`.
