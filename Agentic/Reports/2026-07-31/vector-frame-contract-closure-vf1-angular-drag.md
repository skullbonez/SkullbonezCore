# Vector Frame Contract Closure — VF1 Angular Drag

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Impact area: Physics angular-drag clamping and frame conversion
Phase: VF1 complete

## Outcome

General angular drag now enforces its no-reversal limit in body-principal axes
for rotated anisotropic bodies. The clamped torque returns to world space before
joining other world forces, so `ApplyWorldImpulse` remains the sole owner of the
world-to-body response conversion.

The correction changes no committed baseline. Core and deep Physics reproduce
every mapped CSV, known-issue signature, and SkullScope query artifact exactly.
Physics does **not** need a new baseline.

## Defect Oracle

The focused fixture uses a +45-degree Z rotation, body-principal inertia
`(1, 10, 100)`, world angular velocity `(1, 2, 0.5)`, a box collider, and a gas
drag coefficient large enough to saturate only the first body axis. It derives
the expected result independently:

```text
omega_body = R^T * omega_world
torque_body = R^T * (-drag_scale * omega_world)
limit_axis = abs(omega_body_axis) * inertia_axis / dt
clamped_body_axis = clamp(torque_body_axis, -limit_axis, +limit_axis)
omega_body' = omega_body + clamped_body * dt / inertia_body
omega_world' = R * omega_body'
```

Before the production change, the oracle failed with world X/Y results near
`(-6.31646, -6.72012)` instead of `(-0.124086, 0.124085)`, and the saturated
body X component had magnitude `9.21825` instead of stopping at zero. This is
the mixed-frame defect: world torque/velocity components were limited by an
unrotated body diagonal.

After the change, both world components match the independent result and the
saturated body X component is zero within `1e-5`.

## Corrected Ownership And Math

`ClampAngularDragTorque` now owns only the clamp frame:

1. isotropic/frame-neutral records retain the original componentwise world
   clamp verbatim;
2. anisotropic records rotate world angular velocity and drag torque into body
   axes;
3. each body component is bounded by
   `abs(omega_body) * inertia_body / dt`;
4. if no component changed, the helper returns the original world torque
   exactly, avoiding a harmless but artifact-visible round trip; and
5. if a component changed, the helper rotates the clamped body torque back to
   world space.

`ApplyWorldImpulse` then performs its existing world-to-body conversion once
for the summed world torque. The helper does not pass a body-space torque into
that function and therefore does not double-transform the response.

The wet sphere-only damping branch remains unchanged. A separate rotated
isotropic sphere case actively saturates the general clamp and is
component-exact against the pre-VF1 arithmetic, including the historical
world/body response round trip.

## Artifact Result

VF0 predicted zero changed committed bytes because mapped scenes have zero
effective angular-drag density at their bodies. VF1 confirms that prediction:

- core `physics_regression_varied.csv`: two 44,401-line output runs, exact to
  one committed baseline run;
- deep varied, bullet wall/object/terrain, shooting reaction, and three-body
  CSVs: byte-exact;
- known-issue signatures: exact;
- shooting target reactions: all ten pass; and
- `physics_query_varied.json`: exact.

The existing deep `at_rest` whole-file regression remains the authority. VF1
adds no redundant frame assertion and refreshes no golden.

## Comment Audit

Checklist path: this report.

- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- [x] `SkullbonezTests/TestPhysicsApi.cpp`

Checked: 2. Deferred: 0. Unchecked: none.

`PhysicsBodyStore.cpp` keeps its ownership-bearing header and adds the local
no-clamp exactness/world-return invariant beside the conversion. The test
header now names query, joint, and angular-drag oracles and links the body-store
implementation it exercises. All `Related:` paths resolve. The strict glossary
inventory reports 965 unique definitions, no shared/drifted terms, and zero
diagnostics.

## Validation

- Profile build: pass.
- Expected-failure oracle before production edit: 1/2 cases fail only the
  anisotropic assertions; isotropic legacy arithmetic passes exactly.
- Focused post-change selection: 2/2 cases, 13/13 assertions, pass.
- Formatting and strict glossary inventory: pass.
- `validate_physics`: pass; 44,401-line varied output byte-exact.
- `validate_physics_deep`: pass; all mapped artifacts exact.
- `validate_fast`: 457/457 cases and 2,422,977/2,422,977 assertions pass, as do
  dependency, ownership, complexity, formatting, project-filter, Profile, and
  Debug checks.
- The first fast invocation failed closed only because repository-wide format
  write mode refreshed unchanged source mtimes after the existing Automation
  objects were built. A current Automation rebuild cleared that provenance
  issue; the direct strict reachability scan then reported zero blocking
  diagnostics, and the complete fast rerun passed.
- After strengthening the isotropic fixture to activate its clamp, the
  unchanged production source rebuilt in Profile and the focused 2/2,
  13-assertion selection passed again.

The performance lane remains assigned to VF4, where it will measure the final
landed source once rather than interleaving phase commits. No baseline refresh
or owner decision is required for VF1.
