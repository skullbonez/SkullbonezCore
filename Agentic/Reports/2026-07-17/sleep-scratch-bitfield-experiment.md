# Sleep Scratch Bitfield Experiment

Date: 2026-07-17  
Branch: `nightrunner-16th-july`  
Owner: physics

## Question

Would grouping bool-like sleep scratch rows into a one-byte bitfield record
reduce the fixed-step working set without hurting deterministic physics or the
1,000-body acceptance load?

The experiment deliberately leaves the SIMD-facing `fixed`, `awake`, and
authoritative `sleepState` byte planes unchanged. Those rows are independently
streamed by stages and expose contiguous spans. Only four transient,
sleep-owner-only rows are grouped:

- point-joint membership;
- island point-joint membership;
- island point-joint relaxation;
- explicit resting-wake visitation.

`PhysicsSleepScratchFlags` is statically asserted to one byte. Replacing four
8,192-row byte reserves with one reserve removes 24,576 capacity bytes. The
bits are neither replay state nor shared worker output; the serial sleep owner
sequences every mutation.

## Same-Tip A/B Method

The clean control executable was saved before editing, then the packed build
was compiled from the same git tip. Both Profile executables were launched in
alternating order for seven pairs per scene with the SIMD kernels explicitly
OFF:

```text
Profile\SKULLBONEZ_CORE.exe --vsync off --fixed-step --shadows off \
  --no-contact-audio --physics-simd-kernels off \
  --scene SkullbonezData/scenes/physics_scale_<1000|2000>.scene.json
```

- Control executable SHA-256:
  `8E05CA6A075D2E4098BD5A4FBC06D1F0B0C705FE45779C99E24484A8EAA2E7A9`
- Packed executable SHA-256:
  `53EC2F1FA5F4BA04B4889836AA5CC034631B9F3A3A4BD31B7A7538A3FB9E8433`
- Each artifact contains 1,140 measured frames.
- Raw CSVs, analyzer JSON, and process logs are retained under
  `TestOutput/bitfield_ab/`.

## Results

All values are milliseconds; paired deltas use `(packed/control)-1`.

| Bodies | Control step median | Packed step median | Median paired step delta | Control integrate median | Packed integrate median |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.9980 | 1.0011 | -0.65% | 0.2017 | 0.2024 |
| 2,000 | 7.8889 | 7.5260 | **-4.65%** | 0.2293 | 0.2342 |

The 1,000-body pairs split direction and remain noise: packed deltas ranged
from -1.43% to +2.70%. The 2,000-body result is consistent in all seven pairs:
-4.80%, -5.51%, -4.65%, -4.83%, -4.31%, -3.84%, and -3.53%.

At 2,000 bodies, the inclusive Broadphase median moved from 6.8537 to
6.4931 ms. Its nested GridBuild marker moved from 6.6744 to 6.3144 ms, and the
still-combined `GridBuild/ScalarBounds` marker moved from approximately 6.5690
to 6.2059 ms. Narrowphase pruning and FastSmallSweepAugment were flat. Because
the packed fields are not read by grid insertion, this is recorded as a
repeatable reserve/cache-placement effect rather than a claim that bit
extraction accelerated broadphase arithmetic.

## Decision

Retain the packed scratch representation. It is neutral at the 1,000-body
acceptance load, measurably improves the 2,000-body stretch load, removes
24 KiB of construction-reserved scratch capacity, and does not alter replay or
the SIMD hot-field contract. Do not generalize this result to `sleepState`,
`awake`, or `fixed`: those byte planes remain intentionally contiguous until a
separate stage-level mask experiment proves otherwise.

## Validation

Final packed-source gates all passed:

- `tools\validate_tests.bat` (~7 s): 284/284 doctest cases and
  21,403/21,403 assertions passed.
- `tools\validate_physics.bat` (~60 s): standalone and runtime-handle smoke
  passed; the 44,401-line deterministic varied-scene CSV matched byte-exactly;
  Profile and Debug builds completed with zero warnings and zero errors.
- `tools\validate_perf.bat` (~95 s): allocation guard, selected-ball replay
  path, DX12 and physics-bench budgets, the 200/520/1,000/2,000 scale matrix,
  final budget analysis, and ready Profile/Debug builds all passed. The final
  1,000-body Step/Broadphase averages were 0.9652/0.2731 ms; the final
  2,000-body Step/Broadphase averages were 7.5576/6.5069 ms. The latter remains
  the scale target for the separate broadphase attribution campaign.

## Comment Audit

Touched-file mode covers four source-bearing files: the sleep-controller
header, main implementation, wake implementation, and state implementation.
All four retain complete learning headers and were inspected for the new packed
scratch ownership, serial-write invariant, memory accounting, and bool-like
access sites. Checked: 4; deferred: 0; unchecked: 0. Checklist path: N/A for
touched-file mode.
