# SkullbonezCore Runtime Reference

This file holds details that are useful during debugging or manual testing but too large for `README.md`.

## Command-Line Arguments

| Argument | Values | Description |
|----------|--------|-------------|
| `--renderer` | `gl`, `dx11`, `dx12` | Select render backend. Default is `gl`. |
| `--scene` | path | Load one scene file. Quoted paths are supported. |
| `--suite` | path | Load a `.suite` file with one scene path per line. |
| `--vsync` | `on`, `off` | Override vsync from `engine.cfg`. |
| `--legacy-physics` | flag | Start with the legacy swept-sphere solver. |
| `--switch-interval` | seconds | Cycle renderers at runtime. |
| `--time-scale` | float | Override simulation time multiplier. |
| `--fixed-step` | flag | Run one deterministic physics tick per rendered frame. |
| `--physics-log` | path | Write per-frame physics CSV in Debug builds. |
| `--gen-atlas` | optional path | Generate the SDF font atlas and exit before GPU init. |

## Scene Directives

Scene files are plain text. Blank lines and lines beginning with `#` are ignored.

| Area | Directives |
|------|------------|
| Playback | `frames`, `exit_on_complete`, `screenshot_and_exit`, `fixed_step` |
| Capture | `screenshot`, `screenshot_interval` |
| Logging | `perf_log`, `perf_log_flush`, `perf_log_flush_interval`, `physics_log` |
| Simulation | `physics`, `physics_mode`, `time_scale`, `seed`, `world` |
| Objects | `ball`, `box`, `ball_state`, `legacy_balls`, `solver_balls`, `solver_boxes` |
| Camera | `camera`, `track_height`, `auto_cycle_interval` |
| Rendering | `text`, `text_only`, `debug_vectors`, `vsync`, `pipeline_sync`, `roll_align`, `water_hidden`, `terrain_hidden`, `flat_slope` |

For exact field order, inspect an existing scene in `SkullbonezData/scenes/` and the parser in `SkullbonezSource/SkullbonezTestScene.cpp`.

## Key Bindings

| Key | Action |
|-----|--------|
| Esc | Quit |
| F | Toggle fly mode. Freezes physics and camera auto-cycle. |
| N | Toggle nudge mode. Free camera with live simulation. |
| Enter | In nudge mode, write Debug-build repro data for the object under the crosshair to `Debug/nudge_repro_snapshots.txt`. |
| R | Cycle render backend: GL to DX11 to DX12 to GL. |
| P | Toggle physics solver: impulse or legacy. |
| Z | Fire a ball from the camera. Shift increases speed. |
| X | Fire a box from the camera in impulse mode. |
| F2 | Save a scene snapshot. |
| F3 | Save a screenshot. |
| 0 | Toggle profiler overlay. |
| 1 | Freeze or unfreeze water animation. |
| 2 | Toggle water reflection pass. |
| 3 | Toggle ocean wave displacement. |
| 4 | Toggle terrain visibility. |
| 5 | Toggle water visibility. |
| V | Toggle collision visualiser. |
| 9 | Toggle velocity vectors. |
| G | Cycle tracked ball in scene mode. |

Fly and nudge mode use WASD, mouse look, Shift for faster movement, and Space to step physics while fly mode is paused.

## Test Scenes

| Scene | Purpose |
|-------|---------|
| `SkullbonezData/scenes/water_ball_test.scene` | Visual regression for terrain, skybox, sphere, water, and shadow. |
| `SkullbonezData/scenes/legacy_smoke.scene` | Smoke test with 300 balls. |
| `SkullbonezData/scenes/perf_test.scene` | Tri-renderer performance regression scene. |
| `SkullbonezData/scenes/physics_roll.scene` | Physics rolling validation. |
| `SkullbonezData/scenes/physics_regression_solver.scene` | Byte-exact Debug physics CSV regression. |

## Debug Logging

Use `Log().Writef()` for debug-only diagnostic output:

```cpp
Log().Writef( "Debug/physics.csv", "frame,%d,x,%.3f,y,%.3f,z,%.3f\n", frame, x, y, z );
```

The log singleton lazily opens files and compiles out in Release/Profile where the implementation is guarded by `_DEBUG`.

Debug builds also support nudge-mode repro snapshots. Press `N`, centre an object in the crosshair, then press Enter. Each snapshot appends the scene, frame, active RNG seed, fixed-step mode, renderer, physics mode, camera pose, object transform, velocities, shape data, sleep/contact state, and terrain support probes to `Debug/nudge_repro_snapshots.txt`.
