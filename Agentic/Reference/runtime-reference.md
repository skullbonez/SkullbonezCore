# SkullbonezCore Runtime Reference

This file holds details that are useful during debugging or manual testing but too large for `README.md`.

## Scene Object Identity

`PhysicsSceneObjectId` is the stable identity shared across scene save/load,
undo, picking, logging, replay correlation, and other cross-system operations.
It survives dense-store compaction and must not be replaced by a generic
`EntityId`. Once an owner resolves that id, physics and render hot paths use
their typed handles or dense rows locally; those row indices are temporary
hints, not persisted identity.

## Command-Line Arguments

| Argument | Values | Description |
|----------|--------|-------------|
| `--renderer` | `dx12` | Compatibility alias for the only runtime renderer. Omit it for normal launches. GL and DX11 are retired runtime choices. |
| `--scene` | path | Load one `.scene.json` file. Quoted paths are supported, and bare names resolve through `SkullbonezData\scenes\<name>.scene.json`. |
| `--suite` | path | Load one `.suite.json` file with a JSON `scenes` array. |
| `--demohero` | flag | Run generated demo mode with the low-poly hero rendering/style stack applied. Alias: `--demo-hero`. |
| `--scene-load-only` | flag | Load queued scene files and exit before the frame loop. Alias: `--load-scenes-only`. Used by `tools\validate_scene_loads.bat`. |
| `--scene-snapshot-out` | path | With exactly one loaded scene, load it, serialize the runtime state to the given `.scene.json` path, and exit before the frame loop. Alias: `--scene_snapshot_out`. |
| `--vsync` | `on`, `off` | Override vsync from `engine.cfg`. |
| `--dump-config` | flag | Print the resolved startup config after `engine.cfg` and command-line overrides. |
| `--switch-interval` | retired | Rejected because DX12 is the only runtime renderer. |
| `--time-scale` | float | Override simulation time multiplier. |
| `--fixed-step` | flag | Run one deterministic physics tick per rendered frame. |
| `--seed` | positive integer | Override the RNG seed for every loaded scene, including generated demo mode. Useful with launcher repro snapshots. |
| `--replay` | optional `on`, `off` | Control bounded in-memory replay presentation capture. Generated/interactive runs enable the 30-second buffer by default; scene/suite automation opts in with `--replay on`, `--replay-seconds`, or `--replay-hashes`. |
| `--replay-seconds` | `1..600` | Retention window for replay capture; default is 30 seconds at 120 Hz. Alias: `--replay_seconds`. |
| `--replay-hashes` | path | Enable replay capture and write a CSV of replay frame hashes for fixed-step comparison runs. Alias: `--replay_hashes`. |
| `--replay-scrub-test` | Debug flag | Enable the CLI-only replay scrub SkullScope probe, select an older retained sample, emit one `replay_scrub` row, and exit after the probe passes. |
| `--replay-restore-test` | Debug flag | Enable the CLI-only retained solver restore SkullScope probe, restore an older retained solver frame, emit one `replay_restore` row, and exit after the hash-verified restore passes. |
| `--replay-save-probe` | Debug path | Enable the CLI-only replay v2 save probe, write a binary presentation `.skreplay` artifact after enough retained samples exist, and exit. Alias: `--replay_save_probe`. |
| `--replay-load` | path | Load a binary v2 presentation `.skreplay` artifact after scene initialization and arm the scrubber on the file-backed presentation row. Alias: `--replay_load`. |
| `--replay-load-probe` | Debug path | Load a v2 presentation `.skreplay` artifact, prove the runtime scrub source can select/apply/restore an older loaded sample, and exit before the frame loop. Alias: `--replay_load_probe`. |
| `--replay-restore-file-probe` | Debug path | Load a v2 `.skreplay` solver checkpoint chunk, restore the first saved checkpoint through the live solver restore path, require the recomputed solver hash to match, and exit before the frame loop. Alias: `--replay_restore_file_probe`. |
| `--replay-restore-target-file-probe` | Debug path | Load a v2 `.skreplay` artifact, restore the nearest sparse checkpoint, replay saved typed events/fixed ticks to a non-checkpoint target, require the target solver hash to match, and exit before the frame loop. Alias: `--replay_restore_target_file_probe`. |
| `--replay-restore-branch-file-probe` | Debug path | Exercise the loaded-file branch path from a saved v2 target, require hash-verified restore plus child branch creation, and exit before the frame loop. Alias: `--replay_restore_branch_file_probe`. |
| `--replay-restore-failure-file-probe` | Debug path | Require `--physics-diag`, request an intentionally missing saved v2 target, emit a failed `replay_restore` SkullScope row with a reason, query it in validation, and exit before the frame loop. Alias: `--replay_restore_failure_file_probe`. |
| `--no-water` | flag | Start the fluid surface below the active terrain. Page Up can raise it during runtime. |
| `--no-sleep` | flag | Keep movable physics bodies awake for solver diagnostics and sleep/no-sleep performance comparisons. |
| `--tornado` | optional `on`, `off` | Start with the generated-demo tornado force field enabled or disabled. Bare flag means `on`; the Physics tab can still toggle it live. |
| `--tornado-vectors` | optional `on`, `off` | Start with tornado velocity-field vectors visible. Green vectors are slower; red vectors are faster. Alias: `--tornado-vector-field`. |
| `--cinematic` | optional `on`, `off` | Force cinematic HDR/post rendering on or off for every loaded scene. Bare flag means `on`. Alias: `--cinematic-rendering`. |
| `--shadows` | optional `on`, `off` | Force directional shadow maps on or off for every loaded scene. Bare flag means `on`; shadows work in normal and cinematic rendering. Aliases: `--shadow-maps`, `--cinematic-shadows`, `--cinematic_shadows`. |
| `--interactive` | optional `on`, `off` | Keep scene automation from quitting the app so a screenshot/validation scene can be inspected live. Bare flag means `on`. Alias: `--hold`. |
| `--live-style-control` | directory | Watch `<directory>\live.style.json` and `<directory>\capture.txt` while the scene keeps running. Applies style-only JSON descriptors without reloading physics and saves requested screenshots after the current frame is drawn. Aliases: `--style-harness`, `--live_style_control`, `--style_harness`. |
| `--profiler` | flag | Start with the timer/profiler HUD visible. Alias: `--show-profiler`. |
| `--platform-profiler-markers` | flag | Emit existing profiler markers to the platform profiler marker API when support is available. Enabled by default in Debug and Profile builds while PIX marker support is compiled in. Aliases: `--platform-profiler`, `--pix-markers`, `--pix`. Environment fallback/override: `SKULLBONEZ_PLATFORM_PROFILER_MARKERS=1`; set it to `0` to disable the default. `SKULLBONEZ_PIX_MARKERS=1` is still accepted as a Windows PIX compatibility alias. |
| `--memory-dump` | path | Write a main-memory JSON snapshot at graceful shutdown. The dump includes process memory, replay buckets, game-object buckets, and reconciliation fields. Alias: `--memory_dump`. |
| `--graphics-stress` | optional `on`, `off` | Enable the deterministic DX12 graphics stress runner. It churns scene loads, cinematic settings, sky/fog/ray controls, runtime render toggles, debug overlays, generated object counts, and UI state from one replayable seed. Alias: `--graphics_stress`. |
| `--graphics-stress-seed` | positive integer | Seed for the deterministic graphics stress random stream. Alias: `--graphics_stress_seed`. |
| `--graphics-stress-actions` | `1..64` | Number of graphics stress mutations to apply per rendered frame. Alias: `--graphics_stress_actions`. |
| `--graphics-stress-scene-interval` | `1..600` | Minimum graphics stress frames between forced scene reloads. Alias: `--graphics_stress_scene_interval`. |
| `--graphics-stress-memory-interval` | `0..36000` | Emit `[graphics-stress-memory]` stdout records every N graphics stress frames; `0` disables engine-side memory records. The records include process memory, engine buckets, DXGI local/non-local usage, descriptor pressure, upload arena pressure, and cache/pool counts. Alias: `--graphics_stress_memory_interval`. |
| `--hide-top-text` | flag | Hide the always-on top HUD rows while leaving profiler/key overlays available. Alias: `--no-top-text`. |
| `--broadphase-visualizer` | flag | Start with the broadphase spatial grid visualizer enabled. Alias: `--broadphase-overlay`. |
| `--all-balls` | flag | Force generated object populations to spawn as balls. |
| `--all-boxes` | flag | Force generated object populations to spawn as boxes and use the solver path for those objects. |
| `--physics-debug` | `none`, `axes`, `contacts`, `sleep`, `pipeline`, `terrain`, `all`, `on`, `off` | Override physics debug overlay mode for every loaded scene. |
| `--physics-debug-axes` | optional `on`, `off` | Toggle object local axis debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-contacts` | optional `on`, `off` | Toggle contact manifold debug lines for every loaded scene. Bare flag means `on`. |
| `--physics-debug-sleep` | optional `on`, `off` | Toggle sleep/support/inhibition debug markers for every loaded scene. Bare flag means `on`. |
| `--physics-debug-pipeline` | optional `on`, `off` | Toggle the bounded Catto pipeline stage overlay for every loaded scene. Bare flag means `on`. |
| `--physics-debug-terrain-contact` | optional `on`, `off` | Toggle the terrain contact probe overlay for every loaded scene. Bare flag means `on`. |
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
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\cinematic_volumetric.scene.json --cinematic --hold
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
| `cinematic_shadows` | Enable directional shadow maps. Scene files can also use `shadows on|off`; shadows work in normal and cinematic rendering. |

Scene files may override cinematic settings in the JSON `cinematic` object, for example:

```json
{
  "debug": { "shadows": true },
  "cinematic": {
    "clouds": false,
    "bloomStrength": 0.30,
    "exposure": 0.85
  }
}
```

Scene overrides are merged into a per-run active cinematic config. They do not write back to `engine.cfg`, and `--cinematic on/off` remains the top-level command-line override for the HDR/post rendering stack. Shadow-map controls use the same config object but are independent of the cinematic master switch.

## Interactive Hero Scene

Use this to boot the low-poly hero scene with physics running, unlimited frames, and the balls/cubes bouncing:

```bat
Profile\SKULLBONEZ_CORE.exe --hero
Profile\SKULLBONEZ_CORE.exe --scene hero
```

The dedicated `--hero` flag and the `--scene` aliases `hero`, `low_poly_hero`, and `low-poly-hero` resolve to `SkullbonezData\scenes\concept_12_low_poly_art_style.scene.json`. The hero scene is a live scene with physics on, `fixed_step`, and unlimited frames, so it keeps running until the window is closed. Bare scene names also resolve through `SkullbonezData\scenes\`, so `--scene stacking` loads `SkullbonezData\scenes\stacking.scene.json` when it exists.

Use this to keep the generated demo scene and physics population, but render it with the same low-poly hero style:

```bat
Profile\SKULLBONEZ_CORE.exe --demohero
```

`--demohero` is mutually exclusive with `--hero`, `--scene`, and `--suite`. It only applies `SkullbonezData\styles\low_poly_art_style.style.json` after generated demo objects are created, so it does not import the hero scene's camera, world settings, fixed set dressing, or object list.

## Live Style Harness

The live style harness is for look-dev: keep the game window running, edit a `.style.json` descriptor, then request screenshots without restarting the scene or resetting physics.

```bat
tools\style_harness.bat init -Style low_poly_art_style
tools\style_harness.bat launch -Renderer dx12 -Scene hero
tools\style_harness.bat setshot -Key cinematic_exposure -Value 0.90 -Name exposure_090
tools\style_harness.bat setshot -Key cinematic_style_grade -Value "1.35 1.10 0.22" -Name punchy_grade
tools\style_harness.bat status
```

The watched folder defaults to `Agentic\style-harness\` and contains:

| File | Purpose |
|------|---------|
| `live.style.json` | The active style descriptor. It may contain `includes`, `cinematic`, and `objectMaterials` JSON fields. |
| `capture.txt` | A one-line request such as `capture "C:\SkullbonezCore\Agentic\style-harness\shots\shot.bmp"`. Relative paths are resolved under the harness folder. |
| `status.txt` | Last app-side status, including whether the style was applied or a screenshot was saved. |

`--live-style-control` automatically enters interactive hold mode. The app only parses/apply styles when `live.style.json` changes, then captures after render/UI on the next requested frame. It does not rebuild objects, reload cameras, restart frame counters, change simulation time scale, or apply scene fields such as `terrain`, `objects`, or `simulation.timeScale`.

## Demo Director Shot Lists

Demo Director plays ordered `.shot.json` phase lists for reproducible camera/style takes. `SkullbonezData\shots\butterfly.shot.json` is the authored butterfly-effect take for `prediction_ragdoll_wall_200.scene.json`.

Each shot list uses:

```json
{
  "format": "skullbonez.shot.json",
  "version": 1,
  "loop": false,
  "phases": [
    {
      "name": "phase-name",
      "camera": {
        "position": [0.0, 0.0, 0.0],
        "view": [0.0, 0.0, -1.0],
        "up": [0.0, 1.0, 0.0]
      },
      "stylePath": "SkullbonezData/styles/storm_front.style.json",
      "advance": "RevealAtLeast",
      "timerSeconds": 1.0,
      "revealThreshold": 0.25,
      "blendInSeconds": 0.4,
      "revealRate": 0.5
    }
  ]
}
```

`advance` accepts `Manual`, `Timer`, or `RevealAtLeast`. `Timer` advances when the active phase elapsed time reaches `timerSeconds`. `RevealAtLeast` advances when the replay prediction reveal cursor reaches `revealThreshold` as normalized revealed frame over last prediction frame; it waits for a real prediction frame range before advancing. `revealRate` controls prediction seconds revealed per real second while the phase is active.

Director authoring keys:

| Key | Action |
|-----|--------|
| B | Grab/release the Director camera. Grab starts from the current Director pose and keeps the camera in Director mode. |
| J | Capture the current camera pose into the active phase. Works in Director or fly-style camera modes when a shot list is loaded. |
| K | Select the next phase for authoring. Non-looping shot lists wrap for authoring selection only. |
| L | Save the loaded shot list back to its source path. |

Interaction automation supports Director takes with `loadShotList`, `directorPlay`, `directorAdvance`, `directorGrab`, `directorRelease`, `setPhaseStyle`, `setCameraPose`, and `screenshot`. Assertions include `directorGrabbed`, `directorPhaseIndex`, `directorPhaseName`, and `directorPhaseStylePath`, so scripted screenshots can be pinned to exact phases.

## Replay Capture And Scrub

Replay capture keeps the last 30 seconds of presentation and solver samples in memory by default for generated and interactive runs. Scene/suite automation leaves replay off unless the command line opts in with `--replay on`, `--replay-seconds`, or `--replay-hashes`. With the in-game UI minimized and editor mode off, move the mouse near the bottom edge to reveal the scrubber. Click-hold or drag a row thumb left to inspect earlier retained frames; physics pauses while a historical frame is selected. The active row is the only row whose thumb moves while dragging, and the opposite row is muted at its own stored position. Drag the active thumb back to the live end to resume simulation. Entering scrub inspection or pressing the scrubber's `PAUSE` button copies the current render camera once into the internal `CAMERA_FREE` camera, then leaves the camera completely user-controlled; retained replay camera poses are not applied during inspection preview. While paused, `PLAY` resumes the previous fly/launcher mode, and Space steps physics without clearing the current prediction drawing.

The top row is the presentation track: it previews camera/body presentation samples for inspection only. A loaded binary v2 artifact replaces that row with a `V2 FILE` source that scrubs the saved presentation track from start to end. The lower `SOLVER` row records body state plus the hidden sleep, contact-cache, persistent-contact, tornado, and launcher visual state needed to restore a retained fixed tick. While paused on a retained solver frame or loaded v2 file frame, press `Enter` or click `BRANCH` to make that target the new live branch. Retained solver branching restores directly from the in-memory solver snapshot. V2 file branching restores the nearest sparse checkpoint, replays typed events and fixed ticks forward, compares the saved target solver hash, then assigns a child branch id only after the hash matches.

Left-click a world object outside editor/launcher/UI ownership to select it as the replay path target. Shift+Left Click adds another selected history target and promotes it as the prediction root; a normal click replaces the selected set. In launcher mode, use `Ctrl+Left Click` to select instead of firing, with Shift still additive. Selected root bodies get past/future path overlays built from retained solver samples. Red fades to white from the oldest retained root sample to the current solver scrub frame; white fades to green from the scrub frame to the latest retained frame. Future contacts involving a root add child bodies to that root's trace; child bodies now draw amber incoming paths and amber target rings from the current frame up to the first collision, then continue as grey post-contact paths with grey contact markers. While a replay or prediction chain is selected, objects outside the active chain draw at 50% alpha through the normal lit shader path so the causal bodies remain visually dominant. A right-side `CAUSE TREE` panel lists the active root and child bodies as an indented hierarchy; clicking any row pauses into inspection mode when needed and points the camera at that object. At the live edge there is no retained future, so scrub the solver row backward to inspect the recorded future. Miss-clicking without Shift clears the selected traces.

The bottom scrubber also has a top-row `PREDICT` checkbox with `-`, `+`, and a 1-10 second horizon slider below it. Prediction defaults to 10 seconds but remains off until enabled. When enabled with a selected replay path target, the runtime seeds a replay-owned private `PhysicsEngine` from the current live topology, body rows, solver snapshot, runtime config, and world forces, then advances that private engine as an amortized drawing job capped at 5 ms of speculative work per frame. Live physics stores are not written while prediction builds; the private engine is retained under the `replay_prediction_working_set` reserve owner so rebuilds reuse capacity when the scene size has not grown. Partial samples draw as soon as they are captured, so long horizons fill in over several frames instead of blocking the demo scene. Changing the selected target, horizon, or edited velocity cancels the in-flight job, clears the partial paths, and starts a fresh prediction. Space-stepping while the scrubber pause button is active does not trigger the automatic source-refresh path, so existing prediction lines remain visible while the live simulation follows them. The predicted root future draws white-to-green; bodies touched by that future draw amber incoming paths and target rings before their first predicted collision, then grey post-contact paths and contact markers after activation. The cause tree switches to the predicted chain when prediction rows are available. Prediction exposes profiler/PIX markers under `Frame/Replay/Prediction/BeginJob`, `Frame/Replay/Prediction/SeedPrivateEngine`, `Frame/Replay/Prediction/Slice`, `Frame/Replay/Prediction/Steps`, `Frame/Replay/Prediction/StepPhysics`, `Frame/Replay/Prediction/CaptureJobState`, `Frame/Replay/Prediction/CaptureBodyState`, `Frame/Replay/Prediction/CaptureSample`, `Frame/Replay/Prediction/DrawRoot`, `Frame/Replay/Prediction/BuildTree`, `Frame/Replay/Prediction/DrawChildren`, `Frame/Replay/Prediction/DrawAffectedBodies`, `Frame/Replay/PathVisualizer/...`, `Frame/Replay/CauseTree/...`, `Frame/Replay/FocusMask`, `Frame/Replay/InspectionCamera`, `Frame/Replay/ScrubberInput`, `Frame/Replay/SimulationPause`, and `Frame/Replay/ScrubberOverlay`. When VSync is off, the scrubber draws a red `WARNING: VSYNC OFF` label because free-running frames make scrub timing harder to reason about.

Press `Alt` outside editor mode, or click the scrubber's `ALT VEL` toggle, to enter replay velocity edit. The mode pauses live simulation, keeps the camera in free-cam inspection, enables prediction, and draws a selected dynamic body's velocity gizmo. Drag a colored linear axis handle to increase or reverse that body's velocity component; drag a colored angular ring to change angular velocity around that axis. Ring radius and color heat rise with spin magnitude. Every edit wakes the body when needed, invalidates retained physics streams, cancels any partial prediction, and starts redrawing the future path/cause chain from the new live velocity state. `SkullbonezData/scenes/replay_velocity_four_ball.scene.json` is the manual test scene for this workflow: four resting balls in a line so the first ball can be selected, accelerated, spun, and watched as predicted downstream contacts appear or disappear.

The scrubber's inline save-icon button writes the live replay buffer to binary v2 artifacts named `replays\replay_v2_####.skreplay`, incrementing like `Scenes\snapshot_####.scene.json` and `Screenshots\screenshot_####.bmp`. The adjacent `LOAD` button opens a `.skreplay` picker, loads a binary v2 artifact, and arms the file-backed `V2 FILE` scrub row for smooth backwards scrubbing. Binary v2 is the sole supported saved replay format.

Use hash logging when a fixed-step scene needs cheap frame hashes:

```bat
Profile\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\physics_regression_solver.scene.json --fixed-step --frames 240 --replay on --replay-hashes TestOutput\replay_hashes.csv
```

The recorder stores the configured recent window in memory and writes hash rows only when `--replay-hashes` is supplied. Samples are captured after committed physics ticks and include scene-local replay body IDs, transforms, velocities, sleep/contact flags, camera pose, world presentation state, and a 64-bit presentation hash. Solver samples also hash the authoritative snapshot payload. During scrub preview, runtime rendering temporarily applies the selected sample's body state, hides live bodies that did not exist in that sample, applies solver-sample launcher visuals for the draw, and restores live render state afterward. The retained camera pose remains part of the sample/export payload but is no longer applied to the render camera during replay-panel inspection.

Debug builds include a focused SkullScope replay scrub probe:

```bat
tools\validate_replay_scrub.bat
```

The scrub gate builds Debug, launches `physics_roll.scene.json` with `--replay-scrub-test --physics-diag Debug\replay_scrub.physicsdiag.ndjson`, then runs `tools\physics_query.bat Debug\replay_scrub.physicsdiag.ndjson replay --limit 8`. It also launches the same scene with `--replay-restore-test --physics-diag Debug\replay_restore.physicsdiag.ndjson`, then runs `tools\physics_query.bat Debug\replay_restore.physicsdiag.ndjson restore --limit 8`. The scrub query passes only when the selected replay sample is older than the live edge, the selected/live hashes differ, the chosen body moved, and the logged replay positions match the corresponding SkullScope body rows. The restore query passes only when applying the retained solver frame recomputes the original solver hash, body counts match, and no fallback restore was needed.

The v2 artifact gate builds Debug, launches `replay_v2_solver_one.scene.json` with `--replay-save-probe TestOutput\validation\replay_v2\replay_save_probe.skreplay --physics-diag TestOutput\validation\replay_v2\replay_save_probe_runtime.physicsdiag.ndjson`, then reloads the saved file through the C++ v2 reader and proves an older loaded pose can be applied/restored. The save probe performs a real queued scene reset, saves the restarted timeline, and requires the decoded `ownerAction` event row to name `SceneReset`; saved restore treats that reset as a checkpoint precondition. It also launches `--replay-load-probe` to prove the file-backed presentation row, `--replay-restore-file-probe` to hash-verify a saved checkpoint frame, `--replay-restore-target-file-probe` to restore a non-checkpoint target through checkpoint-plus-event replay, `--replay-restore-branch-file-probe` to drive the paused scrubber branch path from that saved artifact, and `--replay-restore-failure-file-probe` with `--physics-diag` to prove expected saved-file restore failures become queryable `replay_restore` rows. The checker also mutates a copied artifact so an unsupported replay-event kind lands inside the checkpoint-to-target replay window, then requires the runtime to reject it with `unsupported replay event kind`; scene transitions are checkpoint barriers, not in-window fixed-tick events. A second save probe writes `replay_generated_topology_probe.skreplay` from `replay_v2_generated_topology.scene.json`, then restores it from the mismatched one-body scene and requires `generated_topology_rebuilt=1`, proving the saved generated-scene config event can rebuild deterministic topology before the checkpoint is applied. Finally, it uses `tools\replay_query.bat` to query `summary`, `frame`, `body`, `branches`, `events`, `event-cursors`, `hashes`, `checkpoints`, and `export-skullscope`, and requires decoded owner-action, generated-scene-config, world-override, editor-place, editor-transform, launcher-config, and launcher-fire payloads. The editor-transform proof covers committed translate, rotate, and scale flags; scale stores an axis plus factor against the checkpoint shape. The exported bounded NDJSON slice is imported with `tools\physics_query.bat ... summary`; the failure trace is imported with `tools\physics_query.bat ... restore --limit 4` so binary replay files and failed restore attempts stay inspectable without loading raw artifacts into the model.

## Runtime Facades And Streams

`SceneRuntime` lives in `SkullbonezSource/Runtime/Scene/SceneRuntime.h/.cpp` and owns the active `RunSceneState` plus the scene queue. `SimulationSystem` lives in `SkullbonezSource/Runtime/SimulationSystem.h/.cpp` and owns timestep policy plus the fixed-step/variable-step physics accumulators. `ReplayRecorder` lives in `SkullbonezSource/Runtime/Replay/ReplayRecorder.h/.cpp` and owns the bounded presentation sample ring plus hash logging. `CaptureSystem` lives in `SkullbonezSource/Runtime/CaptureSystem.h/.cpp` and owns BMP readback plus scene screenshot/autocycle capture policy. `RuntimeDiagnostics` lives in `SkullbonezSource/Runtime/RuntimeDiagnostics.h/.cpp` and owns perf CSV, scene-finished, and SkullScope run logging policy. `InputController` lives in `SkullbonezSource/Runtime/InputController.h/.cpp` and owns runtime key-edge capture plus mouse-look reset/delta policy. `Run` still coordinates the broad scene load/reset side effects: object construction, terrain swaps, camera setup, UI override application, diagnostics context, input command application, capture completion actions, replay capture/scrub callbacks, and render/backend setup. Treat these as runtime subsystem extraction slices, not the final runtime split.

The obsolete model wrappers have been deleted. `SceneController` coordinates
scene-lifetime creation and prepares the physics-backed `RenderInstanceStore`
once per frame, while physics-facing readers use `PhysicsBodyStore` and
`ColliderStore` records directly. Durable identity, names, grouping, and
material intent live in `SceneEntityStore`; transient contact feedback lives in
render presentation rows.

## Scene JSON Fields

Scene files are JSON objects with `format: "skullbonez.scene.json"` and `version: 1`. Suites use `format: "skullbonez.suite.json"` plus a `scenes` array. Style files use `format: "skullbonez.style.json"` and can be included from scenes or other style files.

| Area | JSON fields |
|------|------------|
| Playback | `playback.frames`, `playback.exitOnComplete`, `playback.screenshotAndExit`, `playback.fixedStep`, `playback.pauseSnapshotState` |
| Capture | `capture.screenshot`, `capture.screenshotInterval` |
| Logging | `logging.perfLog`, `logging.perfLogFlush`, `logging.perfLogFlushInterval` |
| Simulation | `simulation.physics`, `simulation.timeScale`, `simulation.seed`, `simulation.world` |
| Objects | `objects[]` entries with `type` values such as `ball`, `floatingBall`, `box`, `floatingBox`, `convexHull`, or matching `*State` snapshot forms; reusable convex hull assets can also be expanded through `assetLibraries[]` and `assetInstances[]` |
| Camera | `cameras[]`, `editor.trackHeight`, `capture.autoCycleInterval` |
| Rendering | `styles[]`, `objectMaterials[]`, `debug`, `cinematic`, `ui`, `terrain`, `runtime.vsync`, `runtime.pipelineSync`, `terrain.flatSlope` |

For exact field names, inspect existing files in `SkullbonezData/scenes/` and the parser in `SkullbonezSource/Scene/TestSceneParser.cpp`.
`floatingBall` and `floatingBox` use the same fields as `ball` and `box`, but the body is fixed in world space and excluded from gravity, impulses, and scene energy.
Styles include `SkullbonezData/styles/<name>.style.json` through the `styles` or `includes` arrays. Style files hold reusable render-look JSON such as `cinematic` and `objectMaterials`, and are intentionally kept separate from cameras, physics, gameplay objects, and set dressing.
Asset libraries use `format: "skullbonez.asset_library.json"` and are registered as source assets through `AssetSystem` with names such as `assetlib.low_poly_nature` and `assetlib.buildings`; bare scene references first resolve through that registry, then fall back to `SkullbonezData/assets/<name>.assets.json`. `assetInstances[]` entries expand `convexHull` assets or `compound` stacked-hull assets into ordinary scene objects; instance `fixed`, `sleeping`, `position`, `euler`, and `velocity` values override or offset the asset defaults.
`objectMaterials[]` entries accept `target`, `mode`, `tint`/`color`/`colour`, and material response options: `roughness`, `metallic`, `specular`, `emissive`, `strength`, `transmission`, `stylization`, `flags`, and `name`. Targets are `all`, `balls`, `boxes`, `hulls`, `convex_hulls`, `prefix:<name>`, or an exact model name.
`SkullbonezData/assets/low_poly_nature.assets.json` contains reusable rock hulls plus stacked tree compounds, including `tree.pine_stack`, `tree.cedar_stack`, and `tree.small_stack`.
`SkullbonezData/assets/buildings.assets.json` contains registered destructible building compounds, including low/high brick-count variants of `building.brick_house`, `building.cute_house`, and `building.triple_decker`.
The in-game Cine tab exposes live sliders for tonemap, style modes, style grade, sky, terrain, water, basin, fog, and related cinematic values. Dragging those sliders mutates the active scene's `CinematicRenderConfig` without restarting physics; Scene tab `Save Defaults` writes only Cine controls changed by the UI as scene-local `cinematic` JSON overrides, so `.style.json` files remain reusable base descriptors.

Physics regression CSV output is command-line only via `--physics-regression-log` and `--physics-collision-time-log`; scene files must not enable it.

## Key Bindings

| Key | Action |
|-----|--------|
| Esc | Quit |
| F | Toggle fly mode. Freezes physics and camera auto-cycle. |
| N | Toggle launcher mode. Free camera with live simulation. |
| M | In launcher mode, cycle between laser ray impulse and small projectile modes. |
| Left Click | In Attach mode, select or clear the attached camera target. Outside UI/editor/Attach ownership, select or clear replay path targets. In launcher mode, fire the selected launcher action from the camera instead. Laser mode shows a short ribbon to the aimed hit; projectile mode shoots a small dynamic ball. |
| Shift+Left Click | Add another replay path history target and make it the prediction root. |
| Ctrl+Left Click | In launcher mode, select or clear the replay path target without firing. Combine with Shift to add targets. |
| F1 | In Attach mode, cycle attached-camera submodes: fixed relative, velocity forward, and ragdoll eyes when the target supports it. |
| Enter | In Attach mode, pin/unpin the camera so the mouse can operate UI/replay controls without losing the current world-space view. In launcher mode, write Debug-build repro data for the object under the crosshair to `Debug/launcher_repro_snapshots.txt`. While the replay scrubber is paused on the solver or `V2 FILE` row, trigger the same restore path as the scrubber `BRANCH` button. |
| R | Reset or rerun the current scene/generated demo while preserving live controls. |
| F2 | Save a scene snapshot. |
| F3 | Save a screenshot. |
| Backtick / ~ | Toggle edit mode. |
| Alt | In edit mode, toggle Place/Gizmo mode. Outside edit mode, toggle replay velocity edit for the selected dynamic replay target. |
| Tab | In edit mode, cycle the placement object type. |
| Ctrl+Tab | In edit mode, toggle new placements between static/fixed and dynamic/physics. |
| B | In Director mode, grab or release the camera from the active phase pose. |
| J | In Director or fly-style camera modes, capture the current camera pose into the active shot-list phase. |
| K | In Director or fly-style camera modes, select the next shot-list phase for authoring. |
| L | In Director or fly-style camera modes, save the loaded shot list. |
| Rock placement | Tab-selectable rock slab, lump, shard, and chipped-block entries place the authored convex hull rock assets with their stone material colors. |
| Tree Small / Tree Big placement | Tab-selectable tree objects place the full stacked tree at once. With Static object enabled, all tree hulls are fixed; with dynamic placement, the trunk and foliage tiers are separate physics hulls that can be knocked over. |
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
| O | Toggle terrain contact probe overlay for rolling sphere terrain inspection. |
| F7 / F8 | Step the physics pipeline debug overlay to the previous or next Catto stage. |
| G | Toggle broadphase visualizer, or cycle the tracked ball when ball tracking is active and the visualizer is off. |

Fly, launcher, and Attach active-follow mode use WASD, mouse look, Shift for faster movement, and Space to step physics while fly/Attach mode is paused. Attach mode follows a selected object and can be selected from the camera combo or by cycling camera modes with Tab.

## Test Scenes

| Scene | Purpose |
|-------|---------|
| `SkullbonezData/scenes/water_ball_test.scene.json` | Visual regression for terrain, skybox, sphere, water, and shadow. |
| `SkullbonezData/scenes/solver_smoke.scene.json` | Smoke test with 300 generated balls. |
| `SkullbonezData/scenes/perf_test.scene.json` | DX12 performance regression scene. |
| `SkullbonezData/scenes/physics_roll.scene.json` | Physics rolling validation. |
| `SkullbonezData/scenes/replay_v2_solver_one.scene.json` | Tiny generated one-ball scene for replay v2 event/checkpoint artifact validation. |
| `SkullbonezData/scenes/replay_path_pool.scene.json` | Pool-table-style chain for solver replay path/contact visualizer inspection. |
| `SkullbonezData/scenes/replay_velocity_four_ball.scene.json` | Four resting balls in a line for replay velocity editing and live prediction/cause-chain inspection. |
| `SkullbonezData/scenes/cause_effect_marble_run.scene.json` | Fixed floating ramp, scene-energy telemetry, and cube-tower cause/effect demo. |
| `SkullbonezData/scenes/physics_regression_solver.scene.json` | Byte-exact Debug physics CSV regression. |
| `SkullbonezData/scenes/bullet_sweep_wall.scene.json` | High-speed bullet into a fixed wall block; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/bullet_sweep_object.scene.json` | High-speed bullet into a fixed object corner; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/bullet_sweep_terrain.scene.json` | High-speed bullet into flat terrain; emits collision time via `--physics-collision-time-log`. |
| `SkullbonezData/scenes/shooting_reaction_volley.scene.json` | Ten camera-style bullets fired into mixed dynamic targets; validation asserts every target reacts. |
| `SkullbonezData/scenes/standing_box_repro.scene.json` | Deterministic solver-box edge-rest repro seed target. |
| `SkullbonezData/scenes/box_crater_edge_repro.scene.json` | Terrain edge-rest regression scene with Debug physics regression log. |

## Debug Logging

Use `Log().Writef()` for debug-only diagnostic output:

```cpp
Log().Writef( "Debug/physics.csv", "frame,%d,x,%.3f,y,%.3f,z,%.3f\n", frame, x, y, z );
Log().WriteEventf( "renderer_switch_ignored target=%s reason=dx12_only_runtime", targetName );
```

The log singleton lazily opens files and compiles out in Release/Profile where the implementation is guarded by `_DEBUG`.

Runtime lifecycle events go to `Debug/runtime_events.log` in Debug builds. The engine records process start, scene start, scene finish, ignored retired renderer switches, fatal exceptions, and unhandled crash stack traces in that file.
Use `Debug\SKULLBONEZ_CORE.exe --debug-crash-test` to intentionally exercise the crash stack logger.

Debug builds also support launcher-mode repro snapshots. Press `N`, centre an object in the crosshair, then press Enter. Each snapshot appends the scene, frame, active RNG seed, fixed-step mode, DX12 renderer name, camera pose, object transform, velocities, shape data, sleep/contact state, and terrain support probes to `Debug/launcher_repro_snapshots.txt`.
Snapshots also include scene load/reset counts and a `--seed` replay hint so an object can be reproduced from a fresh process.
