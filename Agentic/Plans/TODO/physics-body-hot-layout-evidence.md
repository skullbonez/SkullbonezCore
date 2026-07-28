# Physics Body Hot Layout Evidence

Date: 2026-07-28
Status: TODO — 1/4 phases complete
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

The owner first allowed an array of structures (AoS) only if representative
evidence showed no meaningful degradation relative to the current structure of
arrays (SoA). On 2026-07-28 the owner made the narrower campaign decision below:
leave the current SoA layout in place.

## Goal

Measure the retained hot-body SoA, make its comments match its real consumers,
and implement only evidence-backed improvements that preserve the SoA design.
The campaign may repair payload alignment, allocation topology, or bulk
consumers, but it may not replace the store with AoS.

## Owner Rulings

1. Retain the current SoA layout. This campaign has no authority to select or
   prototype an AoS replacement, so it needs no numeric AoS regression
   threshold.
2. Use the existing 200, 520, 1,000, 2,000, and sleepy-5,000 witnesses at the
   worker counts already exercised by Physics performance validation.

## Phases

- [x] **BL0 — Measure the current layout and real consumers.** Inventory every
  hot stream, payload alignment, allocation, scalar reconstruction, bulk
  consumer, and existing intrinsic. Capture final-source performance and cache/
  bandwidth evidence for the agreed scene matrix. Correct the header claim if
  it is already false; do not change storage yet. Evidence:
  `Agentic/Reports/2026-07-28/physics-body-hot-layout-bl0.md`.
- [ ] **BL1 — Rule the evidence-backed SoA work.** Record memory, allocation
  count, frame/Physics median and tail timing, and deterministic output for the
  retained layout. Select only SoA-internal work justified by BL0, such as real
  payload alignment, contiguous backing, or a measured bulk consumer. Do not
  prototype or select AoS.
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

The retained SoA has measured consumers, honest alignment/allocation comments,
no inert `alignas` workaround, no unexplained performance regression, and
byte-exact deterministic physics. No baseline is refreshed to hide divergence.

## Validation

Focused layout/performance harness while iterating; final
`tools\validate_physics.bat`, `tools\validate_perf.bat`, and
`tools\validate_full.bat`.
