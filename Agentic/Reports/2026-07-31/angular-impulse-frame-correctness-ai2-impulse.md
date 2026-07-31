# Angular Impulse Frame Correctness AI2

Date: 2026-08-01
Plan: `Agentic/Plans/TODO/angular-impulse-frame-correctness.md`
Phase: AI2
Branch: `nightrunner-1st-AUG-26`

## Outcome

`ApplyPendingImpulse` now converts its world-space torque through body-frame
diagonal inertia before returning the result to world angular velocity. One
Physics-owned helper supplies the same frame conversion to pending gameplay
impulses, world-force integration, and contact-solver impulses while leaving
each caller's established diagonal division or multiplication semantics intact.

The AI0 rotated anisotropic-box characterization is now an ordinary passing
test. A second focused test rotates an isotropic sphere and compares every
angular-velocity component exactly with the pre-change direct division, proving
that the sphere path did not acquire rotation-rounding drift.

## Owner Ruling: No Extra `at_rest` Frame Check

Owner direction on 2026-08-01 removes the proposed prerequisite to add a
separate exact all-asleep-frame assertion for `at_rest.scene.json`. The existing
deep Physics lane already launches that scene and hashes the entire generated
CSV. The final AI2 run produced 7,649,427 bytes / 54,001 lines at SHA-256
`0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984`,
which exactly matches the committed known-issue signature. No scene, validation
tool, or baseline changed.

## Validation

| Command | Result |
|---|---|
| `tools\validate_build.bat Profile` | PASS in 46.7 s; zero warnings/errors |
| `Profile\SKULLBONEZ_TESTS.exe --test-case=*Pending gameplay impulse*` | PASS; 2 cases / 15 assertions |
| `tools\validate_tests.bat` | PASS in 61.9 s; 453 cases / 2,422,921 assertions; zero warnings/errors |
| `tools\validate_physics.bat` | PASS in 94.8 s; two generated 44,401-line runs match the committed core baseline byte-for-byte |
| `tools\validate_physics_deep.bat` | PASS in 109.9 s; all CSVs, known-issue signatures, and SkullScope query output exact |

The touched-source comment audit is 4/4 with zero deferred files:

- `SkullbonezSource/Physics/PersistentContactSolver.cpp`
- `SkullbonezSource/Physics/PhysicsBodyStore.cpp`
- `SkullbonezSource/Physics/PhysicsBodyStore.h`
- `SkullbonezTests/TestPersistentContactSolver.cpp`

No physics, query, performance, replay, interaction, screenshot, scene, config,
schema, or baseline artifact was modified. AI3 is next and remains
investigation/reporting only.
