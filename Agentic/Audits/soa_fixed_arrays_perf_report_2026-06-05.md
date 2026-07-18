# SoA Fixed-Array Performance Report - 2026-06-05

Scope: `codex/soa-1000-profile`, fixed-capacity SoA refresh cleanup for `GameModelCollection`.

## Change Under Test

- Replaced `GameModelCollection` SoA hot streams with fixed `std::array<..., MAX_SCENE_OBJECTS>` storage.
- Removed SoA `reserve`, `resize`, and `clear` calls from the hot stream lifecycle.
- Added profiler marker `Frame/SoA/RefreshBodyData` around the SoA body refresh copy.
- The refresh marker is counted inside normal frame totals. The reported wins below do not subtract refresh cost.

## Validation

Command:

```bat
tools\validate_full.bat
```

Log:

```text
TestOutput\validation_logs\soa_fixed_arrays_validate_full_20260605_131556.log
```

Result:

```text
Build succeeded.
    0 Warning(s)
    0 Error(s)
PASS: DX12 InfoQueue reported 0 validation errors.
PASS: Cross-renderer parity acceptable.
PASS: physics_regression_solver.csv (20001 lines, exact match)
VALIDATE_PHYSICS: ALL PASSED
VALIDATE_PERF: COMPLETE
VALIDATE_FULL: ALL PHASES PASSED
```

Perf baseline comparison was skipped by the validation script because the current runner reported a different machine string than the stored perf baselines.

## Standard Perf Suite

Artifacts:

- `Profile\gl_perf.json`
- `Profile\dx11_perf.json`
- `Profile\dx12_perf.json`

| Renderer | Frames | Frame Avg | Frame P50 | Frame P99 | Physics Avg | SoA Refresh Avg | Render Avg | PrepareModels Avg | Memory Start | Memory End |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GL | 1970 | 0.6405 ms | 0.6223 ms | 1.0625 ms | 0.3542 ms | 0.0038 ms | 0.1348 ms | 0.0172 ms | 70.35 MB | 74.10 MB |
| DX11 | 1970 | 0.5412 ms | 0.5114 ms | 0.9582 ms | 0.3709 ms | 0.0036 ms | 0.0579 ms | 0.0106 ms | 73.37 MB | 81.35 MB |
| DX12 | 1970 | 1.0145 ms | 0.9676 ms | 1.5608 ms | 0.3555 ms | 0.0033 ms | 0.3765 ms | 0.0120 ms | 97.14 MB | 223.12 MB |

The standard scene refresh cost is effectively noise-level: about `0.0033-0.0038 ms` per frame across renderers.

## 1000-Object Stress Scene

Scene:

```text
SkullbonezData\scenes\perf_1000.scene
```

Artifacts:

- Before SoA: `Profile\perf_1000_before.csv`
- Vector SoA pass: `Profile\perf_1000_after.csv`
- Fixed-array SoA pass: `Profile\perf_1000_fixed_arrays_report_20260605_133155.csv`

All 1000-object results below use pass 2 steady-state rows, 1000 frames.

| Marker | Before SoA Avg | Vector SoA Avg | Fixed-Array SoA Avg | Fixed vs Before | Fixed vs Vector |
|---|---:|---:|---:|---:|---:|
| Frame | 3.2440 ms | 3.0140 ms | 2.7780 ms | -14.4% | -7.8% |
| Frame/Physics | 2.7432 ms | 2.5368 ms | 2.3427 ms | -14.6% | -7.7% |
| Frame/Physics/ApplyForces | 0.1142 ms | 0.1079 ms | 0.0899 ms | -21.3% | -16.7% |
| Frame/Physics/Broadphase | 0.1034 ms | 0.0923 ms | 0.0748 ms | -27.7% | -19.0% |
| Frame/Physics/Narrowphase | 1.0397 ms | 0.9530 ms | 0.8948 ms | -13.9% | -6.1% |
| Frame/Physics/Terrain | 0.4072 ms | 0.3846 ms | 0.3498 ms | -14.1% | -9.0% |
| Frame/Physics/Narrowphase/PersistentContacts | 0.8774 ms | 0.8001 ms | 0.7511 ms | -14.4% | -6.1% |
| Frame/Physics/Integrate | 0.1987 ms | 0.1877 ms | 0.1741 ms | -12.4% | -7.2% |
| Frame/Render | 0.2982 ms | 0.2599 ms | 0.2379 ms | -20.2% | -8.5% |
| Frame/Render/PrepareModels | n/a | 0.0564 ms | 0.0422 ms | n/a | -25.2% |
| Frame/Render/Balls | 0.0665 ms | 0.0294 ms | 0.0282 ms | -57.6% | -4.1% |
| Frame/Render/Reflection/Balls | 0.0752 ms | 0.0363 ms | 0.0345 ms | -54.1% | -5.0% |
| Frame/Render/Shadows | 0.0689 ms | 0.0599 ms | 0.0572 ms | -17.0% | -4.5% |

## Refresh Marker Detail

Marker:

```text
Frame/SoA/RefreshBodyData
```

Fixed-array 1000-object result:

| Rows | Avg | P50 | P95 | P99 | Max |
|---:|---:|---:|---:|---:|---:|
| 1000 | 0.0112 ms | 0.0093 ms | 0.0244 ms | 0.0365 ms | 0.0556 ms |

This confirms the SoA refresh cost is explicitly measured and already included in `Frame`, `Frame/Physics`, and `Frame/Render/PrepareModels` totals.

## Interpretation

The fixed-array cleanup removes unnecessary dynamic container state from the SoA hot streams and avoids the confusing per-refresh `resize()` calls. The 1000-object pass shows a `14.4%` frame-time reduction versus the pre-SoA baseline, with the refresh copy itself averaging `0.0112 ms`.

The shape of the current implementation is correct but not ideal aesthetically: `GameModelCollection` now owns several parallel cache members and validity flags. The next cleanup should wrap the hot streams into a private fixed-capacity cache type, for example `GameModelSoACache`, with:

- fixed arrays for position, radius, shape flag, and model matrix
- active count
- body and matrix validity flags
- `Invalidate()`
- `RefreshBodyData(models)`
- `BuildModelMatrices(models)`

That keeps the low-risk cache architecture while making the collection header easier to read. A larger future refactor would make SoA the authoritative simulation storage and make `GameModel` a handle or facade, but that should wait until the solver/storage ownership is ready for a wider change.
