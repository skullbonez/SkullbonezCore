# ImGui + Tracy E0 Coexistence Inventory

Date: 2026-07-18

Branch: `nightrunner-18th-july`

Plan task: E0 / 18

Result: complete; documentation and screenshot evidence only

## Ratified Interaction Contract

- Default dock topology remains the plan sketch: editor/scene/hierarchy/create
  on the left, game viewport dominant in the center, Inspector plus
  World/Rendering/Diagnostics and compact Causality on the right, replay
  transport always visible across the bottom, and a small status bar below it.
- Minimum supported client size is **1280 x 720**. Below that, panels may be
  manually redocked, but the default-layout acceptance test does not apply.
  The existing legacy floating window keeps its independent 520 x 250 clamp.
- Development configurations are **Debug, Profile, and Automation** behind one
  future `SKORE_DEVELOPMENT_TOOLS` compile capability. Release has neither
  ImGui nor Tracy sources, symbols, resources, or initialization paths.
- The exact startup selector is `--dev-ui legacy|imgui|both`; omitted means
  `legacy` until E17 owner evaluation changes a later default. The ImGui
  `View > Surfaces` menu exposes independent `Legacy UI` and `ImGui Editor`
  checkboxes; at least one remains enabled. Existing `0` continues to toggle
  only the legacy surface, so enabling it from ImGui-only produces Both.
- No new global UI-cycle hotkey is reserved in this campaign. Existing engine
  bindings keep their current meanings.
- Native ImGui platform multi-viewport remains **deferred**. E0 ratifies only
  single-window DX12 docking in the engine-owned Win32 client area.
- The legacy UI, replay overlays, and causality window remain compiled and
  behaviorally intact throughout the campaign. This report maps migration
  destinations; it authorizes no deletion.

## Screenshot Evidence

These PNGs were generated from fresh Profile/Automation captures with existing
scenes and interaction scripts. No screenshot baseline was changed.

| State | Evidence | What it freezes |
|---|---|---|
| Full legacy controls/footer | [legacy-controls-footer.png](imgui-tracy-e0-images/legacy-controls-footer.png) | 12-tab chrome, footer toggles/combos/stats, content scrolling |
| Profiler timeline | [legacy-profiler-timeline.png](imgui-tracy-e0-images/legacy-profiler-timeline.png) | marker tree, CPU/self/P50/P99/span columns, worker control |
| Physics authoring | [legacy-physics.png](imgui-tracy-e0-images/legacy-physics.png) | debug toggles, pipeline stepping, physics sliders |
| Minimized mode | [legacy-minimized.png](imgui-tracy-e0-images/legacy-minimized.png) | minimized restore bar and camera-mode control |
| Editor mini-palette | [legacy-editor-palette.png](imgui-tracy-e0-images/legacy-editor-palette.png) | left placement palette, status chips, selection/gizmo facts |
| F6 memory waterline | [legacy-memory-overlay.png](imgui-tracy-e0-images/legacy-memory-overlay.png) | independent memory overlay and retained-event rail |
| Replay and causality | [legacy-replay-causality.png](imgui-tracy-e0-images/legacy-replay-causality.png) | bottom scrubber, prediction controls, right cause tree/window |

The memory and replay interaction reports passed. The existing
`editor_undo_redo.json` captured both requested editor screenshots, then failed
later fixed-coordinate transform assertions (`failed to capture editor selection
state`) when rerun alone. E0 does not change that harness. It is recorded as a
pre-existing/transition-sensitive follow-up risk, not hidden as passing evidence.

## Complete Legacy Surface Inventory

Disposition vocabulary:

- **Keep**: same purpose remains available in the ImGui editor.
- **Regroup**: same typed capability moves to the dock location named here.
- **Tracy**: deep generic profiling moves to the external Tracy viewer; the
  legacy implementation stays for side-by-side evaluation.
- **Legacy-only**: intentionally not cloned in the first ImGui surface.

| Surface | Current interactive controls and visible facts | ImGui disposition |
|---|---|---|
| Window chrome | visible/minimized/maximized state, drag, resize, close-to-minimize, maximize, restore, scroll bar, backdrop blur, cached drawing | Legacy-only floating chrome; ImGui uses the dock host. Keep coexistence toggle. |
| `Prof` | worker enable, worker-count slider, expandable marker tree, CPU/self/P50/P99, timeline spans, draw-call tree, worker-core chart | Tracy for timing/tree/timeline/histogram; regroup worker and engine counters under Diagnostics. |
| `Scene` | filterable discovered-scene combo, Demo Scene, New Scene/name entry, Reset, Reset Defaults, Save Defaults, time scale; renderer/status/frame/rate/scene index/energy/model facts | Regroup into left Scene browser plus status bar. |
| `Edit` | editor mode, place mode, static/dynamic, 37 object recipes, viewport cursor/look state, undo/redo depths | Regroup into left Modes, Hierarchy, Assets/Create and contextual Inspector. |
| editor minimized palette | 37 recipes, tree variants/placement policy, ragdoll mode, place/static/level chips, object count, camera mode, restore | Keep as legacy evaluation surface; ImGui uses Assets/Create and toolbar. |
| `Phys` | collision, transparency, broadphase, pipeline, axes, contacts, sleep state/policy, terrain probe, tornado, visual shell, field vectors, ray visualization; previous/next pipeline stage; body alpha, contact linger, ray impulse, projectile speed, gravity, terrain/object/rolling friction, tornado radius/height/inward/swirl/lift | Regroup settings under World/Simulation and debug facts under Diagnostics/Physics. |
| `Sound` | contact audio, counters, flash-mode cycle, simple mode; 13 global sliders; sample selection with Play/Use; material-set selection and 9 set sliders; up to four bands with 5 sliders each; reducer counters | Regroup into Audio Authoring; reducer/capacity facts under Diagnostics/Audio. |
| `Opt` | fixed step, terrain hidden, water hidden, water freeze, flat water, shadows, presentation fact, time scale, model count | Regroup into Simulation, Viewport visibility, Rendering/Water, and Preferences. |
| `Render` | shadows, Save CFG, per-view visibility facts, 25 sliders: sun RGB/intensity, ambient, sky RGB, ground RGB, shadow strength/softness/depth/slope bias, water RGBA/reflection/Fresnel, ball/box roughness/specular | Regroup into right Rendering and Diagnostics/Visibility. |
| `Targets` | render-target selector, availability/type/size metadata, live texture preview for up to 12 targets | Regroup into normally closed Diagnostics/Render Targets. |
| `Ctrl` | seed, solver balls, solver boxes, world fluid height, world fluid density | Regroup into Simulation and World/Fluid. |
| `Sky` | Save Sky; Sky/Clouds/God rays/Volume; 26 direction/palette/cloud/ray/volume/grade sliders | Fold into Rendering/Environment; no duplicate sky editor. |
| `Cine` | concept/cinematic mode selector; Sky/Clouds/God rays/Volume/Bloom/Fog/Relief/Shadows; all 67 `UICinematicParam` sliders from tonemap through fog | Split into Rendering/Lighting, Environment, Post, Water, Terrain/Materials. |
| `Mem` | Balanced/Compact/Lossless replay policy, retention, budget, clamp facts; process/replay/object/unattributed totals; replay categories/trajectory counters; upload arena; reserve-growth table | Tracy for generic allocation investigation; regroup fixed-capacity/replay-reserve facts under Diagnostics/Engine Memory. |
| F5 overlay | movable/resizable multi-marker 120-sample performance histogram and selector | Tracy; legacy overlay retained for comparison. |
| F6 overlay | movable memory waterline, retained-event pins, newest allocation event | Generic graph superseded by Tracy; engine reserve facts regrouped under Diagnostics. |
| Footer | DX12 renderer selector, FBO/DXR/None water reflection, Blur, VSync, Hitboxes, Perf, Timeline; FPS/frame/draw/UI stats | Renderer selector omitted; VSync in Viewport, hitboxes in Diagnostics, performance in Tracy, essential facts in status bar. |

The full object recipe vocabulary is: Box, Ball, Sphere, Hull wedge, Hull tri
prism, Hull tapered, Hull pyramid, Hull hex prism, Hull diamond, Rock slab,
Rock lump, Rock shard, Rock chipped, Root small, Root large, Tree small, Tree
pine, Tree cedar, the three slope/sleep/rooted tree variants, Pine shedding,
Ragdoll, Ragdoll sleep, Brick house low/high, Cute house low/high, Triple
decker low/high, and Brick wall 200.

## Replay Transport And Causality Inventory

The bottom hot zone publishes one fixed typed surface. Every control is mapped:

| Current control/interaction | Typed action/owner | ImGui destination |
|---|---|---|
| Solver or loaded-presentation track label and normalized scrub track | `ReplayScrubberAction::Scrub`, `ReplayScrubber` via `ReplayRuntime` | permanent bottom transport |
| save icon / `LOAD` | `Save` / `Load`; replay timeline plus cold file picker | bottom transport |
| `BRANCH` | `RestoreBranch`; replay restore transaction | bottom transport |
| `PAUSE` / live edge | `TogglePause`; scrubber owner | bottom transport |
| `ALT VEL` | `ToggleVelocityEdit`; replay authoring | bottom transport, advanced action |
| `PREDICT` | `TogglePrediction`; replay prediction owner | bottom transport |
| horizon slider, 1-20 seconds | `SetPredictionHorizon` | bottom transport |
| `RAGDOLL` | `ToggleRagdollVisuals` | bottom transport |
| `PAST` | `TogglePastPath` | bottom transport |
| bottom-edge reveal/fade | hot-zone surface; scrubber visibility owner | removed as a requirement because transport is always docked; legacy keeps it |
| select path/body in world | `ReplayRuntime::RouteWorldPointer` / `ApplyPathPick` | viewport selection feeding Causality |
| comma color cycle | `ReplayRuntime::CyclePathColorMode` | Causality/Viewport presentation menu |
| cause tree row selection | replay authoring/cause-tree state | compact right Causality summary |
| cause window title drag, content scroll, resize corner | `ReplayCauseWindowSurface` and replay authoring state | dock/expand behavior; legacy drag/resize retained only in old surface |

## Typed UI Command Inventory

This is the complete `InGameUICommands` write boundary. ImGui must emit these
same one-frame values or a later owner-approved typed successor; it receives no
subsystem backpointer.

| Packet | Every field |
|---|---|
| `ui` | `userInteracted` |
| `renderer` | `toggleVsync`, retired `requestedRendererIndex` |
| `scene` | `resetScene`, `resetSceneDefaults`, `requestDemoScene`, `saveSceneDefaults`, `createScene`, `requestedSceneName[64]`, `requestedSceneIndex` |
| `editor` | `toggleEditorMode`, `togglePlacementMode`, `togglePlaceStatic`, `toggleTerrainAlign`, `enterPlacementMode`, `requestPlaceStatic`, `requestedPlaceStatic`, `requestedObjectType` |
| `physics` | `toggleCollisionVisualizer`, `togglePhysicsSleepPolicy`, `togglePhysicsDebugTransparent`, `toggleBroadphaseOverlay`, `toggleTerrainContactProbe`, `toggleTornado`, `toggleTornadoVisualShell`, `toggleTornadoFieldVectors`, `toggleRayCastVisualization`, the five tornado request flags/values, ray impulse request/value, launcher speed request/value, three friction request flags/values, `requestedPhysicsDebugAlpha`, `requestedPhysicsDebugContactLinger`, `togglePhysicsDebugFlags`, `stepPhysicsPipelinePrevious`, `stepPhysicsPipelineNext` |
| `sceneOptions` | `toggleTextOnly`, `toggleFixedStep`, `toggleTerrainHidden`, `toggleWaterHidden`, `toggleWaterFreeze`, `toggleWaterFlat`, `toggleShadows`, `requestedTimeScale`, `requestedModelCount` |
| `water` | `toggleWaterReflection`, gravity/fluid-height/fluid-density request flags and values, `requestedWaterReflectionMode` |
| `run` | `requestedCameraMode`, `requestedSeed`, `requestedSolverBallCount`, `requestedSolverBoxCount` |
| `profiler` | `requestedWorkerThreads` |
| `renderTuning` | `toggleShadows`, `saveDefaults`, `requestedParam`, `requestedValue` |
| `sound` | `toggleEnabled`, `toggleDebugCounters`, `cycleFlashMode`, `toggleSimpleMode`, `requestedSetIndex`, `requestedBandIndex`, `requestedParam`, `requestedBandParam`, `requestedValue`, `previewSampleIndex`, `selectSampleIndex` |
| `cinematic` | `toggleRendering`, `saveSkyDefaults`, `requestedModeSceneIndex`, `requestedFeature`, `requestedParam`, `requestedValue` |
| `replayMemory` | `requestPolicy`, `requestedPresetIndex`, `requestedRetentionSeconds`, `requestedBudgetMiB` |
| input result | `unhandledWheelDelta`; native capture `Unchanged`, `Acquire`, `Release` |

`UICinematicParam` covers Exposure, Gamma, four style modes, saturation,
contrast, vignette, sun direction/power/RGB, sky/horizon/zenith, clouds,
shafts/volume, bloom, terrain tint/accent/grid, water/tint/reflection/glint,
basin geometry, and fog. `UICinematicFeature`, `UIRenderParam`, `UISoundParam`,
and `UISoundBandParam` remain the canonical enum vocabularies.

## Frame Snapshot Inventory

This is the complete top-level `InGameUIFrameData` read boundary, grouped only
for readability. Embedded typed snapshots remain embedded; ImGui must not
flatten them into owner reach-back.

| Group | Every top-level field |
|---|---|
| screen/catalog | `screenW`, `screenH`, `rendererName`, `sceneName`, `sceneOptions`, `sceneOptionCount`, `selectedSceneOption`, `selectedCineModeSceneOption` |
| render/perf | `drawCallsBeforeUI`, `UIDrawCalls`, `visibility`, `fps`, `renderMs`, `physicsMs`, `cpuFrameMs`, `gpuFrameMs`, `workerCoreTotalMs`, `profiler`, `profilerMarkerOptions`, `profilerMarkerOptionCount` |
| sound catalogs | `soundSets`, `soundSetCount`, `soundSamplePaths`, `soundSampleCount` |
| memory | `mainMemory`, `renderMemory`, `reserveGrowthEvents`, `reserveGrowthEventCount`, `reserveGrowthEventTotalCount`, `reserveGrowthEventDroppedCount`, `replayMemoryPreset`, `replayMemoryRequestedRetentionSeconds`, `replayMemoryRequestedBudgetMiB`, `replayMemoryPresentationRetentionSeconds`, `replayMemorySolverRetentionSeconds`, `replayMemoryBudgetClamped`, `replayMemorySolverWindowReduced` |
| run/scene | `modelCount`, `modelCapacity`, `workerThreadCount`, `maxWorkerThreadCount`, `currentFrame`, `targetFrameCount`, `rngSeed`, `solverBallCount`, `solverBoxCount`, `currentSceneIndex`, `sceneCount`, `now`, `sceneMode`, `scenePhysicsEnabled`, `sceneTextEnabled`, `textOnly`, `fixedStep`, `exitOnComplete`, `testComplete`, `vsyncEnabled`, `pipelineSyncEnabled` |
| contact audio | `contactAudioEnabled`, `contactAudioAvailable`, `contactAudioDebugCounters`, `contactAudioFlashMode`, `contactAudioFlashModeLabel`, `contactAudioMasterGain`, `contactAudioMaxDistanceScale`, `contactAudioMinClosingSpeed`, `contactAudioMinImpactScore`, `contactAudioImpactScoreRangeSeconds`, `contactAudioSimpleMode`, `contactAudioSimpleMinLinearEnergy`, `contactAudioSimpleMinLinearDeltaSpeed`, `contactAudioSimpleLinearEnergyRange`, `contactAudioBurstVoicesPerWindow`, `contactAudioRollingLevelDb`, `contactAudioRollingMaxDistance`, `contactAudioRollingMinSlipSpeed`, `contactAudioRollingVoicesPerWindow`, `contactAudioEventsSeen`, `contactAudioPatchCandidates`, `contactAudioMergedCandidates`, `contactAudioCandidateOverflows`, `contactAudioBurstWindowSkippedCandidates`, `contactAudioBudgetRejectedCandidates`, `contactAudioRejectedByThreshold`, `contactAudioRejectedByCooldown`, `contactAudioSubmittedVoices`, `contactAudioDroppedVoices`, `contactAudioRollingCandidates`, `contactAudioRollingSubmittedVoices` |
| simulation/presentation | `sceneEnergy`, `timeScale`, `presentationInterpolation`, `presentationPinned`, `presentationAlpha`, `trackHeight`, `autoCycleInterval`, `worldGravity`, `worldFluidHeight`, `worldFluidDensity` |
| physics/debug | `physicsDebugFlags`, `physicsPipelineStageName`, `physicsPipelineStageIndex`, `physicsPipelineStageCount`, `physicsDebugAlpha`, `physicsDebugContactLinger`, `physicsSleepEnabled`, `collisionVisualizer`, `physicsDebugTransparent`, `broadphaseOverlay`, `tornadoEnabled`, `tornadoVisualShell`, `tornadoFieldVectors`, `rayCastVisualization`, `tornadoRadius`, `tornadoHeight`, `tornadoInwardAcceleration`, `tornadoSwirlAcceleration`, `tornadoLiftAcceleration`, `rayCastImpulseStrength`, `launcherProjectileSpeed`, `terrainFrictionCoeff`, `objectFrictionCoeff`, `rollingFrictionCoeff` |
| world/render switches | `waterFreezeDebug`, `waterFlatDebug`, `terrainHidden`, `waterHidden`, `waterNoReflect`, `waterRTReflect` |
| input/editor | `cameraMouseActive`, `nativeCursorVisible`, `runtimeInputModeLabel`, `cameraModeIndex`, `cameraModeEnabledMask`, `editorModeEnabled`, `editorPlacementMode`, `editorPlaceStatic`, `editorTerrainAlign`, `editorViewportLookActive`, `editorObjectType`, `editorUndoDepth`, `editorRedoDepth`, `canSaveSceneDefaults` |
| render settings/resources | `cinematicRendering`, `ordinaryRender`, `cinematic`, `renderTargetPreviews`, `renderTargetPreviewCount` |

Nested bounded records are also frozen as whole values:

- `UIRenderTargetPreviewResource`: `label`, `textureHandle`, `width`, `height`,
  `available`, `depth`, `hdr`.
- `UIProfilerMarkerOption`: `name`, `leafName`, `hash`, `cpuMs`,
  `cpuAverageMs`, `gpuMs`, `colorR/G/B`, `hasGpu`, `sampleValid`,
  `isFrameTotal`.
- `UISoundSetFrameData`: `name`, `materialA/B`, impulse range, cooldowns,
  distance/gain/pitch/voice/sample counts, `bandCount`, and bounded `bands`.
- `UISoundBandFrameData`: `name`, impulse range, gain, pitch range,
  `sampleCount`.
- `ProfilerTab::FrameSnapshot`, `RenderVisibilityStats`, `MainMemoryStats`,
  `RenderMemoryStats`, `RuntimeReserveGrowthEventView`, `OrdinaryRenderConfig`,
  and `CinematicRenderConfig` stay typed publications owned by their current
  producers.

## Keyboard And Open/Close/Input Contract

The current complete binding table is preserved:

| Keys | Actions |
|---|---|
| tilde, Tab, F, N, M | editor toggle; camera cycle; fly camera; launcher; launcher fire mode |
| F1, Enter | attached-camera submode/pin; Debug launcher repro also uses Enter in its disjoint context |
| B, J, K, L | director grab, pose, phase step, shot-list save |
| Alt, Ctrl+Z, Ctrl+Y, Delete | editor tool, undo, redo, delete selection |
| 1, 2, 3, 4, 5 | freeze water, reflection cycle, flat water, terrain hide, water hide |
| V, C, O, F7, F8, 6, G | collision view, physics-overlay cycle, terrain probe, pipeline previous/next, debug transparency, broadphase overlay |
| Q, F9 | retired renderer notice; shader reload |
| comma, P | replay path color; cross-scene pause lock |
| 0, F5, F6 | legacy UI, performance histogram, memory overlay |
| Left, Right | previous/next scene |
| Escape | local focused control first; otherwise toggles legacy UI; quick repeat exits |
| F2, F3 | scene snapshot; screenshot |
| R, Backspace | reset scene; scene-context reset |

Current UI behavior is visible-by-default but minimized-by-default, with Scene
as the initial active tab. Expanded chrome can drag, resize, maximize, minimize,
scroll, and acquire/release native mouse capture. Open combos are the only
legacy state that blocks keyboard; hit regions, drags, sliders, the minimized
bar, mini-palette, and overlays block camera mouse as needed. `0` and Escape
release camera look back to the native UI pointer when they expose the UI.

## Concrete Owners And Seams

| Responsibility | Current concrete owner/seam | ImGui/Tracy rule |
|---|---|---|
| UI frame values and late composition | `UiTextPass.cpp` constructs `InGameUIFrameData`; `InGameUI` draws; `UIWindowInteractionOwner` owns widgets/capture | Reuse typed snapshots; new ImGui composer cannot read Run or subsystem owners. |
| UI input and commands | `BeginRuntimeUIFrame`, `InGameUI::UpdateInput`, `ApplyRuntimeUIFrameCommands`; `InputRouter` publishes semantic snapshots | E11 shares the typed command seam; one side-effect application pass. |
| replay presentation/scrub/cause | `ReplayRuntime` with `ReplayTimeline`, `ReplayScrubber`, `ReplayPrediction`, `ReplayAuthoring`, and `ReplayOverlay` typed surfaces | Panels publish/consume replay values and intents only. |
| Win32 input/window | `Window`, `InputRouter`, `InputController`, immutable `DeviceInputFrame` | E7 Win32 backend feeds the same ownership policy and capture arbitration. |
| DX12 submission | `RuntimeRenderer`/late UI pass through `UIRenderContext`; DX12 backend owns device, command list, descriptors, barriers, frame pacing, Present | ImGui borrows a restricted render capability; no backend reach-back. |
| profiler markers | `Core::Profiler` and existing `PROFILE_*` owner intervals, copied by `UiTextPass` | Tracy zones mirror owner boundaries; old profiler remains. |
| allocation policy | `RuntimeAllocationTracker` and `RuntimeReserveAllocator` | E2 adds only named development-tool exceptions; gameplay guard remains live. |

## Acceptance Reconciliation

- [x] All 12 tabs, floating/minimized chrome, mini-palette, F5/F6 overlays,
  footer, replay transport, and causality interactions mapped.
- [x] Every `InGameUICommands` field and every top-level
  `InGameUIFrameData` field inventoried.
- [x] Every current static keyboard binding mapped.
- [x] Concrete composition, command, replay, window/input, DX12, profiler, and
  allocation owners named.
- [x] Default dock, 1280 x 720 minimum, development configurations,
  Legacy/ImGui/Both selector, and multi-viewport deferral ratified.
- [x] Seven current screenshots captured and visually inspected.
- [x] No source behavior, scene, schema, baseline, golden, screenshot reference,
  or coverage floor changed. Repository validation is not required for E0.

Intentionally deferred controls: none. Intentionally legacy-only presentation
mechanics are the floating chrome, blur/cached-draw implementation, bottom-edge
reveal gesture, and draggable replay-cause window; their capabilities remain
available through docked ImGui destinations while the old pixels stay intact.
