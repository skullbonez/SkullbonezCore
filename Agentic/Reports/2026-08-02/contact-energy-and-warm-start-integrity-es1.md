# Contact Energy And Warm-Start Integrity — ES1

Date: 2026-08-02
Branch: `nightrunner-1st-AUG-26`
Scope: complete-solve energy and momentum oracles; no production solver behavior changed

## Result

Physics now owns one allocation-free `ContactEnergyMeasurement` calculation
over `PhysicsBodyStore`. It sums translational and rotational kinetic energy,
world linear momentum, and world angular momentum about a shared origin. The
non-sphere path rotates angular velocity into body-principal axes, applies the
authored principal inertia, and rotates spin momentum back to world space.
Fixed bodies remain external anchors and are excluded from dynamic totals.

The ES0 bounds are encoded directly:

- closed energy uses `max(1e-6, 64 * FLT_EPSILON * max(abs(E), 1))`;
- each momentum component uses the same factor against its measured magnitude
  scale; and
- biased solves add only the explicit separation work plus the locked
  128-epsilon rounding allowance.

No tolerance includes restitution, friction, cached impulses, or positional
work. No tracked scene, production contact policy, physics baseline, Replay
artifact, or visual golden changed.

## Coverage And Sensitivity

Five focused cases contribute 87 assertions in both Debug and Profile:

1. restitution `0`, `0.5`, and `1` for dynamic/dynamic and dynamic/fixed
   spheres;
2. dynamic box faces, an off-center box/fixed impact that produces rotation,
   dissipative friction, and a rotated anisotropic-inertia terrain row;
3. a matching two-frame contact that proves both a cache hit and applied warm
   start while remaining inside the complete-solve bound;
4. a Baumgarte solve whose positive kinetic work is bounded by the explicit
   sum of `bias * accumulated normal impulse`; and
5. planted failures for an oversized normal impulse, cached scalar applied
   through incompatible contact arms, and restitution `1.1`.

The elastic assertion compares absolute energy delta directly against the
locked tolerance; it does not pass that absolute value into doctest's relative
epsilon API. Every planted energy-injection control is required to fail the
positive oracle, while the momentum-preserving oversized impulse still proves
that momentum conservation alone cannot catch energy creation.

## Integration

`ContactEnergyOracle.h` is registered in `SKULLBONEZ_PHYSICS.vcxproj`, appears
under `Header Files\Bodies`, and is mapped by the deterministic Physics body
prefix rule in `tools/validate_project_filters.py`. The direct project-filter
scan reports 803 project items and 803 filter items with zero errors.

## Validation

- Debug build: PASS, zero warnings/errors.
- Debug `Contact energy oracle:*`: PASS, 5 cases / 87 assertions.
- Profile build: PASS, zero warnings/errors.
- Profile `Contact energy oracle:*`: PASS, 5 cases / 87 assertions.
- `python tools/inventory_glossary_terms.py --repo . --strict`: PASS, 989
  unique terms, zero multi-file terms, drift, rulings, or diagnostics.
- `python tools/check_related_paths.py --repo .`: PASS, 587 files and zero
  findings.
- `tools\validate_fast.bat`: PASS after adding the missing deterministic
  project-filter prefix; formatting, metadata, dependencies, ownership
  inventories, builds, and tests all completed with exit 0.
- `git diff --check`: PASS.

The first fast attempt correctly rejected the project registration because the
new path had no semantic filter rule. That integration defect was repaired and
the direct validator plus the complete fast gate were rerun successfully. A
separate 120-second wrapper attempt was terminated while the healthy gate was
still running; the final run used a 10-minute command allowance and completed.

## Comment Audit

Touched source-bearing scope is 3/3 checked with zero deferred:

- `SkullbonezSource/Physics/ContactEnergyOracle.h`;
- `SkullbonezTests/TestPersistentContactSolver.cpp`; and
- `tools/validate_project_filters.py`.

The oracle header teaches complete-system accounting, body/world inertia
frames, world-origin angular momentum, and the precision-only tolerance hazard.
The test header records the new matrix and explicit separation-work contract.
The validator's existing learning header and nearby invariant correctly state
semantic filter ownership and count-free policy. All behavioral and ownership
claims were checked against the final source; no wording requires owner input.

No independent rubber-duck review is required for this incremental phase. The
plan reserves the mandatory read-only review for ES6, after the correction,
scale evidence, candidate artifacts, and complete validation packet exist.
