# Physics SoA/SIMD S0 Scalar-AoS Baseline

Date: 2026-07-16  
Plan: `physics-soa-simd-1000-bodies`  
Task: S0  
Reference branch: `nightrunner-15th-july`

## Ratified Acceptance Budget

The owner-directed 1,000-body campaign uses this numeric acceptance criterion:

> The final AVX2/FMA cutover binary must average **no more than 0.80 ms per
> Physics Step at 1,000 bodies** on the reference machine, using the S0 authored
> scene and `validate_perf.bat` measurement procedure below.

The scalar-AoS reference averages 0.9978 ms, so the ratified budget requires at
least a 19.8% reduction. It is stricter than merely fitting the 8.333 ms 120 Hz
fixed-step interval and leaves time for non-physics frame work.

## Reference Machine And Method

- CPU: AMD Ryzen Threadripper 3970X 32-Core Processor, 32 cores / 64 logical
  processors, reported maximum clock 3,700 MHz.
- Binary: Profile x64, MSVC v143, `/fp:precise`, `fp_contract(off)`.
- Scenes: authored format v1 with fixed seed `3235774467`, fixed step, no water,
  one camera, shadows disabled, and solver-ball counts of 200, 520, 1,000, and
  2,000. Each scene requests 600 frames; the perf lane supplies the normal
  warmup/measurement behavior and analyzed 1,140 rows per scene.
- Command: `tools\validate_perf.bat`.
- The four scale rows are measurement-only. Existing DX12 and physics-bench
  baselines and absolute budgets remain the hard comparisons before S7.

## Scalar-AoS Measurements

All values are milliseconds. `Physics Step` is the campaign acceptance metric.

| Bodies | Frame avg | Physics avg | Physics p50 | Physics p99 | Apply forces | Broadphase | Grid build | Terrain | Integrate | Narrowphase |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 200 | 0.3948 | 0.0906 | 0.0892 | 0.1552 | 0.0224 | 0.0205 | 0.0173 | 0.0156 | 0.0194 | 0.0005 |
| 520 | 1.3109 | 0.7577 | 0.7626 | 0.9871 | 0.1647 | 0.1440 | 0.1314 | 0.1903 | 0.1865 | 0.0032 |
| 1,000 | 1.7285 | 0.9978 | 0.9997 | 1.3802 | 0.1699 | 0.2751 | 0.2304 | 0.2110 | 0.1998 | 0.0070 |
| 2,000 | 8.7827 | 7.6138 | 9.5769 | 13.0121 | 0.1805 | 6.5494 | 6.3683 | 0.2696 | 0.2444 | 0.0249 |

Analysis artifact SHA-256 values:

- 200: `EDB1B5CFB245D47C4E1ED883BC9109BE2F9213A7E19BE83A52F38E55EADC489A`
- 520: `DAD7C388CD8B1F2D5846FDDB3AA85F1A6AB72BA7ED197217662EBD128371CBA6`
- 1,000: `1F4A78403B36D3AA1EBFF253D1E98337450DDC8CFCE2EC5660F2BF6DD5929940`
- 2,000: `D5C25FCAF99AD51F5B5D60BF3989902AD5051579669AC049CBA1BA69F2085A30`

The 2,000-body stretch row is dominated by spatial-grid build time. It is a
diagnostic stretch point, not the S0 acceptance budget.

## Capacity Audit

- Authored scene active capacity defaults to 4,000 and permits up to 8,192.
- `PhysicsBodyStore` and the relevant fixed stage scratch arrays cover the
  repository-wide 8,192-body maximum. A focused 2,000-body Profile launch
  passed with 63 workers and no capacity diagnostic.
- `SpatialGrid` uses a 4,096-slot table and bounded `MAX_GAME_MODELS` storage.
  The 2,000-body slowdown is occupancy/hash work, not capacity exhaustion.
- Existing issue recorded, not silently changed in S0: sleep counters are
  `uint8_t`, while configuration accepts `physics_sleep_frames` values above
  255. The benchmark uses the default value 30, so this does not block the
  scale scene. Any width correction is a separately ruled behavior task.

## Validation And Certification

- Focused 2,000-body launch, 60 requested frames: passed in 4.00 s.
- `tools\validate_perf.bat`: passed in 94.62 s; hard DX12/physics comparisons
  passed and the four measurement-only artifacts were produced. Log SHA-256:
  `2BE1F76CE3AF7969AA5152E7B140F33EA9B81F279A87521C6C6530E34FF107E9`.
- `tools\validate_physics.bat`: passed in 49.85 s; the 44,401-line regression
  CSV matched byte-for-byte. Log SHA-256:
  `EE613FB16122F60C9023AF72D915F69A60192AF79FD06168E3A098F6C88F88D4`.
- `tools\validate_full.bat`: passed in approximately 108 s; CPU umbrella,
  zero-warning builds, Automation/replay smoke, DX12 screenshots with zero
  InfoQueue errors, and byte-exact physics passed. Log SHA-256:
  `6937A8DD79BDDBC8944DC55026A71ABB28563E98AA668558E10C65580EE89C9D`.
- The single S0 reference `tools\validate_replay_visual_fidelity.bat`
  invocation passed in 427.56 s: 2,401 ticks, 200 moved wall bricks, 187
  toppled bricks, 199 causal nodes, one presented cascade, and all byte/
  semantic/determinism false-pass controls. Log SHA-256:
  `9A469187DF440ABD950F163C6BAF0165A3A38BDB70CBB72DBA449C44B2D8C29A`.
- No physics baseline, replay golden, screenshot baseline, or perf baseline was
  refreshed.

## Comment Quality Audit

Touched source-bearing tool inventory: 1 file (`tools/validate_perf.bat`).
Checked: 1. Deferred: 0. Its learning header defines the new scale-matrix
vocabulary and explicitly documents the measurement-only/non-gating invariant.
