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
| `--seed` | positive integer | Override the RNG seed for every loaded scene, including legacy mode. Useful with nudge repro snapshots. |
| `--no-water` | flag | Start the fluid surface below the active terrain. Page Up can raise it during runtime. |
| `--all-balls` | flag | Force generated object populations to spawn as balls. |
| `--all-boxes` | flag | Force generated object populations to spawn as boxes and use the solver path for those objects. |
| `--physics-debug` | `none`, `axes`, `contacts`, `sleep`, `all`, `on`, `off` | Override physics debug overlay mode for every loaded scene. |
| `--physics-debug-axes` | optional `on`, `off` | Toggle object local axis debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-contacts` | optional `on`, `off` | Toggle contact manifold debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-sleep` | optional `on`, `off` | Toggle sleep/support/inhibition debug markers for every loaded scene. Bare flag means `on`. |
| `--physics-debug-transparent` | optional `on`, `off` | Toggle translucent debug collision volumes for every loaded scene. Bare flag means `on`. |
| `--physics-debug-alpha` | float | Override translucent debug body alpha, `0.05` to `1.0`; also enables translucent debug bodies. |
| `--physics-debug-contact-linger` | seconds | Keep contact manifold visuals visible after contact rows disappear, `0.0` to `5.0`. |
| `--physics-log` | path | Write per-frame physics CSV in Debug builds. |
| `--gen-atlas` | optional path | Generate the SDF font atlas and exit before GPU init. |

Physics debug command-line arguments also accept underscore spellings matching scene directives, for example `--physics_debug all` and `--physics_debug_contact_linger 0.75`.

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
| Rendering | `text`, `text_only`, `debug_vectors`, `physics_debug`, `physics_debug_axes`, `physics_debug_contacts`, `physics_debug_sleep`, `physics_debug_transparent`, `physics_debug_alpha`, `physics_debug_contact_linger`, `vsync`, `pipeline_sync`, `roll_align`, `water_hidden`, `terrain_hidden`, `flat_slope` |

For exact field order, inspect an existing scene in `SkullbonezData/scenes/` and the parser in `SkullbonezSource/SkullbonezTestScene.cpp`.

## Key Bindings

| Key | Action |
|-----|--------|
| Esc | Quit |
| F | Toggle fly mode. Freezes physics and camera auto-cycle. |
| N | Toggle nudge mode. Free camera with live simulation. |
| Enter | In nudge mode, write Debug-build repro data for the object under the crosshair to `Debug/nudge_repro_snapshots.txt`. |
| Q | Cycle render backend: GL to DX11 to DX12 to GL. |
| R | Reset or rerun the current scene, including legacy mode. |
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
| 6 | Toggle translucent debug bodies for the physics debug overlay. |
| Page Up / Page Down | Move the water surface up or down while held. |
| V | Toggle collision visualiser. |
| C | Cycle physics debug overlay: none, axes, contacts, sleep, all. |
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
| `SkullbonezData/scenes/standing_box_repro.scene` | Deterministic solver-box edge-rest repro seed target. |
| `SkullbonezData/scenes/box_crater_edge_repro.scene` | Terrain edge-rest regression scene with Debug physics log. |

## Debug Logging

Use `Log().Writef()` for debug-only diagnostic output:

```cpp
Log().Writef( "Debug/physics.csv", "frame,%d,x,%.3f,y,%.3f,z,%.3f\n", frame, x, y, z );
```

The log singleton lazily opens files and compiles out in Release/Profile where the implementation is guarded by `_DEBUG`.

Debug builds also support nudge-mode repro snapshots. Press `N`, centre an object in the crosshair, then press Enter. Each snapshot appends the scene, frame, active RNG seed, fixed-step mode, renderer, physics mode, camera pose, object transform, velocities, shape data, sleep/contact state, and terrain support probes to `Debug/nudge_repro_snapshots.txt`.
Snapshots also include scene load/reset counts and a `--seed` replay hint so an object found after repeated Q resets can be reproduced from a fresh process.
