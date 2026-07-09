# Replay Stage 10 Runtime/Replay Inventory

Date: 2026-07-10
Branch: nightrunner-9th-july

Stage 10 started with 28 tracked `SkullbonezSource/Runtime/Replay` source-bearing
files and 22,309 lines. After the legacy retained-path deletion, the directory
still has 28 files and 21,770 lines. `RunReplayTools.cpp` dropped from 4,955 to
4,367 lines; the active replay behavior remains split across the same ownership
files below.

| File | Responsibility | What breaks if deleted |
|---|---|---|
| `ReplayExporter.cpp` | Serializes retained recorder samples through the exporter API. | Replay artifact export helpers lose their implementation. |
| `ReplayExporter.h` | Declares the exporter surface for retained replay data. | Callers cannot compile replay export requests. |
| `ReplayInteractionController.cpp` | Applies cold replay UI commands such as scrubber, target, preset, and toggle mutations. | Replay controls stop mutating replay-owned state cleanly. |
| `ReplayInteractionController.h` | Declares replay interaction command entry points. | Input/UI code loses the command boundary. |
| `ReplayOverlayLayout.cpp` | Computes shared screen-space scrubber/cause-tree rectangles. | Hit testing and drawn overlay geometry drift apart. |
| `ReplayOverlayLayout.h` | Names overlay layout constants and rectangles. | Input and renderer code lose shared layout types. |
| `ReplayOverlayRenderer.cpp` | Draws replay scrubber, cause-tree, and status overlays. | Replay UI becomes invisible even if state still updates. |
| `ReplayOverlayRenderer.h` | Declares late-pass replay overlay drawing APIs. | Runtime renderer cannot call replay overlay drawing. |
| `ReplayPredictionReserve.cpp` | Registers and requests replay prediction working-set reserve growth. | Prediction, private-engine, and trajectory-store allocations lose their approved runtime owner. |
| `ReplayPredictionReserve.h` | Names the replay reserve owner and growth helpers. | Replay allocation-policy call sites cannot compile. |
| `ReplayRecorder.cpp` | Captures compact presentation, solver, and event rings and reconstructs dense samples for public callers. | Scrub, restore, save, hashes, and replay memory policy lose their retained timelines. |
| `ReplayRecorder.h` | Defines replay samples, compact frames, recorder configs, and recorder APIs. | Most replay runtime, artifact, and probe code cannot compile. |
| `ReplayRestoreService.h` | Applies retained solver samples back into live runtime owners. | Restore probes and scrub-restore workflows lose the rollback boundary. |
| `ReplayRuntime.cpp` | Coordinates recorders, prediction state, trajectory store, memory policy, diagnostics, and compatibility accessors. | Replay subsystem state has no owner. |
| `ReplayRuntime.h` | Defines replay-owned state, policy, prediction, trajectory, and UI structs. | Nearly every replay caller loses its shared types. |
| `ReplaySolverSnapshot.h` | Defines solver-world snapshot records used by restore/hash/delta paths. | Solver retention and restore verification lose restorable world state. |
| `ReplayV2Artifact.cpp` | Reads/writes compact chunked v2 replay files. | V2 save/load probes and artifact compatibility fail. |
| `ReplayV2Artifact.h` | Declares v2 artifact save/load result and API types. | Save/load callers and tests cannot compile. |
| `RunReplayCauseTreeTools.cpp` | Handles cause-tree input, focus, and row behavior. | Cause-tree selection/focus overlays stop working. |
| `RunReplayImportExport.cpp` | Bridges scrubber UI save commands to artifact writers. | Replay save buttons lose path/status glue. |
| `RunReplayImportExport.h` | Declares replay import/export helper glue. | Scrubber tools cannot call save helpers. |
| `RunReplayProbes.cpp` | Owns validation probes for scrub, restore, save, prediction determinism, and v2 target restore. | `validate_replay_scrub` and replay CLI probes lose coverage. |
| `RunReplayQueryTools.cpp` | Converts picks into stable replay path targets and query helpers. | Path target selection and related reports stop working. |
| `RunReplayScrubberTools.cpp` | Handles scrubber input, inspection camera, live restore, and preset controls. | Replay UI cannot scrub, inspect, restore, or save. |
| `RunReplayTools.cpp` | Builds/draws trajectory overlays, prediction previews, causal markers, and replay path visualization. | Prediction path visuals and target/cause overlays disappear. |
| `RunReplayVelocityEdit.cpp` | Implements velocity-edit picking, dragging, mutation, and overlay drawing. | Replay velocity branch editing stops working. |
| `TrajectoryStore.cpp` | Implements versioned trajectory record replacement, prefix publication, and reserve helpers. | Store-backed path drawing and determinism fingerprints break. |
| `TrajectoryStore.h` | Defines trajectory keys, points, records, and store API. | Prediction and retained path builders lose their publication contract. |

Stage 10 deletion result: the pre-`TrajectoryStore` retained solver callback
renderer, the `SKULLBONEZ_REPLAY_LEGACY_TRAJECTORY_DRAW_FALLBACK` compile-time
rollback switch, stale retained child/root path visitors, and obsolete draw
budget counters were removed. Remaining files are active ownership boundaries,
not safe whole-file deletions in this pass.
