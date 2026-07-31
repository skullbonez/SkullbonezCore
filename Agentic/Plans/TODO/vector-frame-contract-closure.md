# Vector Frame Contract Closure

Date: 2026-08-01
Status: IN PROGRESS — 3/5 phases complete
Impact area: Physics force integration, public Physics descriptors, Scene authoring, Maths vector semantics
Owner: Physics, Scene, Maths
Priority: High

## Problem And Evidence

Angular Impulse Frame Correctness AI3 found three standing convention defects
and one public-contract gap that deterministic baselines cannot adjudicate:

- the general angular-drag clamp compares world-space angular velocity and
  torque components with body-axis diagonal inertia on rotated anisotropic
  bodies;
- `ragdoll_playground.scene.json` supplies the `wake_ball` impulse offset as its
  absolute world position, the only such equality among 56 `forcePosition`
  fields across 23 scenes;
- `VectorReflect` has no production caller and its test pins reflection about
  the normal axis, while the function's incident/surface-normal vocabulary
  conventionally describes reflection across the surface plane; and
- `PhysicsApi.h` leaves the coordinate frame of its public vector and
  orientation fields implicit even though current consumers consistently use
  body-local shape/anchor/inertia values and world-space pose, velocity, query,
  and hit values.

Evidence:
`Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai3-convention-sweep.md`.

## Goal

Make every affected vector contract explicit, prove equivalent operations with
independent oracles, correct the three identified convention defects, and keep
all unaffected committed artifacts exact.

## Non-Goals

- Do not add a regex or frozen count as a substitute for frame reasoning.
- Do not introduce a broad coordinate-system wrapper or context bag.
- Do not regenerate a baseline merely because it reproduces current behavior.
- Do not change the world-up/Y-up engine convention.
- Do not revive the cancelled extra `at_rest` frame assertion; its complete CSV
  is already byte-regressed by the deep Physics gate.

## Phases

- [x] **VF0 — Pin the public frame matrix and cross-path oracles.** Document the
  frame of every vector/quaternion/shape field in `PhysicsApi.h`: body-local
  shape offsets, body-to-world orientation, world pose and velocities,
  body-principal diagonal inertia, local joint anchors, world ray/AABB inputs,
  and world hit outputs. Add focused tests where a caller could otherwise pass
  a plausible value in the wrong frame. Re-census the correct duplicate
  `Ragdoll.cpp::ApplyRecordInvInertia`; either route it through the shared
  inertia helper with exact arithmetic or record why its local owner retains
  the spelling.
  Evidence:
  `Agentic/Reports/2026-07-31/vector-frame-contract-closure-vf0-frame-matrix.md`.

- [x] **VF1 — Correct anisotropic angular-drag clamping.** Establish a rotated
  anisotropic-body oracle that independently derives the no-reversal clamp in
  body axes and returns the response to world space. Correct
  `ClampAngularDragTorqueAxis` integration without double-transforming the
  torque passed to `ApplyWorldImpulse`. Prove isotropic spheres remain exact.
  Before accepting any artifact movement, map whether a committed scene reaches
  the active clamp; unexplained movement blocks the phase and no regeneration
  is authorized.
  Evidence:
  `Agentic/Reports/2026-07-31/vector-frame-contract-closure-vf1-angular-drag.md`.

- [x] **VF2 — Repair the authored impulse-offset schema.** Replace the ambiguous
  `forcePosition`/`forcePos*` vocabulary with an explicit world-space
  center-relative impulse offset across parser values, authored setup, scene
  files, and relevant tests. Correct `ragdoll_playground`'s absolute-position
  outlier using a focused scene-loading/impulse oracle. Do not retain a silent
  compatibility alias that lets new files keep authoring the ambiguous field.
  Evidence:
  `Agentic/Reports/2026-07-31/vector-frame-contract-closure-vf2-authored-impulse-offset.md`.

- [ ] **VF3 — Adjudicate `VectorReflect`.** With its zero-production-caller
  census in hand, choose one honest surface: delete the test-only helper, rename
  it as normal-axis mirroring, or implement conventional incident reflection
  across the surface plane. The selected name, formula, and unit cases must
  agree for oblique and normal incidence; do not preserve ambiguity solely to
  keep the current unit expectation green.

- [ ] **VF4 — Validate and close.** Run focused tests plus `validate_tests`,
  `validate_fast`, `validate_physics`, `validate_physics_deep`,
  `validate_perf`, `validate_replay_visual_fidelity`, and `validate_full` as
  applicable to the landed source. Compare all mapped artifacts completely,
  audit every touched source comment, run the six ownership inventories, and
  obtain independent read-only closure review. Any behavior change outside the
  focused, causally explained corrections reopens its owning phase.

## Dependencies And Decisions

- Angular Impulse Frame Correctness AI4 closes first so its zero-delta oracle is
  accepted independently of this follow-up work.
- VF0 precedes VF1-VF3; the contract and independent oracle must exist before a
  deterministic implementation is changed.
- This plan carries no bounded-divergence or baseline-refresh authority. If a
  committed artifact reaches VF1 or VF2, stop with the complete delta and seek
  an explicit owner ruling.
- The existing `at_rest` whole-file regression remains authoritative; no extra
  frame assertion belongs to this plan.

## Acceptance

Public frame vocabulary is explicit and tested, angular-drag clamping is
correct for rotated anisotropic inertia without changing isotropic response,
authored impulse offsets cannot be confused with absolute positions, and the
Maths reflection surface has one unambiguous convention. All unaffected
artifacts remain exact and any accepted focused transition has complete causal
evidence.

## Validation

- `tools\validate_tests.bat`
- `tools\validate_fast.bat`
- `tools\validate_physics.bat`
- `tools\validate_physics_deep.bat`
- `tools\validate_perf.bat`
- `tools\validate_replay_visual_fidelity.bat`
- `tools\validate_full.bat`
- Six ownership inventories and touched-source comment audit
- Independent read-only rubber-duck review
