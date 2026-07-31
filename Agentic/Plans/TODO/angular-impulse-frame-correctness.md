# Angular Impulse Frame Correctness

Date: 2026-07-31
Status: IN PROGRESS — 3/5 phases complete
Impact area: Physics angular impulse response, mutual-gravity reduction, physics baselines
Owner: Physics
Priority: High
Owner gate: AI4 requires explicit owner sign-off before closure; AI0 predicts no baseline regeneration

## Problem And Evidence

Three code paths convert a torque impulse into an angular-velocity delta. Two
handle body-frame inertia correctly. One does not.

**Correct — `ApplyWorldImpulse`, `Physics/PhysicsBodyStore.cpp:801`:**

```cpp
const RotationMatrix orientation = hot.orientation.GetOrientationMatrix();
if ( orientation.TransposeMultiply( worldTorqueImpulse ).TryDivided( record.rotationalInertia, localAngularImpulse ) )
    hot.angularVelocity += orientation * localAngularImpulse;
```

That is `ω += R · I_body⁻¹ · Rᵀ · τ`.

**Correct — the contact solver**, which honours `usesWorldInertia` and rotates
inertia through orientation for non-spheres
(`Physics/PersistentContactSolver.cpp:317-328`).

**Incorrect — `ApplyPendingImpulse`, `Physics/PhysicsBodyStore.cpp:827`:**

```cpp
const Vector3 torque = CrossProduct( record.pendingImpulseApplicationPoint, record.pendingImpulse );
if ( torque.TryDivided( record.rotationalInertia, angularImpulseDelta ) )
    hot.angularVelocity += angularImpulseDelta;
```

A world-space torque is divided by the **body-frame diagonal** inertia and added
straight into world angular velocity. There is no orientation rotation and no
`usesWorldInertia` check. For any rotated body with anisotropic inertia — every
box and every hull — the resulting spin axis and magnitude are wrong. Spheres
are unaffected because `R I⁻¹ Rᵀ = I⁻¹` for isotropic inertia, which is the
likely reason this has not surfaced.

There is a second, related question on the same line.
`pendingImpulseApplicationPoint` is documented as a **local** application point
(`Physics/PhysicsEngine.h:199`, `:204`), while callers supply what reads as a
world impulse. `CrossProduct( localPoint, worldImpulse )` is a frame-mixed
torque that is meaningful in neither frame at non-identity orientation.

The live path is the launcher tool: `Runtime/Tools/RuntimeTools.cpp:285` →
`PhysicsEngine::ApplyBodyImpulse` (`Physics/PhysicsEngine.cpp:1103`) →
`SetPendingBodyImpulse` → `ApplyPendingImpulse`, applied at an off-centre point
to an arbitrary already-rotated target. Authored scene impulses
(`Runtime/Scene/SceneGeneratedSetup.cpp:277`, `:301`, `:395`, `:446`,
`Runtime/Scene/SceneAuthoredSetup.cpp:640`) reach the same path but are applied
at creation, where orientation is typically identity and the defect is masked.

**Why the gates did not catch this.** The behavior is deterministic, so
byte-exact CSV baselines reproduce it perfectly and lock it in. Determinism
proves consistency; it cannot distinguish "consistently correct" from
"consistently wrong." That distinction is the reason this plan carries an owner
gate rather than a bounded-divergence allowance.

**Separate finding, same plan — the mutual-gravity reduce.**
`Physics/Stages/PhysicsForceStage.cpp:268-390` walks the full triangular pair
space twice. Pass one runs in parallel and evaluates `mass > TOLERANCE`,
`fixed`, `inverseMass`, and `sleepState` per pair before writing the force. Pass
two runs serially on the main thread and re-evaluates every one of those
predicates to decide whether to read the slot back. At the 512-body cap that is
roughly 131,000 re-derived pair predicates per step, serial. The preceding
`m_mutualGravityPairForces.assign( requiredPairCapacity, ZERO_VECTOR )` clears
about 1.5 MB per step.

The serial reduction itself is correct and must stay: the comment at
`PhysicsForceStage.cpp:344` explains that chunked partial-body reduction would
regroup float additions and change result bits. Determinism forces the serial
*order*. It does not force re-deriving the *filter*.

## Goal

Make the pending-impulse angular response agree with the world-inertia handling
that `ApplyWorldImpulse` and the contact solver already use, and remove the
redundant triangular pass from mutual gravity without moving a single physics
byte.

## Non-Goals

- No gyroscopic term. The absence of `ω × (I ω)` is a deliberate fidelity limit
  shared with most game solvers and is out of scope.
- No SoA layout change. The 20-stream hot-field store is a known, owner-accepted
  observation and is explicitly not addressed here.
- No change to the serial reduction *order* in mutual gravity. Only the
  derivation of which pairs participate may change.
- AI3 does not fix what it finds. It reports and registers follow-up plans, so
  the AI4 baseline delta remains attributable to exactly one behavioral change.

## Owner Ruling

This plan carries **no bounded-divergence allowance**. AI1 is strictly
byte-exact and a differing byte reverts the task. AI2 is a deliberate behavioral
correction, but AI0 proved that no committed baseline or fixture reaches the
defect with both non-identity orientation and anisotropic inertia. AI2 must
therefore leave every committed artifact byte-exact. Any artifact movement is
unexplained divergence that reopens AI0 and AI2; it is never accepted or
refreshed at AI4.

The task order is deliberate. AI1 lands first and must prove it moved nothing.
AI2 then turns the focused expected-failure test green without moving committed
artifacts. The owner therefore sees an exact refactor, a focused correction
proof, and a complete zero-delta artifact comparison.

## Phases

- [x] **AI0 — Census the angular impulse paths, predict the delta, and resolve
  the frame question.** Document all three torque→angular-velocity conversions
  and their exact arithmetic. Resolve whether `pendingImpulseApplicationPoint`
  is local or world by inspecting every caller, and state the intended contract;
  correct either the callers or the documentation, not both directions at once.
  Add a **failing** focused test: a rotated non-sphere body must receive the
  same angular response from an off-centre gameplay impulse as from the
  equivalent contact impulse. Enumerate which baseline scenes and interaction
  fixtures reach `ApplyPendingImpulse` with a non-identity orientation and/or
  anisotropic inertia, and **predict the expected baseline delta before any
  production code changes** — that prediction is the oracle AI4 checks against.
  Evidence:
  `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai0-census.md`.
  Completed with all three arithmetic paths and every direct caller recorded.
  The application-point contract is a world-space offset from the body's center:
  launcher ray hits already construct that value, generated/authored values are
  lever offsets at spawn, and zero-point callers are frame-neutral. One authored
  ragdoll demo contains an absolute-position-looking outlier, recorded for
  separate authoring repair rather than used to reverse the API contract. A
  `doctest::should_fail` characterization exercises the production pending and
  contact paths and records the current `(-2.35, 0.96, 0.55)` versus
  `(-1.13982, 0.808092, 0.55)` mismatch. The pre-change artifact oracle is zero
  committed baseline bytes: mapped authored impulses are spheres, generated
  anisotropic boxes start at identity, and the launcher fixture targets a
  sphere.

- [x] **AI1 — Byte-exact mutual-gravity reduce.** Emit a compacted canonical
  pair list from the parallel build pass and make the serial reduce a linear
  walk over it, preserving the exact triangular accumulation order and the exact
  float operation sequence. Address the per-step full-capacity `assign` clear;
  a compacted list should make clearing the whole triangular table unnecessary.
  Acceptance is byte-identical physics CSV against the committed baselines and
  passing worker-count invariance tests
  (`SkullbonezTests/TestDeterminism.cpp:1534`, `:1540`). **A byte difference
  here means the refactor changed evaluation and the task is reverted, not
  baselined.** Record before/after Profile timings for the reduce. Evidence:
  `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai1-gravity-reduce.md`.
  Completed with per-chunk dense pair prefixes, ascending overlap-safe
  compaction, and one linear active-pair reduction. Packed body/receiver values
  avoid rereading cold and hot state during reduction, while each body's float
  additions retain their original order. The 40-body worker fixture now creates
  sparse gaps across chunk boundaries and remains exact at zero, one, and four
  workers; the 520-body fallback remains unchanged. Across 660 Profile samples
  from the same 200-body scene, mean Reduce time improved from 0.093166 ms to
  0.079943 ms and median from 0.0924 ms to 0.0780 ms.

- [x] **AI2 — Correct the pending-impulse angular path.** Route
  `ApplyPendingImpulse` through the same world-inertia conversion used by
  `ApplyWorldImpulse` and the contact solver, honouring `usesWorldInertia`.
  Apply AI0's frame ruling. Prefer one shared conversion helper over a third
  copy of the arithmetic, so a future fourth path cannot diverge again. The AI0
  test goes green. Confirm the sphere path is bit-identical, since isotropic
  inertia must be unaffected by the change. Evidence:
  `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai2-impulse.md`.
  Completed with one caller-supplied diagonal-operation helper shared by the
  gameplay, world-force, and contact-solver paths. The rotated anisotropic-box
  characterization is now an ordinary passing cross-path test, while a rotated
  sphere pins the exact pre-change component values. Public and stored impulse
  vocabulary now names the application value as a world-space center-relative
  offset, matching the AI0 ruling. Unit, core Physics, and
  deep Physics gates pass without refreshing any baseline. Owner direction on
  2026-08-01 removed the proposed additional `at_rest` frame assertion because
  the deep gate already hashes the complete 54,001-line CSV byte-for-byte;
  its generated SHA-256 remains
  `0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984`.

- [ ] **AI3 — Sweep for other conventions that baselines would lock in.**
  Investigation and reporting only; register follow-up plans rather than
  changing behavior. Look for the same shape elsewhere: frame mixing between
  local and world quantities, a convention documented in one direction and
  implemented in the other, and arithmetic duplicated across paths where only
  some copies were later corrected. Concrete starting points are the inertia and
  torque conversions, `VectorReflect`'s incident-direction convention
  (`Maths/Vector3.h:325`), the local-versus-world contract on every
  `PhysicsApi.h` descriptor field, and any remaining site that divides by
  `rotationalInertia` directly. State for each whether a byte-exact baseline
  would hide it. Evidence:
  `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-ai3-convention-sweep.md`.

- [ ] **AI4 — Owner verification and zero-delta acceptance.** Present the
  complete artifact comparison against AI0's prediction and prove every
  committed physics, query, performance, interaction, screenshot, and replay
  visual artifact remained byte-exact. Confirm AI1 and AI2 contributed zero
  artifact bytes while the focused test changed from expected failure to an
  ordinary pass. Prove no body loss, non-finite state, energy explosion,
  invariant failure, allocation violation, false negative in the census, or
  unrelated scene/config/schema/render change. Any artifact movement blocks
  closure and reopens AI0/AI2; this plan authorizes no baseline regeneration.
  **Stop here for explicit owner sign-off.** After acceptance, rerun the matching
  gates against the unchanged committed baselines and close without refreshing
  them. Evidence:
  `Agentic/Reports/2026-07-31/angular-impulse-frame-correctness-closure.md`.

## Acceptance

One shared conversion produces the angular response for gameplay, world-force,
and contact impulses, and a rotated box responds identically to equivalent
impulses from any of those sources. Mutual gravity derives its participating
pairs once and its reduce is measurably cheaper with zero artifact movement.
The focused rotated-box proof passes, every committed artifact remains
byte-exact, and the owner explicitly accepts the correction without a baseline
refresh.

## Validation

`tools\validate_tests.bat`, `tools\validate_physics.bat`,
`tools\validate_physics_deep.bat`, `tools\validate_perf.bat`,
`tools\validate_replay_visual_fidelity.bat`, and `tools\validate_full.bat`.
Compare final Debug artifacts against the committed baselines. Any difference
blocks closure and reopens AI0/AI2; this plan authorizes no regeneration.
