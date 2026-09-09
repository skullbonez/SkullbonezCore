# Unified UI control inventory

Migration checklist for [unified-ui.md](unified-ui.md). Existing owners and
presenters are authoritative. A destination alone is not a passing check.

## Routing and regression rules

Tools destinations are identical in Canvas and Editor. Scene additionally opens
from the header Scenes action; Editor controls also appear in the left Editor
pane. Every control requires its surface and clipped row to be visible with no
blocking popup. Unless stated, there is no individual shortcut. Commands refer
to `Runtime/Interaction/OperatorUiCommands.h`; views are completed owner values,
not widget previews. Every catalog entry needs a separate check.

- Toggle: native click changes its owner once; held click does not repeat;
  hidden controls cannot act at their former position.
- Slider: native minimum, maximum and interior; captured drag outside; focus-loss
  cancellation; existing units, quantization and deferred/continuous semantics.
- Choice: native popup, stable identity, scrolling, cancellation, empty/disabled
  rows. Loaded scene identity changes only after owner activation.
- Inspection: retain selection, expansion, scroll and data identity through
  drawer closure and layout changes.
- Operation: actual UI route and owning result once, including unavailable/error
  cases. An existing file alone does not prove Save ran.

## Shared shell, tabs and footer

Sources: `UI.cpp`, `UIWindowInteractionOwner.cpp`, `GameUILayout.cpp` under
`Runtime/UI/GameUI`. Layout/workspace are independent presentation choices.

| Control | Action / authoritative state | Tooltip and check |
|---|---|---|
| Skull / Tools | Open/close retained drawer | Viewport shrinks/restores; no scene reload |
| Scenes / narrow scene name | Open Tools on Scene browser | One-click scene access |
| Scene / Solver Lab | Foreground workspace | Leaving pauses/retains comparison; Close releases |
| Canvas / Editor | Layout preference | Preserve camera, selection, simulation, playback and editing |
| Camera selector | Existing camera command and supported mask | Actual modes, availability and selected identity |
| Left/right folds | Presentation preferences | Preserve controls and state |
| Dock/drawer resize | Bounded shared geometry | Captured drag, window/DPI clamp, no world input |
| Profiler tab | SetActiveTab(Profiler) / active tab | Detailed timings/draw calls, distinct from F5 |
| Scene tab | SetActiveTab(Scene) / active tab | Browser and scene persistence |
| Editor tab | SetActiveTab(Editor) / active tab | Opening never enables functional editing |
| Physics tab | SetActiveTab(Physics) / active tab | Physics policy and diagnostics |
| Options tab | SetActiveTab(Options) / active tab | Presentation and timing |
| Render tab | SetActiveTab(Render) / active tab | Ordinary rendering and path style |
| Targets tab | SetActiveTab(Targets) / active tab | Existing render targets |
| Keys tab (Ctrl label) | SetActiveTab(Keys) / active tab | Generated scene/fluid controls |
| Sky tab | SetActiveTab(Sky) / active tab | Sky appearance and defaults |
| Cinematic tab | SetActiveTab(Cinematic) / active tab | Existing scene/features/parameters |
| Memory tab | SetActiveTab(Memory) / active tab | Allocations and replay retention |
| Content scroll/bar | m_scrollY and ContentHeight | Local clipping and pointer edges |
| Close/minimize/restore | SetVisible/SetMinimized | Preserve tab and inspection |
| Former float/resize/maximize | Bounded drawer resize | Replaced chrome; same retained contents |
| Renderer popup | renderer.requestedRendererIndex / DX12 catalog | DX12 only; retired choices unavailable |
| Water reflection | water.requestedWaterReflectionMode / current mode | Existing FBO/DXR/None and disabled mask |
| Blur | blurPreviewEnabled | Tools backdrop blur |
| Vsync | renderer.toggleVsync / renderer presentation | Synchronize to display refresh |
| Timeline | profiler.timelineEnabled | Detailed CPU timeline; retained on close |
| Perf | performanceHistogramEnabled | F5 Canvas toggle / Editor focus |
| Hitboxes | hitboxOverlayEnabled | UI interaction bounds |
| Compact tool selector | Same eleven tab actions | Bounded scrollable popup |
| Compact Display menu | Same footer handlers | All footer settings reachable |

## Scene

Sources: `UITabScene.cpp`, `SceneNavigationModel`, `SceneController`, authored
persistence. Canvas/Editor: Scenes opens Tools > Scene. An externally loaded
file absent from discovery must not be mislabeled Demo.

| Control | Command / authoritative source | Tooltip and check |
|---|---|---|
| Browser/filter | SceneTab filter / navigation rows | Escape cancels; typing cannot reach world |
| Discovered row | scene.requestedSceneIndex / activated path | Load identity, failures and empty list |
| Demo | scene.requestDemoScene / generated activation | Switch to generated demo |
| Create/Enter | scene.createScene, requestedSceneName / result | Validate name, starter file, refresh, load |
| Reset | scene.resetScene / current scene | Rebuild while preserving live controls |
| Reset defaults | scene.resetSceneDefaults / authored reload | Discard live edits |
| Save defaults | scene.saveSceneDefaults / persistence result | Changed saved values and active path |
| Recordings popup | requestedInteractionRecordingIndex / catalog | Existing newest-first playback identity |
| Lab library | scene.solverLab: RagdollWall/WallOnly | Existing load/cancel/error route |
| Time scale | sceneOptions.requestedTimeScale / timeScale | Continuous simulation multiplier |
| Pause lock | scene.toggleCrossScenePause / crossScenePauseLocked | Preserve pause across scene changes |
| Single step | scene.requestSingleStep / paused scene turn | One turn, disabled unless locked, held click once |
| Prediction reveal | physics.requestPredictionRevealRate / reveal clock | Reveal speed without simulation mutation |
| Continuous forecast | forecast.ToggleContinuous / forecast owner | Existing private orbital forecast |
| Reset forecast | forecast.Reset / forecast owner | Restart forecast |
| Forecast status | Immutable forecast publication | Time, age, stability, conservation, first cause |

## Editor

Sources: `UITabEditor.cpp`, `UIEditorMiniPalette.cpp`, `UIEditorMiniPaletteDraw.cpp`.
Canvas: Tools > Editor; Editor: left Editor pane or the same Tools tab.

| Control | Command / authoritative source | Tooltip and check |
|---|---|---|
| Editor mode | editor.toggleEditorMode / editorModeEnabled | Enable editing; layout alone emits nothing |
| Place mode | editor.togglePlacementMode / placementModeEnabled | Placement versus selection/gizmos |
| Static | editor.togglePlaceStatic / placeStaticObject | Static placement |
| Each OBJECT_LABELS choice | requestedObjectType, enterPlacementMode / objectType | Catalog identity/order, scroll, disabled rows |
| Each mini-palette entry | SelectEditorMiniPaletteObject / objectType | Click versus hold choices |
| Tree hold choices | fixed/sleeping/rooted and small/pine/cedar mapping | Preserve compound type/state |
| Ragdoll hold choices | active/sleeping mapping | Preserve placement state |
| Terrain alignment | editor.toggleTerrainAlign / autoTerrainAlign | Align placement with terrain |
| Viewport/history status | viewport mode, undoDepth, redoDepth | Current input mode and retained history |
| Undo/redo/delete/duplicate | Existing InputRouter bindings / EditorTools | Selected identity and history |

The palette contains choices beyond the Editor-tab widgets; retain them until
each has a verified destination. `unified-catalog-2` exercises all eleven live/disabled target entries and test-owned recording playback in both layouts.

## Physics, Options and Keys

Source: `UITabPhysics.cpp`. Each toggle uses `EmitPhysicsToggleCommand(index)`
and the owner-projected value below; each requires a Toggle check and help.

| Index | Toggle | Authoritative state |
|---|---|---|
| 0 | Collision state | physicsDebug.collisionVisualizer |
| 1 | Axes | physicsDebug.axes |
| 2 | Contacts | physicsDebug.contacts |
| 3 | Sleep state | physicsDebug.sleep |
| 4 | Transparent | physicsDebug.transparent |
| 5 | Broadphase | physicsDebug.broadphase |
| 6 | Sleep policy | physicsSleepEnabled |
| 7 | Pipeline | physicsDebug.pipeline |
| 8 | Terrain probe | physicsDebug.terrainContact |
| 9 | Tornado | tornadoEnabled |
| 10 | Visual shell | tornadoVisualShell |
| 11 | Field vectors | tornadoFieldVectors |
| 12 | Ray visual | rayCastVisualization |

| Control | Command / authoritative state | Tooltip and check |
|---|---|---|
| Pipeline previous/next | stepPhysicsPipelinePrevious/Next / stage | Separate previous/next operations |
| Alpha | requestedPhysicsDebugAlpha / alpha | Diagnostic opacity; slider |
| Contact linger | requestedPhysicsDebugContactLinger / linger | Existing duration unit; slider |
| Ray impulse | requestedRayCastImpulseStrength / strength | Existing impulse unit; slider |
| Launcher speed | requestedLauncherProjectileSpeed / speed | Projectile speed; slider |
| Gravity strength | water.requestedWorldGravity / negative worldGravity | Acceleration; preserve sign conversion |
| Terrain friction | requestedTerrainFrictionCoeff / coefficient | Dimensionless; slider |
| Object friction | requestedObjectFrictionCoeff / coefficient | Dimensionless; slider |
| Rolling friction | requestedRollingFrictionCoeff / coefficient | Dimensionless; slider |
| Tornado radius/height | requestedTornadoRadius/Height / field config | Separate distance sliders |
| Tornado inward/swirl/lift | requestedTornadoInward/Swirl/Lift / field config | Separate acceleration sliders |
| Capture lockstep | sceneOptions.toggleFixedStep / fixedStep | Capture request, distinct from live pacing |
| Hide terrain | toggleTerrainHidden / terrainHidden | Visibility toggle |
| Hide water | toggleWaterHidden / waterHidden | Visibility toggle |
| Freeze water | toggleWaterFreeze / waterFreezeDebug | Animation freeze |
| Flat water | toggleWaterFlat / waterFlatDebug | Flat-water diagnostic |
| Shadows | toggleShadows / active ordinary or cinematic state | Test both policies |
| Time scale (Options) | requestedTimeScale / timeScale | Continuous multiplier |
| Model count | requestedModelCount / count/capacity | Generated rebuild on release |
| Seed (Keys) | run.requestedSeed / seed | Generated rebuild on release |
| Balls (Keys) | requestedSolverBallCount / count/capacity | Generated rebuild on release |
| Boxes (Keys) | requestedSolverBoxCount / count/capacity | Generated rebuild on release |
| Fluid height (Keys) | water.requestedWorldFluidHeight / height | Surface height; slider |
| Fluid density (Keys) | water.requestedWorldFluidDensity / density | Existing density unit; slider |

## Render, Targets, Sky and Cinematic operations

| Control | Command / authoritative source | Tooltip and check |
|---|---|---|
| Ordinary shadows | renderTuning.toggleShadows / ordinary shadow settings | Toggle ordinary shadows |
| Save CFG | renderTuning.saveDefaults / RenderDefaultsStore | Actual persisted values |
| Save Paths | Same saveDefaults owner | Save path style through existing ordinary profile |
| Each target resource | selectedRenderTargetPreview / renderTargets.previews | Type, size, unavailable reason, preview clipping |
| Cinematic scene popup | requestedModeSceneIndex / selected scene | Existing load operation |
| Each Cinematic feature | cinematic.requestedFeature / cinematic owner | Held toggle once; pass help |
| Save sky | cinematic.saveSkyDefaults / persistence result | Actual saved sky values |
| Sky/Clouds/God rays/Volume | Same feature commands / owner | Separate toggles |
| Each Render parameter | renderTuning.requestedParam/value / ordinary owner | Expanded below; ranges and units |
| Each Sky/Cinematic parameter | cinematic.requestedParam/value / cinematic owner | Expanded below; ranges and units |

No Cinematic master toggle is invented: no corresponding control was drawn in
the existing tab. Shared commands do not justify removing either Tools tab.

## Detailed Profiler and Memory

Sources: `UITabProfiler.cpp`, `UITabProfilerHistogram.cpp`, `UITabMemory.cpp`.
Detailed Profiler remains distinct from F5's marker-history line chart.

| Control/readout | Action / authoritative state | Destination, tooltip and check |
|---|---|---|
| Workers toggle | requestedWorkerThreads / current and restore count | Tools > Profiler; disable/restore |
| Worker count | requestedWorkerThreads / count and maximum | Deferred slider, worker threads |
| Each marker expander | ToggleMarker / expandedHashes | Retain hierarchy identity |
| Each draw-call expander | ToggleDrawNode / drawExpandedHashes | Retain draw hierarchy identity |
| Timeline | timelineEnabled | CPU timeline in ms; same footer action |
| CPU/self/work/p50/p99 | MarkerSnapshot and worker samples | Existing ms and aggregation |
| Core work / draw calls | workerCoreSamples, DrawTraceSnapshot | core-ms, jobs, draws, vertices, instances |
| Each replay memory preset | replayMemory.requestPolicy / active policy | Existing seconds/MiB presets |
| Retention | requestedRetentionSeconds / replay policy | Seconds; slider |
| Budget | requestedBudgetMiB / replay policy | MiB; slider |
| Memory tables/waterline | MemoryTab frame and retained history | Allocated/reserved/capacity units |
| F5 visibility/focus | performanceHistogramEnabled; F5 | Canvas overlay / Editor pinned focus; retain samples |
| F5 each marker and Frame Total | histogramOptionHashes/Selected | Millisecond history; stable identity |
| F5 floating drag/resize | histogram bounds | Canvas floating / Editor dock; capture |
| F6 visibility/focus | overlayEnabled; F6 | Canvas overlay / Editor pinned; retain waterline/events |
| F5 detailed action | SetActiveTab(Profiler), open Tools | Preserve chart and detailed inspection |
| F6 detailed action | SetActiveTab(Memory), open Tools | Preserve memory histories and operations |

## Replay and Causes

Canvas: thin bottom transport, Details > Replay/Causes. Editor: bottom transport,
left Replay and right Causes. Existing owners apply commands; selected,
published and rendered identities must agree.

| Control | Authoritative owner/action | Tooltip and check |
|---|---|---|
| Scrubber hold/drag/seek | ReplayScrubber + InputRouter / cursor | Exact tick, capture outside, pause semantics |
| Recording save/load | Existing artifact operations | Artifact identity, cancellation and errors |
| Branch | Replay authoring branch command | Recorded branch identity |
| High detail | Recording detail setting | Same setting/meaning |
| Modify velocity (old ALT VEL) | Existing velocity-edit action | Identical command and selected identity |
| Predict/horizon | Prediction publication owner; P | Selected/published/rendered target, P behavior |
| Ragdoll | Replay display setting | Same visual policy, no timeline reset |
| Past / Live | Existing display/transport settings | Past visibility versus return to live |
| All / Prediction / Contacts | Cause-tree filter | Same evidence set and filter state |
| Filter field/funnel | Cause text/family filter | Keyboard focus prevents world/UI shortcuts |
| Tree rows/expanders | Stable evidence identities | Selected primary/counterpart and source frame |
| Evidence open/fold | CauseInspection drawer | Preserve inspected evidence |
| Three Summary sections | summaryExpandedSection | Accordion folds survive layout and launch |
| Raw record / Copy | Raw projection and copy command | Exact evidence and clipboard text |
| Iterations / scroll | Solver iteration projection | Exact records; missing/ambiguous evidence explicit |
| Blue prediction outlines | blueOutlinesVisible | Preserve semantic blue and rendered identity |
| Grey resting outlines | greyOutlinesVisible | Preserve semantic grey and rendered identity |

## Existing planning overlays

Sources: `Planning/ReplayPlanningOverlayLayout.h/.cpp`, `ReplayTripPlanner`,
`ReplayPorkchopPanel`, App planning dispatch. Shared viewport geometry and local scrolling are exercised by `unified-planning-8`; near-Mars native Commit has separate evidence. The unchanged solar-system planner regression passes all eight assertions in unified-trip-probe-5-report.json.

| Control | Owner/action | Existing availability and check |
|---|---|---|
| Intercept readout | Planned intercept projection | Selected trip/target; units and identity |
| Trip toggle | ToggleReplayTripPlanner; J | Keyboard-unblocked context |
| Time-of-flight minus | TimeOfFlightDecrease | Existing bounds; seconds |
| Time-of-flight plus | TimeOfFlightIncrease | Existing bounds; seconds |
| Plan | Trip planner plan command | Endpoint/solver availability and result |
| Commit | Trip planner commit command | Commit-ready state; replay branch identity |
| Cancel | Trip planner cancel command | Clear plan without world input leak |
| Porkchop toggle | ToggleReplayPorkchopPanel; I | Keyboard-unblocked context |
| Each grid cell | ReplayPorkchopCellAtPointer / selectedCell | Row-major departure/flight time and delta-v; unavailable cells |
| Grid hover/readout | hoveredCell / sweep values | Units, progress, missing/invalid data |
| Path colour / guide arcs | Existing comma / H bindings | Existing display policy and semantic colours |

## Solver Lab

`PhysicsComparisonPanel` emits existing actions; `RunComparison` owns load,
playback, camera and findings. Canvas uses Details; Editor uses left controls
and right differences. Both share transport. Recorded divergence does not prove
causality; absent and ambiguous evidence remain explicit.

| Control | Existing action / owner state | Check |
|---|---|---|
| Open | 1 / comparison load | File identity, cancellation/error, release-before-replace |
| Save finding | 3 / PhysicsComparison::SaveFinding | Saved bundle/build identities, tick, selected object/event, camera, loop and settings |
| Load finding | 4 / PhysicsComparison::LoadFinding | Restore saved inspection; picker cancellation retains it, accepted replacement releases old evidence before load; invalid files show an error inside the shell |
| Focus | 2 / camera | Target and selected object |
| Follow A | 10 / follow flag | Retain flag/camera through layout changes |
| Split orientation | 14 / orientation | Both axes and per-pane picking |
| Split/Overlay/Toggle/Heatmap/Pixels | 20-24 / view mode | All five paired render modes |
| A/B | 11 / selected display | Distinct colours/data |
| X-ray | 12 / display flag | Existing semantics |
| Selected only/All/Diff only | 6/7/8 / event filters | Same filtered event identities |
| First difference | 9 / divergence event | Exact recorded tick |
| Threshold | 13 / threshold | Existing units and event recomputation |
| Each event row | 1000+index / event identity | Seek correct event |
| Previous/Reverse/Play/Next | 30/31/32/33 / playback | Exact ticks, directions and pause |
| Speed/Loop | 34/35 / playback settings | Existing choices retained |
| Close comparison | 5 / loaded data | Explicit release, distinct from workspace exit |
| Scene workspace | Foreground workspace | Pause/retain tick, selection, camera/settings; no auto-resume |
| Selected-object details | Paired samples | Identity and missing samples |
| Height/vertical-velocity plots | position.y / velocity.y | Existing units and gaps |

## Evidence and remaining checks

Native artifacts are under `TestOutput/skarness/`; build/check logs are recorded
in the owning plan. No baseline is changed by this inventory.

- `unified-compact-tools-final`: all eleven tabs at three small sizes in both
  layouts; compact footer, popup scrolling, focus loss and F5 marker selection.
- `unified-wide-tools-after-compact`: workers/hierarchy/timeline, Memory presets
  and budget, shared tooltips. This does not certify every diagnostics readout.
- `unified-scene-time-2`: pause lock, held one-shot step, reveal and forecast in
  both layouts with actual owner observations.
- `unified-scenes-saved-values-3`: create/filter/Demo/Reset/Reset defaults and
  actual Save with changed fluid height; only test-owned scene files.
- `unified-causes-typing-5`: text, Backspace, Escape/Return, shortcut isolation,
  evidence identity, folding/tabs, layout/drawer retention.
- `unified-preferences-evidence-2`: layout/dock/drawer/tab and summary-section
  preference through restart; Tools closed; invalid/malformed file fallback.
- `unified-render-catalog-2`: all 38 ordinary parameters in both layouts.
- `unified-cinematic-catalog-3`: all 26 Sky and 64 Cinematic parameters and
  all four/eight feature toggles in both layouts.
- `unified-physics-ui-3`: all 13 Physics toggles/sliders and pipeline navigation
  in both layouts, including the existing tornado auto-visual coupling.

- `unified-options-keys-2`: all six Options toggles and seven Options/Keys sliders
  in both layouts (54 checks).
- `unified-editor-both-catalog-1`: all 37 catalog choices, 24 quick choices and
  11 hold variants in both layouts; layout/editor independence and dock retention.
- `unified-files-2`: native Open, finding Save/Load, cancellation and load errors;
  exact saved tick, object, camera, loop, display mode and restored inspection.
- `unified-replay-files-3`: recording Save/Load, cancellation, missing file and recovery.
- `unified-config-files-1`: Save CFG/Paths/Sky persist changed owner values to
  isolated config files while preserving comments and the authored original.

- `unified-catalog-2`: all eleven Target entries with live/disabled identity assertions,
  and native playback of a test-owned recording in each layout; both reports confirm F5 consumed.
- `unified-diagnostics-9`: worker endpoints/interior/restore, marker and draw hierarchy
  folding and retention, all three Memory presets and both sliders in each layout.
  Retention now matches Replay's 20-600-second range; budget remains 32-512 MiB.
- `unified-planning-8`: native TOF, Plan/Cancel, selected/hovered transfer cell identity,
  local scroll and screenshots at 900/640/480/320 pixels in both layouts.
- `unified-planning-commit-1`: near-Mars test fixture converges and held Commit clears
  the candidate once in each layout. The separate unchanged solar-system regression now also passes.

Remaining: ordinary solar-system planning regression, independent review findings,
complete visual review and terminal gates. No aggregate plan phase is accepted merely
because a control has a destination listed here.

## Expanded render parameters

Each row below is a separate native route. Destinations/availability are as above.
Render emits `renderTuning.requestedParam/value`; Sky/Cinematic emit
`cinematic.requestedParam/value`. Observed indices come from the existing owner
projection. Tests do not write these observations. Formats preserve existing
units; dimensionless tuning values acquire no invented unit. The checks cover
minimum, maximum, interior and hidden-control isolation, not Save operations.

### Render

| Control | Command enum / owner index | Range | Step | Format |
|---|---|---|---|---|
| Sun intensity | `SunIntensity` / 0 | 0 to 4 | 0.01 | `%.2f` |
| Sun R | `SunRed` / 1 | 0 to 2 | 0.01 | `%.2f` |
| Sun G | `SunGreen` / 2 | 0 to 2 | 0.01 | `%.2f` |
| Sun B | `SunBlue` / 3 | 0 to 2 | 0.01 | `%.2f` |
| Ambient | `AmbientStrength` / 4 | 0 to 1.5 | 0.01 | `%.2f` |
| Sky R | `SkyRed` / 5 | 0 to 1.5 | 0.01 | `%.2f` |
| Sky G | `SkyGreen` / 6 | 0 to 1.5 | 0.01 | `%.2f` |
| Sky B | `SkyBlue` / 7 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground R | `GroundRed` / 8 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground G | `GroundGreen` / 9 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground B | `GroundBlue` / 10 | 0 to 1.5 | 0.01 | `%.2f` |
| Strength | `ShadowStrength` / 11 | 0 to 1 | 0.01 | `%.2f` |
| Softness | `ShadowSoftness` / 12 | 0.25 to 4 | 0.01 | `%.2f` |
| Depth bias | `ShadowDepthBias` / 13 | 0 to 0.005 | 1e-05 | `%.5f` |
| Slope bias | `ShadowSlopeBias` / 14 | 0 to 0.005 | 1e-05 | `%.5f` |
| Water R | `WaterRed` / 15 | 0 to 1.5 | 0.01 | `%.2f` |
| Water G | `WaterGreen` / 16 | 0 to 1.5 | 0.01 | `%.2f` |
| Water B | `WaterBlue` / 17 | 0 to 1.5 | 0.01 | `%.2f` |
| Alpha | `WaterAlpha` / 18 | 0 to 1 | 0.01 | `%.2f` |
| Reflection | `WaterReflection` / 19 | 0 to 1 | 0.01 | `%.2f` |
| Fresnel F0 | `WaterFresnel` / 20 | 0 to 0.12 | 0.001 | `%.3f` |
| Ball roughness | `BallRoughness` / 21 | 0.25 to 2 | 0.01 | `%.2f` |
| Ball specular | `BallSpecular` / 22 | 0 to 2 | 0.01 | `%.2f` |
| Box roughness | `BoxRoughness` / 23 | 0.25 to 2 | 0.01 | `%.2f` |
| Box specular | `BoxSpecular` / 24 | 0 to 2 | 0.01 | `%.2f` |
| Future width | `TrajectoryFutureWidth` / 25 | 1 to 6 | 0.05 | `%.2f px` |
| Future opacity | `TrajectoryFutureAlpha` / 26 | 0.05 to 1 | 0.01 | `%.2f` |
| Future edge feather | `TrajectoryFutureEdgeFeather` / 27 | 0.25 to 1.25 | 0.05 | `%.2f px` |
| Causal width | `TrajectoryCausalWidth` / 28 | 1 to 6 | 0.05 | `%.2f px` |
| Causal opacity | `TrajectoryCausalAlpha` / 29 | 0.05 to 1 | 0.01 | `%.2f` |
| Causal edge feather | `TrajectoryCausalEdgeFeather` / 30 | 0.25 to 1.25 | 0.05 | `%.2f px` |
| Baseline width | `TrajectoryBaselineWidth` / 31 | 1 to 6 | 0.05 | `%.2f px` |
| Baseline opacity | `TrajectoryBaselineAlpha` / 32 | 0.05 to 1 | 0.01 | `%.2f` |
| Baseline edge feather | `TrajectoryBaselineEdgeFeather` / 33 | 0.25 to 1.25 | 0.05 | `%.2f px` |
| Marker width | `TrajectoryMarkerWidth` / 34 | 1 to 6 | 0.05 | `%.2f px` |
| Marker opacity | `TrajectoryMarkerAlpha` / 35 | 0.05 to 1 | 0.01 | `%.2f` |
| Marker edge feather | `TrajectoryMarkerEdgeFeather` / 36 | 0.25 to 1.25 | 0.05 | `%.2f px` |
| Selected emphasis | `TrajectorySelectedEmphasis` / 37 | 0 to 1 | 0.01 | `%.2f` |

### Cinematic

| Control | Command enum / owner index | Range | Step | Format |
|---|---|---|---|---|
| Exposure | `Exposure` / 0 | 0.05 to 3 | 0.01 | `%.2f` |
| Gamma | `Gamma` / 1 | 1 to 3 | 0.01 | `%.2f` |
| Sky mode | `SkyMode` / 2 | 0 to 32 | 1 | `%.0f` |
| Terrain mode | `TerrainMode` / 3 | 0 to 32 | 1 | `%.0f` |
| Object style | `ObjectStyle` / 4 | 0 to 32 | 1 | `%.0f` |
| Water mode | `WaterMode` / 5 | 0 to 4 | 1 | `%.0f` |
| Saturation | `StyleSaturation` / 6 | 0 to 2.5 | 0.01 | `%.2f` |
| Contrast | `StyleContrast` / 7 | 0 to 2.5 | 0.01 | `%.2f` |
| Vignette | `StyleVignette` / 8 | 0 to 1 | 0.01 | `%.2f` |
| Azimuth | `SunAzimuth` / 9 | 0 to 1 | 0.005 | `%.3f` |
| Elevation | `SunElevation` / 10 | 0 to 1 | 0.005 | `%.3f` |
| Brightness | `SunBrightness` / 11 | 0 to 40 | 0.1 | `%.1f` |
| Sun R | `SunRed` / 12 | 0 to 2 | 0.01 | `%.2f` |
| Sun G | `SunGreen` / 13 | 0 to 2 | 0.01 | `%.2f` |
| Sun B | `SunBlue` / 14 | 0 to 2 | 0.01 | `%.2f` |
| Glow | `SkyGlow` / 15 | 0 to 8 | 0.05 | `%.2f` |
| Horizon R | `HorizonRed` / 16 | 0 to 1.5 | 0.01 | `%.2f` |
| Horizon G | `HorizonGreen` / 17 | 0 to 1.5 | 0.01 | `%.2f` |
| Horizon B | `HorizonBlue` / 18 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith R | `ZenithRed` / 19 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith G | `ZenithGreen` / 20 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith B | `ZenithBlue` / 21 | 0 to 1.5 | 0.01 | `%.2f` |
| Coverage | `CloudCoverage` / 22 | 0 to 1 | 0.01 | `%.2f` |
| Softness | `CloudSoftness` / 23 | 0.01 to 0.65 | 0.01 | `%.2f` |
| Scale | `CloudScale` / 24 | 0.5 to 12 | 0.05 | `%.2f` |
| Intensity | `CloudIntensity` / 25 | 0 to 1.5 | 0.01 | `%.2f` |
| Strength | `ShaftStrength` / 26 | 0 to 3 | 0.01 | `%.2f` |
| Falloff | `ShaftFalloff` / 27 | 0.25 to 5 | 0.01 | `%.2f` |
| Strength | `VolumetricStrength` / 28 | 0 to 2 | 0.01 | `%.2f` |
| Density | `VolumetricDensity` / 29 | 0 to 2.5 | 0.01 | `%.2f` |
| Decay | `VolumetricDecay` / 30 | 0.8 to 0.995 | 0.001 | `%.3f` |
| Threshold | `BloomThreshold` / 31 | 0 to 4 | 0.01 | `%.2f` |
| Knee | `BloomKnee` / 32 | 0.01 to 2 | 0.01 | `%.2f` |
| Strength | `BloomStrength` / 33 | 0 to 2 | 0.01 | `%.2f` |
| Radius | `BloomRadius` / 34 | 0.25 to 8 | 0.05 | `%.2f` |
| Relief | `TerrainRelief` / 35 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground R | `TerrainTintRed` / 36 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground G | `TerrainTintGreen` / 37 | 0 to 1.5 | 0.01 | `%.2f` |
| Ground B | `TerrainTintBlue` / 38 | 0 to 1.5 | 0.01 | `%.2f` |
| Accent R | `TerrainAccentRed` / 39 | 0 to 1.5 | 0.01 | `%.2f` |
| Accent G | `TerrainAccentGreen` / 40 | 0 to 1.5 | 0.01 | `%.2f` |
| Accent B | `TerrainAccentBlue` / 41 | 0 to 1.5 | 0.01 | `%.2f` |
| Grid scale | `TerrainGridScale` / 42 | 0.1 to 120 | 0.1 | `%.1f` |
| Grid strength | `TerrainGridStrength` / 43 | 0 to 4 | 0.01 | `%.2f` |
| Water R | `WaterTintRed` / 44 | 0 to 1.5 | 0.01 | `%.2f` |
| Water G | `WaterTintGreen` / 45 | 0 to 1.5 | 0.01 | `%.2f` |
| Water B | `WaterTintBlue` / 46 | 0 to 1.5 | 0.01 | `%.2f` |
| Alpha | `WaterAlpha` / 47 | 0 to 1 | 0.01 | `%.2f` |
| Reflection | `WaterReflection` / 48 | 0 to 1 | 0.01 | `%.2f` |
| Glint | `WaterGlint` / 49 | 0 to 4 | 0.01 | `%.2f` |
| Center X | `BasinCenterX` / 50 | 0 to 1200 | 1 | `%.0f` |
| Center Z | `BasinCenterZ` / 51 | 0 to 1200 | 1 | `%.0f` |
| Radius X | `BasinRadiusX` / 52 | 1 to 500 | 1 | `%.0f` |
| Radius Z | `BasinRadiusZ` / 53 | 1 to 500 | 1 | `%.0f` |
| Feather | `BasinFeather` / 54 | 0 to 1 | 0.01 | `%.2f` |
| Basin Depth | `BasinDepth` / 55 | 0 to 80 | 1 | `%.0f` |
| Rim Lift | `BasinRimLift` / 56 | 0 to 60 | 1 | `%.0f` |
| Density | `FogDensity` / 57 | 0 to 0.006 | 5e-05 | `%.5f` |
| Opacity | `FogOpacity` / 58 | 0 to 1 | 0.01 | `%.2f` |
| Start | `FogStart` / 59 | 0 to 500 | 1 | `%.0f` |
| End | `FogEnd` / 60 | 100 to 4000 | 10 | `%.0f` |
| Fog R | `FogRed` / 61 | 0 to 1.5 | 0.01 | `%.2f` |
| Fog G | `FogGreen` / 62 | 0 to 1.5 | 0.01 | `%.2f` |
| Fog B | `FogBlue` / 63 | 0 to 1.5 | 0.01 | `%.2f` |

### Sky

| Control | Command enum / owner index | Range | Step | Format |
|---|---|---|---|---|
| Azimuth | `SunAzimuth` / 9 | 0 to 1 | 0.005 | `%.3f` |
| Elevation | `SunElevation` / 10 | 0 to 1 | 0.005 | `%.3f` |
| Sun power | `SunBrightness` / 11 | 0 to 40 | 0.1 | `%.1f` |
| Glow | `SkyGlow` / 15 | 0 to 8 | 0.05 | `%.2f` |
| Sun R | `SunRed` / 12 | 0 to 2 | 0.01 | `%.2f` |
| Sun G | `SunGreen` / 13 | 0 to 2 | 0.01 | `%.2f` |
| Sun B | `SunBlue` / 14 | 0 to 2 | 0.01 | `%.2f` |
| Horizon R | `HorizonRed` / 16 | 0 to 1.5 | 0.01 | `%.2f` |
| Horizon G | `HorizonGreen` / 17 | 0 to 1.5 | 0.01 | `%.2f` |
| Horizon B | `HorizonBlue` / 18 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith R | `ZenithRed` / 19 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith G | `ZenithGreen` / 20 | 0 to 1.5 | 0.01 | `%.2f` |
| Zenith B | `ZenithBlue` / 21 | 0 to 1.5 | 0.01 | `%.2f` |
| Coverage | `CloudCoverage` / 22 | 0 to 1 | 0.01 | `%.2f` |
| Softness | `CloudSoftness` / 23 | 0.01 to 0.65 | 0.01 | `%.2f` |
| Scale | `CloudScale` / 24 | 0.5 to 12 | 0.05 | `%.2f` |
| Intensity | `CloudIntensity` / 25 | 0 to 1.5 | 0.01 | `%.2f` |
| Shafts | `ShaftStrength` / 26 | 0 to 3 | 0.01 | `%.2f` |
| Falloff | `ShaftFalloff` / 27 | 0.25 to 5 | 0.01 | `%.2f` |
| Volume | `VolumetricStrength` / 28 | 0 to 2 | 0.01 | `%.2f` |
| Density | `VolumetricDensity` / 29 | 0 to 2.5 | 0.01 | `%.2f` |
| Exposure | `Exposure` / 0 | 0.05 to 3 | 0.01 | `%.2f` |
| Gamma | `Gamma` / 1 | 1 to 3 | 0.01 | `%.2f` |
| Saturation | `StyleSaturation` / 6 | 0 to 2.5 | 0.01 | `%.2f` |
| Contrast | `StyleContrast` / 7 | 0 to 2.5 | 0.01 | `%.2f` |
| Vignette | `StyleVignette` / 8 | 0 to 1 | 0.01 | `%.2f` |
