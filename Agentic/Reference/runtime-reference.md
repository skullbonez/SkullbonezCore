# SkullbonezCore Runtime Reference

This file holds details that are useful during debugging or manual testing but too large for `README.md`.

## Command-Line Arguments

| Argument | Values | Description |
|----------|--------|-------------|
| `--renderer` | `gl`, `dx11`, `dx12` | Select render backend. Default is `gl`. |
| `--scene` | path | Load one scene file. Quoted paths are supported. |
| `--suite` | path | Load a `.suite` file with one scene path per line. |
| `--scene-load-only` | flag | Load queued scene files and exit before the frame loop. Alias: `--load-scenes-only`. Used by `tools\validate_scene_loads.bat`. |
| `--vsync` | `on`, `off` | Override vsync from `engine.cfg`. |
| `--dump-config` | flag | Print the resolved startup config after `engine.cfg` and command-line overrides. |
| `--switch-interval` | seconds | Cycle renderers at runtime. |
| `--time-scale` | float | Override simulation time multiplier. |
| `--fixed-step` | flag | Run one deterministic physics tick per rendered frame. |
| `--seed` | positive integer | Override the RNG seed for every loaded scene, including generated demo mode. Useful with nudge repro snapshots. |
| `--no-water` | flag | Start the fluid surface below the active terrain. Page Up can raise it during runtime. |
| `--no-sleep` | flag | Keep movable physics bodies awake for solver diagnostics and sleep/no-sleep performance comparisons. |
| `--cinematic` | optional `on`, `off` | Force cinematic HDR/post rendering on or off for every loaded scene. Bare flag means `on`. Alias: `--cinematic-rendering`. |
| `--interactive` | optional `on`, `off` | Keep scene automation from quitting the app so a screenshot/validation scene can be inspected live. Bare flag means `on`. Alias: `--hold`. |
| `--profiler` | flag | Start with the timer/profiler HUD visible. Alias: `--show-profiler`. |
| `--hide-top-text` | flag | Hide the always-on top HUD rows while leaving profiler/key overlays available. Alias: `--no-top-text`. |
| `--broadphase-visualizer` | flag | Start with the broadphase spatial grid visualizer enabled. Alias: `--broadphase-overlay`. |
| `--all-balls` | flag | Force generated object populations to spawn as balls. |
| `--all-boxes` | flag | Force generated object populations to spawn as boxes and use the solver path for those objects. |
| `--physics-debug` | `none`, `axes`, `contacts`, `sleep`, `pipeline`, `all`, `on`, `off` | Override physics debug overlay mode for every loaded scene. |
| `--physics-debug-axes` | optional `on`, `off` | Toggle object local axis debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-contacts` | optional `on`, `off` | Toggle contact manifold debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-sleep` | optional `on`, `off` | Toggle sleep/support/inhibition debug markers for every loaded scene. Bare flag means `on`. |
| `--physics-debug-pipeline` | optional `on`, `off` | Toggle the bounded Catto pipeline stage overlay for every loaded scene. Bare flag means `on`. |
| `--physics-debug-transparent` | optional `on`, `off` | Toggle translucent debug collision volumes for every loaded scene. Bare flag means `on`. |
| `--physics-debug-alpha` | float | Override translucent debug body alpha, `0.05` to `1.0`; also enables translucent debug bodies. |
| `--physics-debug-contact-linger` | seconds | Keep contact manifold visuals visible after contact rows disappear, `0.0` to `5.0`. |
| `--physics-regression-log` | path | Write the byte-exact physics regression CSV in Debug builds. |
| `--physics-collision-time-log` | path | Write a Debug-only swept collision event CSV with collision times for focused regression scenes. |
| `--physics-diag` | path | Write queryable physics diagnostics NDJSON in Debug builds. Forces fixed-step playback and can be queried with `tools\physics_query.bat`. Alias: `--physics-diagnostics`. |
| `--gen-atlas` | optional path | Generate the SDF font atlas and exit before GPU init. |

Physics debug command-line arguments also accept underscore spellings matching scene directives, for example `--physics_debug all` and `--physics_debug_contact_linger 0.75`.

## Cinematic Rendering

Launch the authored cinematic look-dev scene with:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --scene SkullbonezData\scenes\cinematic_volumetric.scene --cinematic --hold
```

The in-game UI has a `Cine` tab with feature toggles and sliders. Feature toggles are backed by `engine.cfg` keys:

| Key | Description |
|-----|-------------|
| `cinematic_sky_atmosphere` | Use the procedural sunset sky instead of the legacy skybox while cinematic mode is active. |
| `cinematic_clouds` | Enable procedural cloud silhouettes and their ray-occlusion mask. |
| `cinematic_god_rays` | Enable screen-space sun shafts and radial god rays. |
| `cinematic_volumetric_lighting` | Enable the half-resolution volumetric light accumulation pass. |
| `cinematic_bloom` | Enable tonemap-stage bloom sampling. |
| `cinematic_fog` | Enable depth fog and basin haze in the tonemap pass. |
| `cinematic_terrain_relief_enabled` | Enable render-only cinematic basin relief on terrain. Physics terrain is unchanged. |

Scene files may override any `cinematic_*` key with the same spelling and a space-separated value, for example:

```text
cinematic_rendering on
cinematic_clouds off
cinematic_bloom_strength 0.30
cinematic_exposure 0.85
```

Scene overrides are merged into a per-run active cinematic config. They do not write back to `engine.cfg`, and `--cinematic on/off` remains the top-level command-line override for the rendering stack.

## Scene Directives

Scene files are plain text. Blank lines and lines beginning with `#` are ignored.

| Area | Directives |
|------|------------|
| Playback | `frames`, `exit_on_complete`, `screenshot_and_exit`, `fixed_step` |
| Capture | `screenshot`, `screenshot_interval` |
| Logging | `perf_log`, `perf_log_flush`, `perf_log_flush_interval` |
| Simulation | `physics`, `time_scale`, `seed`, `world` |
| Objects | `ball`, `box`, `floating_box`, `ball_state`, `solver_balls`, `solver_boxes` |
| Camera | `camera`, `track_height`, `auto_cycle_interval` |
| Rendering | `text`, `text_only`, `physics_debug`, `physics_debug_axes`, `physics_debug_contacts`, `physics_debug_sleep`, `physics_debug_pipeline`, `physics_debug_transparent`, `physics_debug_alpha`, `physics_debug_contact_linger`, `vsync`, `pipeline_sync`, `water_hidden`, `terrain_hidden`, `flat_slope` |

For exact field order, inspect an existing scene in `SkullbonezData/scenes/` and the parser in `SkullbonezSource/SkullbonezTestScene.cpp`.
`floating_box` uses the same fields as `box`, but the body is fixed in world space and excluded from gravity, impulses, and scene energy.

Physics regression CSV output is command-line only via `--physics-regression-log` and `--physics-collision-time-log`; scene files must not enable it.

## Key Bindings

| Key | Action |
|-----|--------|
| Esc | Quit |
| F | Toggle fly mode. Freezes physics and camera auto-cycle. |
| N | Toggle nudge mode. Free camera with live simulation. |
| Left Click | In nudge mode, fire a pooled high-speed silver bullet from the camera. Shift increases speed. |
| Enter | In nudge mode, write Debug-build repro data for the object under the crosshair to `Debug/nudge_repro_snapshots.txt`. |
| Q | Cycle render backend: GL to DX11 to DX12 to GL. |
| R | Reset or rerun the current scene/generated demo while preserving live controls. |
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
| F7 / F8 | Step the physics pipeline debug overlay to the previous or next Catto stage. |
| G | Toggle broadphase visualizer, or cycle the tracked ball when ball tracking is active and the visualizer is off. |

Fly and nudge mode use WASD, mouse look, Shift for faster movement, and Space to step physics while fly mode is paused.

## Test Scenes

| Scene | Purpose |
|-------|---------|
| `SkullbonezData/scenes/water_ball_test.scene` | Visual regression for terrain, skybox, sphere, water, and shadow. |
| `SkullbonezData/scenes/solver_smoke.scene` | Smoke test with 300 generated balls. |
| `SkullbonezData/scenes/perf_test.scene` | Tri-renderer performance regression scene. |
| `SkullbonezData/scenes/physics_roll.scene` | Physics rolling validation. |
| `SkullbonezData/scenes/cause_effect_marble_run.scene` | Fixed floating ramp, scene-energy telemetry, and cube-tower cause/effect demo. |
| `SkullbonezData/scenes/physics_regression_solver.scene` | Byte-exact Debug physics CSV regression. |
| `SkullbonezData/scenes/bullet_sweep_wall.scene` | High-speed bullet into a fixed wall block; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/bullet_sweep_object.scene` | High-speed bullet into a fixed object corner; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/bullet_sweep_terrain.scene` | High-speed bullet into flat terrain; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/shooting_reaction_volley.scene` | Ten camera-style bullets fired into mixed dynamic targets; validation asserts every target reacts. |
| `SkullbonezData/scenes/standing_box_repro.scene` | Deterministic solver-box edge-rest repro seed target. |
| `SkullbonezData/scenes/box_crater_edge_repro.scene` | Terrain edge-rest regression scene with Debug physics regression log. |

## Debug Logging

Use `Log().Writef()` for debug-only diagnostic output:

```cpp
Log().Writef( "Debug/physics.csv", "frame,%d,x,%.3f,y,%.3f,z,%.3f\n", frame, x, y, z );
Log().WriteEventf( "renderer_changed from=%s to=%s", oldName, newName );
```

The log singleton lazily opens files and compiles out in Release/Profile where the implementation is guarded by `_DEBUG`.

Runtime lifecycle events go to `Debug/runtime_events.log` in Debug builds. The engine records process start, scene start, scene finish, renderer changes, fatal exceptions, and unhandled crash stack traces in that file.
Use `Debug\SKULLBONEZ_CORE.exe --debug-crash-test` to intentionally exercise the crash stack logger.

Debug builds also support nudge-mode repro snapshots. Press `N`, centre an object in the crosshair, then press Enter. Each snapshot appends the scene, frame, active RNG seed, fixed-step mode, renderer, camera pose, object transform, velocities, shape data, sleep/contact state, and terrain support probes to `Debug/nudge_repro_snapshots.txt`.
Snapshots also include scene load/reset counts and a `--seed` replay hint so an object found after repeated Q resets can be reproduced from a fresh process.
