# Physics Fixed List Copy Contract

Date: 2026-07-28
Status: TODO — 0/3 phases complete
Impact area: Physics storage lifetime, prediction cloning, allocation policy
Owner: Physics
Priority: High

## Problem And Evidence

`PhysicsFixedList` advertises ordinary copy construction and assignment, but
both call `Reserve()`. A non-empty copy outside SceneLoad or the existing
approved ReplayPrediction scope can therefore terminate the process. The
allocation policy is intentional; presenting it through a generally copyable
type is not.

## Goal

Make illegal copying unrepresentable and expose any genuinely required
scene-load or prediction clone as an explicit owner operation with a visible
phase precondition.

## Owner Ruling

Explicit cloning lives only on concrete aggregate owners (`PhysicsWorld`,
`PhysicsBodyStore`, `ColliderStore`, or a prediction snapshot owner).
`PhysicsFixedList` does not expose a public clone primitive that would recreate
the hidden phase contract for arbitrary callers.

## Phases

- [ ] **FC0 — Census copy and move semantics.** Find every actual and potential
  list/owner copy, identify its phase and allocator owner, and distinguish
  prediction isolation from accidental compiler-generated copying.
- [ ] **FC1 — Delete implicit copies and install explicit owner clones.** Delete
  list copy construction/assignment, keep or narrow moves based on the same
  phase audit, and add only the explicit owner operations required by FC0.
  Each clone must name and enforce SceneLoad or approved ReplayPrediction scope.
- [ ] **FC2 — Prove lifecycle and policy.** Add compile-time non-copyability
  checks plus focused legal-clone and illegal-phase coverage; run comment audit,
  allocation policy, Physics, Replay, performance, and broad validation.

## Acceptance

No normal C++ copy expression can reach a phase-fatal reserve. Required cloning
is named at the owning subsystem, phase-checked, allocation-accounted, and
covered without adding a generic context/service bag.

## Validation

`tools\validate_tests.bat`, allocation-policy self-test/repository scan,
`tools\validate_physics.bat`, `tools\validate_replay_allocation_policy.bat`,
`tools\validate_replay_visual_fidelity.bat`, `tools\validate_perf.bat`, and
`tools\validate_full.bat`.
