# Catto Optimization Report - DX Fixed-Step Pacing

Date: 2026-06-02
Base commit during validation: fd57e0c
Working-tree phase: DX vsync-off fixed-step pacing fix plus varied physics benchmark

## Problem

`--fixed-step --vsync off` made OpenGL free-run quickly, while DX11/DX12 behaved slowly. The physics step is one fixed tick per rendered frame, so any renderer present throttle directly reduces simulation throughput and makes physics benchmarking misleading.

The earlier generated solver benchmarks were also weak for this diagnosis because many objects spent the measured window airborne. They did not sustain varied ground, terrain, object-object, sleeping, or wake-up behavior.

## Changes

- DX11 now queries `DXGI_FEATURE_PRESENT_ALLOW_TEARING`, creates/resizes the flip swap chain with `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` when available, and uses `DXGI_PRESENT_ALLOW_TEARING` for `vsync off`.
- DX12 now applies the same tearing support path.
- DX12 no longer blocks `Present()` waiting for an old GPU timer readback in free-running mode; stale timer readbacks are dropped and the next fence publishes fresh data.
- Added `SkullbonezData/scenes/physics_bench_varied.scene`, a 1200-frame fixed-step contact-heavy benchmark.
- Added the varied scene to `physics_bench.suite`.
- Updated `Agentic/Skills/bench_report.py` so the varied benchmark appears in physics reports.

## Varied Benchmark

Command pattern:

```bat
Profile\SKULLBONEZ_CORE.exe --vsync off --fixed-step --scene SkullbonezData/scenes/physics_bench_varied.scene
Profile\SKULLBONEZ_CORE.exe --renderer dx11 --vsync off --fixed-step --scene SkullbonezData/scenes/physics_bench_varied.scene
Profile\SKULLBONEZ_CORE.exe --renderer dx12 --vsync off --fixed-step --scene SkullbonezData/scenes/physics_bench_varied.scene
```

Archived CSVs:

- `Profile/catto_optimization/varied_benchmark/gl_varied_physics_perf_log.csv`
- `Profile/catto_optimization/varied_benchmark/dx11_varied_physics_perf_log.csv`
- `Profile/catto_optimization/varied_benchmark/dx12_varied_physics_perf_log.csv`

Results use pass 2, 1200 logged frames.

| Renderer | Full Run Wall Time | Frame p50 ms | Physics p50 ms | Render p50 ms | GPU Render p50 ms | Present/VsyncWait p50 ms |
|---|---:|---:|---:|---:|---:|---:|
| GL | 2.063 s | 0.3394 | 0.0920 | 0.0925 | 0.0768 | 0.1404 |
| DX11 | 2.026 s | 0.2564 | 0.0950 | 0.0264 | 0.0543 | 0.1044 |
| DX12 | 4.059 s | 0.7464 | 0.0976 | 0.3362 | 0.1137 | 0.2999 |

## Physics Needle

The physics needle did not move differently per renderer: `Frame/Physics` p50 stayed around 0.092-0.098 ms across GL, DX11, and DX12. That means the original symptom was not a solver speed difference. It was renderer/present pacing affecting how many fixed physics ticks could run per wall-clock second.

The DX needle moved in frame throughput:

- DX11 now free-runs at parity with GL on the varied fixed-step benchmark.
- DX12 now free-runs instead of monitor-rate clamping, but still has higher CPU render/present overhead than GL/DX11.
- Remaining DX12 work should focus on CPU render submission and present wait behavior, not physics.

## Suite Report Smoke

After adding the varied scene to `physics_bench.suite`, the report script produced:

| Mode | Avg Physics ms | P50 Physics ms | P95 Physics ms | Frames |
|---|---:|---:|---:|---:|
| Legacy 300b | 0.4232 | 0.4961 | 0.7042 | 2218 |
| Solver 300b | 0.6052 | 0.6373 | 1.1715 | 1794 |
| Solver 150b+150box | 0.5725 | 0.6454 | 0.9518 | 1780 |
| Solver 300box | 0.5428 | 0.5534 | 0.7517 | 1801 |
| Solver varied 20s | 0.0916 | 0.0923 | 0.1223 | 1200 |

## Validation

Command:

```bat
tools\validate_full.bat
tools\validate_fast.bat
```

Result:

- `validate_renderers`: passed.
- DX12 InfoQueue: 0 validation errors.
- Cross-renderer parity: all average pixel diffs below 10.
- `validate_physics`: passed, byte-exact match against `physics_regression_solver.csv`.
- `validate_perf`: completed; baseline comparison skipped because the current machine string did not match the stored baseline machine.
- `validate_fast`: passed after adding this report.
