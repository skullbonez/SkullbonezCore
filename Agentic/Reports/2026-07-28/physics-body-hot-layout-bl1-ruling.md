# Physics Body Hot Layout BL1 Ruling

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Phase: BL1 — evidence-backed SoA work selection
Decision: Remove inert control-block alignment only

## Evidence Considered

BL0 establishes:

- 20 independently allocated hot payloads: 18 `float` streams and two
  `uint8_t` streams;
- 74 live payload bytes per body, or 14,800–370,000 live bytes across the
  approved scene matrix;
- useful 32-byte payload alignment already owned by
  `PhysicsFixedList::AllocateStorage`;
- scalar production consumers and no vector-intrinsic body-store consumer;
- a passing clean five-scene `validate_perf` run, with the full timing and
  logical traffic table in
  `Agentic/Reports/2026-07-28/physics-body-hot-layout-bl0.md`; and
- a clean `tools\validate_physics.bat` pass in 109.8 seconds at `23d32b65`,
  including the committed byte-exact baseline comparison.

## Selected BL2 Work

BL2 will make one source-layout correction:

1. Remove the 20 `alignas(32)` annotations from hot `PhysicsFixedList` member
   control blocks in `PhysicsBodyStore`.
2. Delete the matching MSVC C4324 warning push, suppression, explanatory
   comment, and pop.
3. Preserve the 32-byte payload guarantee in `PhysicsFixedList`; do not weaken
   or relocate it.

This removes a false control-block alignment signal and its padding without
changing hot payload allocation, field order, live-prefix ownership, or access
semantics.

## Rejected BL2 Work

### Contiguous backing

Do not collapse the 20 allocations. They occur in the allowed scene-load
reserve phase, and the allocation guard passes. BL0 contains no measurement
showing that scene-load allocation count or separated backing causes the
observed frame cost. A shared block would introduce offset, construction,
destruction, and ownership changes without evidence of a runtime benefit.

### New vectorized consumer

Do not add one. Consecutive full-body loops coexist with scattered contact,
query, sleep, and diagnostic access. BL0 identifies no stage whose measured
cost and access shape justify a vector implementation, and no current body
store intrinsic establishes an existing contract to preserve.

### AoS prototype

Do not add one. The owner selected SoA and explicitly withheld authority for an
AoS prototype or replacement.

## BL2 Proof

Because the selected edit changes only the containing class layout and leaves
payload allocation/access code unchanged, BL2 must prove:

- zero remaining `alignas(32)` or C4324 suppression in `PhysicsBodyStore.h`;
- Profile and Debug compilation with warnings as errors;
- byte-exact `validate_physics`;
- the same five-scene `validate_perf` witness, investigated rather than
  baseline-refreshed if timing shifts materially; and
- touched-file comment audit with no claim that control-block alignment owns
  payload alignment.

The sleepy-5,000 BL0 P99 is recorded as a tail observation, not attributed to
layout. BL2 must not claim to improve that tail without a repeatable causal
witness.
