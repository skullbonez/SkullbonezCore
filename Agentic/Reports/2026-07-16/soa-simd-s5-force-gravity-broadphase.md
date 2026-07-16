# SoA/SIMD S5 — Force, Mutual Gravity, And Broadphase

Date: 2026-07-16
Branch: `nightrunner-15th-july`
Plan task: `physics-soa-simd-1000-bodies` S5

## Outcome

S5 adds three default-OFF AVX2/FMA paths without changing the scalar oracle:

- eight-lane universal-gravity velocity updates followed by the existing
  store-owned drag, buoyancy, torque, and pending-impulse completion;
- eight-pair mutual-gravity construction under the existing `<=512` pair-table
  path, with the original scalar model-order reduction and untouched `>512`
  serial fallback;
- eight-lane broadphase displacement/AABB preparation, consumed in model order
  by `SpatialGrid`, which retains bucket, capacity, traversal, and pair-order
  ownership.

The kernels compile with AVX2 only in their dedicated translation units. The
project-wide instruction-set policy remains unchanged and
`physicsExecution.simdKernels` remains default OFF.

## Direct Coverage

`TestSolverBroadphaseStage.cpp` now locks:

- gravity completion masks for awake, sleeping, fixed, zero-inverse-mass, and
  masked-tail rows;
- independent mutual-pair rows and a two-lane tail;
- broadphase valid/swept masks, fixed-body handling, prepared bounds, and a
  two-lane tail.

The current Profile and Debug builds both completed with zero warnings and zero
errors. The focused test runner passed 207/207 cases and 17,353/17,353 checks
before the final repository gates.

## A/B Stability Evidence

### Chaotic 1,000-body fixture

Command shape:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --frames 1000 --scene SkullbonezData/scenes/physics_scale_1000.scene.json --physics-regression-log <artifact> --physics-simd-kernels <off|on>
```

The comparator streamed 2,000,000 rows across two synchronized 1,000-tick
sessions. It found zero NaN/Inf values. Final-frame count deltas were three
grounded and three sleeping bodies in each session, within the explicit count
limit of ten. Maximum absolute divergence was:

| Field group | Maximum |
|---|---:|
| Position X/Y/Z | 218.8961 / 47.9878 / 184.9777 |
| Velocity X/Y/Z | 109.0962 / 151.7058 / 108.3791 |
| Quaternion X/Y/Z/W | 1.847754 / 1.878746 / 1.955890 / 1.928831 |
| Angular X/Y/Z/magnitude | 60.2330 / 18.9659 / 107.3819 / 107.8796 |
| Speed | 100.7836 |

The S4 oracle's generic 128 velocity limit exposed one S5-specific result:
`velY=151.7058`. S5 therefore records a field-specific `velY=160` bound; it
does not raise the generic velocity limit. Position remains bounded at 256,
quaternion components at 2, generic dynamic fields at 128, and final outcome
count deltas at 10. The resulting oracle passes.

Artifacts:

- OFF SHA-256:
  `93BF7D0C877541B6D0171AF63BF2C393FC2EA4524452D7F4715DD3E9A72435A0`
- ON SHA-256:
  `2511E9B000CF8FAC49D00EF7D6A0DB1AD86ADEB513CD2D8436B38D28F0F8AED9`
- comparator: `TestOutput/s5_simd_ab_1000_current.json`

The broadphase exact-AABB consumption optimization was applied after the first
ON capture. A fresh current-tip capture has the exact same byte length and
SHA-256 as the original ON artifact above. Thus the original mismatch remains
byte-exactly reproducible with the final S5 implementation; it was not moved or
masked by the optimization.

### Focused 200-body mutual-gravity fixture

`space_field_200.scene.json` exercised the `<=512` pair-table kernel for 360
ticks. The comparator covered 72,000 rows, found zero non-finite values, zero
ground/sleep outcome changes, and maximum logged position/velocity divergence
of 0.0001. Its final artifacts are:

- OFF SHA-256:
  `97E28A55AC36F461BD87F2AC4430DFBA49CC2C60CE85010369C126D51C5E90B5`
- ON SHA-256:
  `61D6B564C30ED1997A3068C1FBB745CDCB9DC8706E5595029AFFCAA5DC9D3837`
- comparator: `TestOutput/s5_mutual_ab_360.json`

The final ON artifact also matches the pre-optimization ON artifact byte for
byte. The `>512` mutual-gravity branch remains the original scalar fallback by
construction and direct source review.

## Performance Evidence

Two paired current-tip Profile captures of the 1,000-body fixture produced the
following mean marker values. Negative is faster; positive is slower.

| Marker | OFF ms | ON ms | Delta |
|---|---:|---:|---:|
| Apply-forces scalar/SIMD body region | 0.1722 | 0.1771 | +2.85% |
| Broadphase scalar/SIMD bounds region | 0.1965 | 0.2198 | +11.88% |
| Integration scalar/SIMD region | 0.1755 | 0.1773 | +1.03% |
| Physics total | 1.0116 | 1.0601 | +4.79% |
| Frame total | 1.7830 | 1.8827 | +5.59% |

A separate paired Profile capture of the 200-body mutual-gravity fixture
measured `Frame/Physics/MutualGravity/PairBuild` at 0.0816 ms OFF and 0.0851 ms
ON (+4.29%).

This is deliberately recorded as a negative performance finding, not described
as a speedup. These dark kernels establish the ownership, mask, and numerical
envelope but do not yet justify S7 cutover on this reference machine. S7's
existing precondition remains binding: the complete enabled kernel set must
meet the ratified 0.80 ms 1,000-body budget. If S6 does not offset this cost,
kernel optimization or rejection is required before cutover; no baseline or
golden refresh is authorized by S5.

## Formal Gates

- `tools\validate_fast.bat`: passed in 49 seconds. Project/filter metadata was
  exact, Profile/Debug builds had zero warnings/errors, and the focused runner
  passed 207/207 cases with 17,353/17,353 assertions.
- `python tools\validate_project_filters.py`: passed independently at 720/720
  production project/filter items with zero errors.
- `tools\validate_physics.bat`: passed in 50 seconds. The toggle-OFF
  `physics_regression_varied.csv` proof remained a 44,401-line byte-exact match
  with no baseline update.
- `tools\validate_perf.bat`: passed in 1 minute 42 seconds. Allocation policy,
  the gameplay allocation guard, absolute budgets, regression checks, the
  measurement-only 200/520/1,000/2,000 matrix, and zero-warning final builds
  all completed successfully.

## Comment Audit

The final source audit covers 14 touched source-bearing files: the four kernel
files, both stage implementations and their shared context, `PhysicsBodyStore`
header/implementation, `PhysicsWorld.cpp`, `SpatialGrid` header/implementation,
the focused test file, and the project-filter validator. All 14 are inspected;
none are deferred. Learning headers and nearby ownership, masking, fallback,
and lifetime comments describe the non-obvious boundaries.
