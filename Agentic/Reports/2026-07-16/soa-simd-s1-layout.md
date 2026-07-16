# Physics SoA/SIMD S1 Hot-Field Layout Evidence

Date: 2026-07-16  
Plan task: S1  
Commit subject target: `physics-soa-simd-1000-bodies, TASK 2 / 9, 22% OVERALL COMPLETE — split hot physics state into SoA storage`

## Result

`PhysicsBodyStore` now owns 20 fixed-capacity, 32-byte-aligned component arrays:
position xyz, orientation xyzw, linear velocity xyz, angular velocity xyz,
inverse mass, inverse inertia xyz, bounding radius, fixed, and awake. The arrays
use the existing inline `PhysicsFixedList` capacity owner, so they add no heap or
runtime growth path.

S1 deliberately retains the public `PhysicsBodyRecord` hot fields as a
transitional view. A one-writer dirty-state seam copies component bits only when
control crosses between record and SoA access. It performs no arithmetic and
preserves dense row order. S2's deletion condition is explicit: migrate every
stage/replay/presentation/diagnostics consumer to the narrow SoA spans, then
remove the record-side hot fields, dirty-state seam, and copy helpers.

The focused unit test checks all 20 array addresses for 32-byte alignment and
proves SoA-to-record and record-to-SoA changes retain exact float/flag values.

## Validation

- Profile build: passed with 0 warnings and 0 errors in 22.25 s.
- `tools\validate_tests.bat`: 204/204 tests and 12,633/12,633 assertions passed
  in 8.22 s. Final log SHA-256:
  `D56BF08EE1397B42DE27BDEE6CC631429E55C12B7F42376BA2572CF2D72CE647`.
- `tools\validate_physics.bat`: passed in 75.18 s; the 44,401-line varied CSV
  matched byte-for-byte. Log SHA-256:
  `204C54F22E666C4B84465AE3B9FC2D4BD1BDD17B4445D56C0FB4DE4A1B456967`.
- `tools\validate_physics_deep.bat`: passed in 127.54 s. The varied, three
  bullet-sweep, shooting-reaction, and three-body CSV artifacts were byte-exact;
  known-issue signatures and `physics_query_varied.json` matched exactly. Log
  SHA-256:
  `ACF8CC4F72B0BBA202085544E1932D4BC203A880BC6F66C5F83DDDA49AAB35AC`.
- `python tools\check_allocation_policy.py --self-test`: passed.
- `python tools\check_allocation_policy.py --repo .`: passed; scanned 370 files,
  with 0 allowlist errors. No allowlist row changed.
- `tools\validate_format.bat`: passed for 252 headers and all source files.
- No baseline, golden, screenshot reference, or perf reference was refreshed.

## Comment Quality Audit

Source-bearing touched-file inventory: 3. Checked: 3. Deferred: 0.

- `SkullbonezSource/Physics/PhysicsBodyStore.h`: learning header now defines hot
  SoA vocabulary/alignment, while the declarations document borrowed lifetimes,
  the compatibility seam, and the S2 deletion condition.
- `SkullbonezSource/Physics/PhysicsBodyStore.cpp`: nearby `Concept:` and
  `Invariant:` comments explain the one-writer synchronization rule and why
  copying is bit-neutral.
- `SkullbonezTests/TestPhysicsHandles.cpp`: learning header defines the new test
  vocabulary and the exact bidirectional-coherence invariant.
