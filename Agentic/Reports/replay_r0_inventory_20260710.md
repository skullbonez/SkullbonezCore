# Replay R0 Reconciled Inventory

Date: 2026-07-10
Branch: `engine-cleanup-10th-july`
Scope: tracked source-bearing files under `SkullbonezSource/Runtime/Replay`

## Exact source inventory

`git ls-files SkullbonezSource/Runtime/Replay` reconciles to exactly 28 files
and 23,814 physical lines. `RunReplayProbeState.h` is a 117-line replay
dependency outside that directory and is listed separately so it cannot be
silently lost or added to the scoped total.

Validation keys: CPU = `tools\validate_all_cpu_tests.bat`; scrub =
`tools\validate_replay_scrub.bat`; v2 =
`tools\validate_replay_v2_artifact.bat`; interaction =
`tools\validate_interaction_clicks.bat`; renderer =
`tools\validate_dx12_renderer.bat`; perf = `tools\validate_perf.bat`.

| File | Lines | Owner/category | Primary public entry | Allocation owner | Current `Run` coupling | R1/R2 disposition | Validation |
|---|---:|---|---|---|---|---|---|
| `ReplayExporter.cpp` | 350 | Legacy solver/presentation JSON writer | `ReplayExporter::Save` | Cold artifact I/O allowlist | Called only by `ReplayRuntime::ExportSolver` | Retain only the supported solver-v1 debug export; delete unused presentation overload | v2, CPU |
| `ReplayExporter.h` | 43 | Legacy artifact API | two `Save` overloads | None | Indirect through runtime | Narrow to solver export, then merge with implementation if still cohesive | v2, CPU |
| `ReplayInteractionController.cpp` | 256 | Cold replay command application | `ReplayInteractionController::Apply` | None | Receives callback access to live restore | Move into replay owner and delete callback bridge | interaction, scrub |
| `ReplayInteractionController.h` | 125 | Typed UI action boundary | `ReplayInteractionCommand`, `Apply` | None | `ReplayLiveRestoreApi` stores `void*` plus callbacks | Keep typed actions; delete `ReplayLiveRestoreApi` | interaction, scrub |
| `ReplayOverlayLayout.cpp` | 286 | Shared hit/draw layout | `BuildReplayOverlayLayout` | Fixed value output | Run replay input consumes rectangles | Keep cohesive layout owner | interaction, renderer |
| `ReplayOverlayLayout.h` | 98 | Layout value schema | layout builders/types | None | Included by Run replay tools | Keep; replay input/render views consume it | interaction, renderer |
| `ReplayOverlayRenderer.cpp` | 1,071 | Late replay UI presentation | scrub/cause/status render functions | Fixed/pre-reserved draw storage | Run prepares broad mutable views | Keep under replay presentation; narrow input view | renderer |
| `ReplayOverlayRenderer.h` | 99 | Replay presentation API | render functions | None | Run constructs argument packs | Replace packs with replay-owned frame output | renderer |
| `ReplayPredictionReserve.cpp` | 80 | Registered replay allocation policy | prediction reserve helpers | `replay_prediction_working_set`, 256 MiB cap | No direct Run ownership | Keep as sole prediction growth path | perf, CPU |
| `ReplayPredictionReserve.h` | 45 | Allocation-policy API | reserve helpers | Same registered owner | Indirect | Keep | perf, CPU |
| `ReplayRecorder.cpp` | 3,107 | Presentation/solver/event retained rings | recorder configure/capture/query APIs | `replay_recorder_samples`, 64 MiB/request | Run capture and probes traverse recorders | Split by presentation, solver, and event cohesion in R3 | CPU, scrub, v2 |
| `ReplayRecorder.h` | 675 | Durable retained-sample schemas | recorder/sample APIs | Same registered owner | Run exposes recorder access through runtime | Keep schemas; remove mutable compatibility exposure | CPU, scrub, v2 |
| `ReplayRestoreService.h` | 193 | Solver-to-live-owner restore command | `ReplayRestoreService::Restore` | Fixed scratch/owner storage | Run supplies broad collection/world owners | Keep service; narrow to typed physics/scene command in R4 | scrub, physics |
| `ReplayRuntime.cpp` | 3,418 | Aggregate replay state and compatibility shell | configure/capture/accessor/tool methods | Owns fixed UI buffers and registered replay reserves | Run implements most workspace behavior around it | Split into frame coordinator plus recording/prediction/presentation owners | CPU, scrub, interaction, renderer, perf |
| `ReplayRuntime.h` | 1,067 | Replay state/value model | broad mutable accessors and config API | Fixed capacities plus reserve contracts | Run reaches mutable internal structs | Delete mutable compatibility surface; expose typed frame input/output | all replay gates |
| `ReplaySolverSnapshot.h` | 135 | Durable solver snapshot schema | snapshot value types | `replay_solver_snapshot`, 64 MiB cap in physics owner | Run probes/restore consume samples | Keep schema; document ownership in R3 | CPU, scrub, physics |
| `ReplayV2Artifact.cpp` | 2,543 | Binary chunked artifact read/write | `ReplayV2Artifact::Save/Load` | Cold artifact I/O allowlist | Run probes and import/export glue call it | Keep supported v2 owner; split reader/writer only if size closure requires | v2, CPU |
| `ReplayV2Artifact.h` | 136 | V2 artifact API/schema | save/load result and document APIs | None | Indirect | Keep | v2, CPU |
| `RunReplayCauseTreeTools.cpp` | 334 | Cause-tree input/focus decisions | `Run::TickReplayCauseTreeInput` | ReplayRuntime fixed rows | Direct Run business method | Move into replay interaction/presentation owner; delete file | interaction, renderer |
| `RunReplayImportExport.cpp` | 77 | Save-path/status forwarding glue | `SaveReplayArtifactFromScrubber` | Cold artifact I/O | Called by Run scrubber | Merge into replay artifact/workspace owner; delete file/header | v2, interaction |
| `RunReplayImportExport.h` | 32 | Tiny forwarding declaration | save helper | None | Run-only bridge | Delete after merge | v2, interaction |
| `RunReplayProbes.cpp` | 2,654 | Runtime validation drivers | `Run::Tick*Probe`, `Verify*Probe` | Probe/cold diagnostics | Direct Run business methods/state | Move to a replay probe harness owner; delete Run methods/file | scrub, v2, CPU |
| `RunReplayQueryTools.cpp` | 275 | Stable target picking/query | `Run::TryPickReplayPathTargetFromMouse` | Fixed target storage | Direct Run business method | Move into replay interaction owner; delete file | interaction, scrub |
| `RunReplayScrubberTools.cpp` | 766 | Scrub/preset/camera/restore workflow | `Run::TickReplayScrubberInput` | Replay-owned retained data | Direct Run methods and camera transition | Move decisions behind typed replay action/output; delete file | scrub, interaction |
| `RunReplayTools.cpp` | 4,778 | Prediction/path/cause presentation | `Run::RenderReplayPathVisualizer`, cause overlay | Registered prediction reserve + fixed draw requests | Largest direct Run replay implementation | Split prediction building from fixed draw-record emission; delete Run file | scrub, renderer, perf |
| `RunReplayVelocityEdit.cpp` | 775 | Velocity branch-edit interaction | tick/render Run methods | Prediction reserve and fixed UI state | Direct Run input, physics mutation, overlay methods | Move typed edit action and physics command into replay owner; delete file | interaction, scrub, physics |
| `TrajectoryStore.cpp` | 293 | Versioned prediction prefix store | replace/publish/query methods | `replay_prediction_working_set` | Accessed through ReplayRuntime and Run tools | Keep cohesive store | CPU, scrub, perf |
| `TrajectoryStore.h` | 103 | Prediction record/value API | store types and methods | Same prediction reserve owner | Indirect | Keep; remove bare-index persistent identity in R4 | CPU, scrub, perf |
| **Total** | **23,814** | **28 files** | | | | | |

Outside the scoped total, `SkullbonezSource/Runtime/RunReplayProbeState.h`
contains 117 lines of Run-owned probe state. R2 must move it with the probe
harness rather than leave a renamed shared-state adjunct.

## Run-owned business surface

The current logical `Run` surface includes replay gesture begin/end/cancel,
automation target selection, timeline reset, target picking, path/cause/velocity
rendering, cause/velocity/scrubber ticks, replay inspection-camera transitions,
live restore/hash/sample application, three probe ticks, artifact load, probe
verification, and replay-presentation loading. Definitions are spread across
`Run.cpp`, `RunInput.cpp`, `RunInteractionAutomation.cpp`, and all seven
`RunReplay*.cpp` implementations. R2 accepts only owner construction/wiring and
one typed replay frame call in `Run`; moving these methods to another Run
translation unit does not count.

## Capacity, high-water, and artifact measurements

These are separate measurements; a configured capacity is not reported as
live use, and an on-disk artifact is not reported as retained memory.

| Measurement class | Evidence | Measurement |
|---|---|---|
| Configured retained capacity | 3-second deterministic replay config | presentation 360 samples / 14 checkpoints; solver 360 / 8; events 192 |
| Observed retained high-water | replay prediction determinism run | presentation 60 samples / 2 checkpoints; solver 60 / 1 checkpoint |
| Registered solver snapshot backing-capacity high-water | allocator growth diagnostics | 1,437,696 bytes; 3 growth events; 67,108,864-byte hard cap |
| Registered prediction backing-capacity high-water | allocator growth diagnostics | prediction engine 158,970,656 bytes; debug contacts 48,749,440 bytes; bodies 3,656,208 bytes; 268,435,456-byte hard cap |
| Prediction growth telemetry | bounded diagnostic stream | owner counter reached 411; 195 retained diagnostic rows in the captured stdout |
| Raw v2 artifact | final artifact gate | `replay_save_probe.skreplay`: 13,062 bytes, 24 samples, 2 bodies, 24 hashes, 1 checkpoint, 8 events, 1 cursor |
| Generated-topology artifact | final artifact gate | `replay_generated_topology_probe.skreplay`: 17,234 bytes |
| SkullScope raw artifacts | existing scrub/restore evidence | scrub NDJSON 54,932 bytes + SQLite 225,280 bytes; restore NDJSON 54,912 bytes + SQLite 225,280 bytes |
| Bounded model-read evidence | prediction determinism output | stdout 60,290 bytes; two determinism JSON reports 5,712 bytes each |

Fixed replay presentation budgets are 24 prediction ghost frames,
`(24 + 2) * MAX_GAME_MODELS` ghost requests, `MAX_GAME_MODELS` markers, 261
baseline root points, 100 path roots, `MAX_GAME_MODELS * 4` cause contacts, and
`1 + MAX_GAME_MODELS + contacts * 3` cause rows. The user memory request clamps
to 32-512 MiB and 1-600 seconds; the current default request is 256 MiB.

## R1 deletion proof candidates

- Wire kind 2 (`runtimeCommand`) belonged to the deleted omnibus command queue.
  It has no supported owner or input and remains unsupported; query and gate
  tools now decode only explicit wire kind 10 owner actions.
- The presentation overload of legacy JSON `ReplayExporter::Save` has no caller.
  The solver overload remains temporarily owned by the explicit debug export;
  its deletion condition is replacement or removal of that supported command.
- `ReplayLiveRestoreApi` is a `void*` callback pack that lets a nominal replay
  controller reach back into Run. R2 deletes it when typed replay output carries
  the restore request to the physics/scene boundary.
- `RunReplayImportExport.*` is behavior-free save forwarding and is merged into
  the artifact/workspace owner.
- Broad mutable `ReplayRuntime` accessors and all `Run::TickReplay*`,
  `Run::RenderReplay*`, replay-camera, replay-restore, and replay-probe methods
  are compatibility authority, not a completed owner boundary. R2/R4 delete
  them rather than renaming the bag.
