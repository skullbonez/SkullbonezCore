# SoA/SIMD S4 — Dark Integration Pilot

Date: 2026-07-16
Branch: `nightrunner-15th-july`
Implementation base: `d9a01df1e`
Owner: physics

## Outcome

S4 adds the first pinned AVX2/FMA physics kernel behind
`physicsExecution.simdKernels`, which remains default OFF. The integration
pilot advances eight position/velocity rows at a time, uses masked loads and
stores for partial tails, returns its active-lane mask, and leaves orientation,
terrain clamping, and water-sample invalidation with `PhysicsBodyStore`.

The kernel translation unit alone is compiled with AVX2. There is no runtime
dispatch, CPU feature probe, FTZ/DAZ change, or project-wide `/arch:AVX2`
setting before S7. The OFF path retains the scalar loop and matched the existing
44,401-line physics artifact byte-for-byte.

## Configuration And Compatibility

- Engine config format advances from v2 to v3.
- `physics_simd_kernels = 0` is written beside
  `physics_parallel_integrate` and is the deterministic v2→v3 migration value.
- Current/previous/future format tests, the 221-key registry hash, and render-
  defaults writer tests were updated.
- `--physics-simd-kernels [on|off]` is an explicit diagnostic/benchmark
  override; it reports that the enabled path is the pinned AVX2/FMA path.
- `migrate_data_formats.py --self-test` passed and `--check` passed all 39
  authored files.

## Mask And Tail Coverage

`IntegrationKernel.cpp` builds an eight-lane validity/eligibility mask from
the body bound, fixed/awake state, sleep state, and positive remaining time.
The final partial block keeps the same vector arithmetic and masks absent rows;
it does not use a scalar tail. Focused coverage proves the first-block mask
`0xC3`, a two-row tail mask `0x03`, inactive-row preservation, velocity epsilon
simplification, and explicit `std::fma` position results. All 205 doctest cases
and 17,337 assertions passed.

## A/B Oracle And Evidence

The streaming oracle is `tools/check_physics_simd_ab.py`. It compares rows in
lockstep without exposing the million-row artifacts to the model, requires
identity/order agreement, rejects every NaN or infinity, records every
per-field maximum and per-body discrete mismatch, and evaluates the documented
high-energy scale-scene envelope:

- position component maximum: 256 world units;
- linear/angular velocity and magnitude maximum: 128 units;
- quaternion component maximum: 2;
- final sleeping, grounded, and sleep-inhibited count delta: at most 10 bodies
  (1% of the 1,000-body fixture).

The paired Debug commands were identical except for the final toggle:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --shadows off --frames 1000 --scene SkullbonezData/scenes/physics_scale_1000.scene.json --physics-regression-log TestOutput/s4_simd_{off|on}_1000.csv --physics-simd-kernels {off|on}
```

Each artifact contains two synchronized 1,000-tick engine sessions, separated
by the logger's repeated header, matching the repository's two-run regression
shape. The OFF artifact is 280,753,524 bytes and the ON artifact is
280,776,636 bytes. The oracle processed 2,000,000 body rows and passed:

| Field | Maximum absolute divergence |
|---|---:|
| `posX`, `posY`, `posZ` | 218.7944, 57.5046, 183.8850 |
| `velX`, `velY`, `velZ`, `speed` | 107.3065, 108.0541, 116.9447, 91.6604 |
| `omegaX`, `omegaY`, `omegaZ`, `omegaMag` | 59.9705, 23.1339, 107.3819, 107.8796 |
| `qX`, `qY`, `qZ`, `qW` | 1.878650, 1.933785, 1.922206, 1.877669 |

The first logged tolerance-visible divergence occurs at frame 146. Contacts
then amplify the intentional FMA rounding difference into different chaotic
trajectories; this is why the report does not claim per-body equivalence after
contact. At final frame 999, OFF/ON aggregate outcomes are 317/311 grounded,
16/11 sleeping, and 0/0 sleep-inhibited. Deltas are therefore 6, 5, and 0,
inside the explicit 1% stability envelope. There were zero non-finite values.
The raw per-body mismatch totals remain in
`TestOutput/s4_simd_ab_1000.json`; they are not hidden by the aggregate oracle.

## Performance

Paired 1,000-body Profile runs used the same binary and command, differing only
by `--physics-simd-kernels off|on`, for 1,140 measured frames each:

| Marker | OFF average | ON average | Change |
|---|---:|---:|---:|
| Integration body/kernel marker | 0.1874 ms | 0.1789 ms | 4.5% faster |
| `Frame/Physics/Integrate` | 0.2139 ms | 0.2046 ms | 4.35% faster |
| `Frame/Physics/Step` | 1.0728 ms | 1.0346 ms | 3.56% faster |

The formal default-OFF `validate_perf` run completed the
200/520/1,000/2,000-body matrix, passed allocation guard, absolute budgets, and
baseline comparisons, and measured the 1,000-body Physics Step at 1.0700 ms.
S4 is one pilot only; the final 0.80 ms campaign budget remains an S7 cutover
acceptance condition.

## Validation

- `Profile\SKULLBONEZ_TESTS.exe`: 205/205 cases and 17,337/17,337 assertions.
- `python tools/check_physics_simd_ab.py --self-test`: passed.
- `python tools/migrate_data_formats.py --self-test`: passed.
- `python tools/migrate_data_formats.py --check`: 39 files passed.
- `python tools/validate_project_filters.py`: 716 project items and 716 filter
  items, zero errors.
- `tools\validate_physics.bat`: passed; standalone smoke and 44,401-line
  byte-exact baseline match. Log: `TestOutput/s4_validate_physics.log`.
- `tools\validate_perf.bat`: complete; allocation guard and all enforced
  budgets/comparisons passed. Log: `TestOutput/s4_validate_perf.log`.
- `tools\validate_full.bat`: default gate passed; CPU umbrella, zero-warning
  builds, Automation/replay, DX12 screenshots, zero InfoQueue errors, and
  byte-exact physics passed. Log: `TestOutput/s4_validate_full_pass.log`.
- Touched-file comment audit: 17/17 source/tool files checked, zero deferred.

Substantial timings: Profile build 12.20 s, Debug build 27.35 s, CPU tests
3.10 s, each 1,000-tick Debug A/B pass about 75 s, streaming comparison
about 29 s, each paired Profile pass about 15 s, formal physics about 54 s,
formal performance about 150 s, and the final successful full gate about
170 s. S4 elapsed wall time was approximately 32 minutes including diagnosis,
two formatting-preflight corrections, documentation, and all final gates.
