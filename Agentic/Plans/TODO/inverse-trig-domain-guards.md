# Inverse Trig Domain Guards

Date: 2026-07-30
Status: NOT STARTED — 0/4 phases complete
Impact area: `SkullbonezSource/Runtime/Camera/Camera.cpp`,
`SkullbonezSource/Maths/Matrix4.cpp`,
`SkullbonezSource/Maths/OrbitalMechanics.cpp`,
`SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp`,
`SkullbonezSource/Physics/Ragdoll.cpp`,
`SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp`, the shared `Maths`
domain-clamp spelling, camera pitch limiting, orientation construction
Owner: Camera + Maths orientation
Priority: Medium

## Problem And Evidence

Source-only review at tip `91a8403d` on 2026-07-30 enumerated every inverse-trig
call site in `SkullbonezSource/`. Excluding one comment mention at
`Matrix4.cpp:350`, there are eight code sites — six `acosf`, one `asinf`, one
`std::acos`. Each is classified by whether its argument is proven inside
`[-1, 1]`:

| Site | Argument | Guard status |
|---|---|---|
| `OrbitalMechanics.cpp:302` | `angularMomentum.z / magnitude` | **Guarded** — `ClampUnit( ... )`, a file-local helper at `OrbitalMechanics.cpp:71` |
| `Ragdoll.cpp:247` | `dot` | **Guarded** — inline `std::clamp( Dot( headUp, torsoUp ), -1.0f, 1.0f )` at `Ragdoll.cpp:231` |
| `AttachedCameraController.cpp:855` | `normalizedY` | **Guarded** — inline `std::clamp( offset.y / pitchDistance, -1.0f, 1.0f )` at `:853` |
| `Camera.cpp:463` | `Dot( vNegatedView, m_upVector )` | **Unguarded** |
| `Camera.cpp:465` | `Dot( vNegatedView, -m_upVector )` | **Unguarded** |
| `Matrix4.cpp:406` | `cosA` | **Upper-guarded only** — `if ( cosA < 0.9999f )` at `:399` bounds the `+1` pole and leaves the `-1` pole open |
| `EditorPlacementAssets.cpp:1774` | `dot` | **Incidentally protected** — the `axisMag <= TOLERANCE` early return at `:1768` rejects the parallel case, but no domain clamp exists |
| `GeometricMath.cpp:176` | `trianglePlane.m_normal.y` | **Unguarded** — deleted by `maths-surface-reachability` MR1 |

The census shows one domain policy expressed three ways — a file-local
`ClampUnit` used once, an inline `std::clamp` used twice, and nothing at all
three times. A reader cannot predict which form a given site uses, which is the
same decorative-convention failure `AGENTS.md` names under Capability Slice
Ownership: a convention that is not the only convention on its path is not a
convention.

The `Camera.cpp` pair is the material defect.
`Camera::UpVectorViewVectorRotationCap` takes the dot of two unit vectors; float
rounding that pushes the magnitude past 1.0 makes `acosf` return NaN. Both
subsequent comparisons at `Camera.cpp:469` and `:478` are then false, because a
NaN comparison is always false, and the function returns `requestRadians`
unmodified at `Camera.cpp:484`. The pitch cap disables itself at exactly the
singularity it exists to guard — the comment at `Camera.cpp:462` states
"Compare against both poles so pitch caps cannot flip through the up axis."

`Camera.cpp:61-63` additionally sets `m_upVector` to `ZERO_VECTOR` when
normalization fails. `Dot( v, ZERO_VECTOR )` is 0, so both angles become
`π/2` and the caps compare against meaningless values. `Camera::GetRightVector`
at `Camera.cpp:488-501`, twenty-five lines below the defect in the same file, has
an explicit fallback for that same degenerate state. The knowledge is present in
the file and did not reach this function.

`Camera.cpp` is part of the older naming stratum — 24 first-party files retain
`vNegatedView`/`fQuantity`-style Hungarian notation. Those files received
learning headers during the 2026-07-10 boilerplate pass because that gate is
mechanical, and did not receive semantic hardening because that gate is review.
This plan does not rename them; it closes the domain gaps.

## Goal

Every inverse-trig call site in first-party source either has a proven-in-domain
argument or one explicit, consistently spelled clamp, and every degenerate-axis
fallback in `Camera` is consistent with the one `GetRightVector` already
implements.

## Non-Goals

- No renaming of the Hungarian-notation stratum. Renaming determinism-sensitive
  maths and camera code is churn this plan does not need and does not authorize.
- No camera behavior change in the non-degenerate range. A clamp that never
  fires must produce identical output.
- No trig *wrapper* — no `SafeAcos`, no math facade, no type that hides which
  transcendental is being called. Promoting the existing `ClampUnit` to a shared
  `Maths` helper so one spelling covers all guarded sites is in scope and is the
  expected TD1 outcome; wrapping `acosf` itself is not.
- No baseline, golden, or physics artifact refresh.

## Phases

- [ ] **TD0 — Census and classify every domain-sensitive site.** Confirm the
  table above against the post-`maths-surface-reachability` tree and extend it to
  the 96 `sqrtf` and three `atan2f` sites, classifying each as proven-non-negative
  by construction, guarded, or open. For each open site, state the concrete input
  state that reaches the bad domain and whether that state is achievable at
  runtime — an unreachable domain gap is documented, not clamped. Determine
  whether `Matrix4.cpp:406` is on a Debug-only reference path or a shipping path;
  `Matrix4.cpp:387-395` describes a release path that eliminates the `acosf`
  round-trip, and the answer changes whether TD1's fix is physics-visible.
  Evidence: `Agentic/Reports/2026-07-30/inverse-trig-domain-guards-td0-census.md`.
- [ ] **TD1 — Unify the domain-clamp spelling and close the Camera gap.** Promote
  `ClampUnit` from its file-local definition at `OrbitalMechanics.cpp:71` into
  the shared `Maths` surface so one named spelling expresses the `[-1, 1]`
  domain policy, and convert the two existing inline `std::clamp` sites and the
  `OrbitalMechanics` call to it. Apply it to both dot products in
  `Camera::UpVectorViewVectorRotationCap` before `acosf`. This is a pure
  respelling at the three already-guarded sites and must be byte-exact there.
  Give the zero-`m_upVector`
  degenerate state the same explicit treatment `GetRightVector` uses, so the two
  functions agree on what a degenerate camera basis means. Add a `Hazard:` or
  `Invariant:` comment naming the NaN-comparison failure mode, because the
  fail-open behavior is not visible from reading the guard. Correct the
  `Camera.cpp:462` comment if the post-change behavior differs from what it
  claims.
- [ ] **TD2 — Close the remaining open sites and pin them.** Apply TD0's ruling to
  `Matrix4.cpp:406` and `EditorPlacementAssets.cpp:1774`. Add focused regression
  coverage that fails without each guard: a camera pose whose view vector is
  numerically coincident with the up axis, asserting the cap engages rather than
  returning the raw request; a matrix construction with a fully inverted normal;
  and a placement whose terrain normal is antiparallel to up. Assert finite
  outputs explicitly — a NaN that propagates silently is the failure this plan
  exists to prevent, so the tests must check `std::isfinite`, not just a value
  range.
- [ ] **TD3 — Close the plan.** Complete the touched-file comment audit against
  `Agentic/Reference/comment-style-guide.md`, obtain one independent rubber-duck
  review answering all five ownership questions, and run the mapped gates.
  Evidence:
  `Agentic/Reports/2026-07-30/inverse-trig-domain-guards-closure.md`.

## Dependencies And Decisions

- Barrier in: `maths-surface-reachability` MR1 before TD1. MR1 deletes
  `GeometricMath::GetHeightFromPlane`, which holds one of the open sites; running
  TD1 first would harden code that is about to be removed.
- **Byte-exactness is the decision rule, not a formality.** Adding a clamp that
  never fires preserves every physics byte. If `tools\validate_physics.bat`
  reports a CSV difference after TD1 or TD2, that clamp *is* firing in a baseline
  scene, which means a live NaN or out-of-domain value existed in the accepted
  baselines. That outcome is a defect discovery, not a baselining opportunity:
  stop, record the reaching input state, and escalate to the owner before any
  artifact changes. This plan carries no bounded-divergence allowance.
- `Matrix4.cpp` is Maths and physics-adjacent. If TD0 proves the site is on a
  shipping path, TD2's change is physics-visible and the rule above applies with
  full force. If it is Debug-only reference code, note that the Debug build is
  the physics baseline writer per `AGENTS.md`, so it still applies.
- Open decision for TD2: if TD0 finds `EditorPlacementAssets.cpp:1774` fully
  protected by its `axisMag` early return, record the proof and leave the site
  unclamped rather than adding a guard that can never fire. A redundant clamp
  with no stated reason is noise the next reader has to re-derive.

## Acceptance

No first-party inverse-trig call site can receive an out-of-domain argument from
a reachable runtime state without an explicit clamp, and every clamp uses one
shared spelling rather than three. `Camera`'s pitch cap
engages at the pole instead of failing open, and its degenerate-basis handling
matches `GetRightVector`. Each new guard has a regression test that fails when
the guard is removed and asserts finite output. Physics remains byte-exact, or a
difference is escalated under the decision rule above.

## Validation

`tools\validate_tests.bat` for the new regression coverage, then
`tools\validate_physics.bat` for byte-exact confirmation if TD0 places
`Matrix4.cpp:406` on a physics-reachable path, then `tools\validate_full.bat`
because `Camera.cpp` is `Runtime/*`.
