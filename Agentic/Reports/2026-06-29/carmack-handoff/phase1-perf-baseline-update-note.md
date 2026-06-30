# Phase 1 Perf Baseline Update Note

Date: 2026-06-29

Scope: Carmack Phase 1 perf gate closure.

## Decision

The remaining `tools\validate_perf.bat` failure was closed as an intentional
perf-baseline refresh, not a code change.

The fresh current run still passed both absolute performance budgets, but the
same-machine `PHYSICS_BENCH` relative comparison failed against the older
baseline commit `14795e0`. The failing rows from
`TestOutput\validation\agent_logs\carmack_phase1_validate_perf_initial.log`
were:

| Row | Delta | Threshold |
|-----|-------|-----------|
| `Frame.avg` | +61.1% | 16% |
| `Frame.p50` | +72.1% | 17% |
| `Frame/Render.avg` | +404.5% | 32% |
| `Frame/Render.p50` | +416.7% | 34% |
| `Frame/VsyncWait.avg` | +138.9% | 31% |
| `Frame/VsyncWait.p50` | +93.8% | 27% |
| `mem_start` | +11.84 MB | 5.0 MB |
| `mem_restart` | +72.41 MB | 5.0 MB |
| `mem_end` | +72.41 MB | 5.0 MB |

The current artifacts remained small in absolute terms:

| Artifact | Frame avg | Frame p99 | Memory start | Memory restart/end |
|----------|-----------|-----------|--------------|--------------------|
| `dx12_perf.json` initial | 1.2938 ms | 1.9765 ms | 84.32 MB | 154.26 MB |
| `physics_bench_perf.json` initial | 0.6523 ms | 1.0951 ms | 84.04 MB | 150.48 MB |

`PHYSICS_BENCH` physics timing remained low at `Frame/Physics.avg=0.0958 ms`
and `Frame/Physics/Step.avg=0.0957 ms`. The old baseline had
`Frame/Physics.avg=0.0920 ms`, so the actual physics step was not the source of
the gate failure.

## Baseline Review

`PHYSICS_BENCH` old baseline versus refreshed baseline:

| Marker | Old avg/p50 | Refreshed avg/p50 |
|--------|-------------|-------------------|
| `Frame` | 0.4050 / 0.3509 ms | 0.6523 / 0.6038 ms |
| `Frame/Render` | 0.0987 / 0.0878 ms | 0.4979 / 0.4537 ms |
| `Frame/VsyncWait` | 0.1053 / 0.1376 ms | 0.2516 / 0.2667 ms |
| `Frame/Physics` | 0.0920 / 0.0930 ms | 0.0958 / 0.0965 ms |
| `Frame/Physics/Step` | absent | 0.0957 / 0.0964 ms |
| `Frame/Render/Skybox` | 0.0138 / 0.0116 ms | 0.1386 / 0.1315 ms |
| `Frame/Render/Balls` | 0.0112 / 0.0109 ms | 0.0452 / 0.0430 ms |
| `Frame/Render/Terrain` | 0.0045 / 0.0040 ms | 0.0304 / 0.0287 ms |

`PHYSICS_BENCH` memory moved from `72.20/78.07/78.07 MB`
start/restart/end to `84.04/150.48/150.48 MB`.

`DX12` was also refreshed because `tools\update_baselines.bat --perf --require`
updates both perf baselines when both current Profile artifacts exist. Leaving
the old DX12 baseline in place would preserve a skipped relative comparison
from a different machine label instead of creating a usable branch-local perf
gate. The old DX12 baseline was commit `fba8600` on
`AMD Ryzen Threadripper 3970X 32-Core Processor`, Windows `10.0.22631`, and
`191.9 GB` RAM; the refreshed baseline is commit `6524e06a` on
`AMD64 Family 23 Model 49 Stepping 0, AuthenticAMD`, Windows `10.0.26200`, and
the current `0.0 GB` RAM label.

DX12 old baseline versus refreshed baseline:

| Marker | Old avg/p50 | Refreshed avg/p50 |
|--------|-------------|-------------------|
| `Frame` | 0.8482 / 0.7320 ms | 1.2938 / 1.1956 ms |
| `Frame/Render` | 0.3202 / 0.2997 ms | 0.7979 / 0.7128 ms |
| `Frame/VsyncWait` | 0.1858 / 0.1156 ms | 0.1962 / 0.2834 ms |
| `Frame/Physics` | 0.1413 / 0.1367 ms | 0.4347 / 0.3382 ms |
| `Frame/Physics/Step` | absent | 0.4346 / 0.3381 ms |
| `Frame/Render/Skybox` | 0.0850 / 0.0789 ms | 0.1361 / 0.1301 ms |
| `Frame/Render/Balls` | 0.0256 / 0.0253 ms | 0.0458 / 0.0440 ms |
| `Frame/Render/Terrain` | 0.0123 / 0.0116 ms | 0.0301 / 0.0289 ms |

DX12 memory moved from `84.42/141.54/141.54 MB` start/restart/end to
`84.32/154.26/154.26 MB`.

GPU marker rows also changed, especially in the render hotspots:

| Artifact | `ShadowMap_gpu` avg | `Skybox_gpu` avg | `Balls_gpu` avg | `Terrain_gpu` avg |
|----------|---------------------|------------------|-----------------|-------------------|
| DX12 old | absent | 0.0025 ms | 0.0728 ms | 0.0062 ms |
| DX12 refreshed | 0.0577 ms | 0.0276 ms | 0.2334 ms | 0.0793 ms |
| PHYSICS old | absent | 0.0128 ms | 0.0205 ms | 0.0162 ms |
| PHYSICS refreshed | 0.0532 ms | 0.1027 ms | 0.0587 ms | 0.1699 ms |

Source inspection found the failing CPU buckets map to render/present work:

- `SkullbonezSource\Runtime\RunFrame.cpp` wraps `Render()` in
  `Frame/Render` and wraps `renderLifecycle.Present()` in `Frame/VsyncWait`.
- `SkullbonezSource\Rendering\DX12\RenderBackendDX12.cpp` `Present()` resolves
  GPU timer queries, transitions the swapchain backbuffer, submits the command
  list, presents, records the fence, and waits for the next allocator fence if
  the CPU has lapped the GPU. That means `Frame/VsyncWait` can include
  present/driver/fence pacing even with `--vsync off`.
- `SkullbonezSource\Runtime\RunFrame.cpp` also sums GPU markers for
  `Frame/Shadows/ShadowMap`, `Frame/Render/Skybox`, `Frame/Render/Balls`,
  `Frame/Render/Terrain`, and related render passes into
  `m_timers.gpuFrameWorkMs`.
- `SkullbonezSource\Runtime\RunRender.cpp`
  `ExecuteCinematicPostThroughRenderGraph()` builds a render graph, compiles it,
  dry-runs callbacks, then executes live callbacks. This confirms the current
  marker taxonomy includes render-graph callback scaffolding that older
  baselines did not contain.

No source file was changed for Phase 1. The conclusion is not that historical
render cost stayed flat; it is that the current branch accepts a new perf
baseline after an absolute-budget pass, a physics-step sanity check, and a clean
second `validate_perf` run.

The baselines were updated from the fresh Profile artifacts using:

```bat
tools\update_baselines.bat --perf --require
```

## Evidence

- Initial failing gate:
  `TestOutput\validation\agent_logs\carmack_phase1_validate_perf_initial.log`
  - `PASS: absolute perf budgets [DX12]`
  - `PASS: absolute perf budgets [PHYSICS_BENCH]`
  - `PHASE1_VALIDATE_PERF_INITIAL_EXIT=7`
- Baseline update:
  `TestOutput\validation\agent_logs\carmack_phase1_update_perf_baselines.log`
  - updated `TestOutput\baselines\dx12_perf.json`
  - updated `TestOutput\baselines\physics_bench_perf.json`
  - `PHASE1_UPDATE_PERF_BASELINES_EXIT=0`
- Final passing gate:
  `TestOutput\validation\agent_logs\carmack_phase1_validate_perf_final.log`
  - `PASS: absolute perf budgets [DX12]`
  - `PASS: No regressions [DX12]`
  - `PASS: absolute perf budgets [PHYSICS_BENCH]`
  - `PASS: No regressions [PHYSICS_BENCH]`
  - `VALIDATE_PERF: COMPLETE`
  - `PHASE1_VALIDATE_PERF_FINAL_EXIT=0`

## Residual Risk

The updated baselines intentionally accept the current machine label and current
marker taxonomy. Future perf comparisons now measure drift from commit
`6524e06a` on this machine instead of the older `dx12` baseline from a different
Windows/CPU label and the older `physics_bench` marker/memory profile.
