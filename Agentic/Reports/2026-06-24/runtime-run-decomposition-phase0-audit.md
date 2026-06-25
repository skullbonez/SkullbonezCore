# Runtime Run Decomposition Phase 0 Audit

Date: 2026-06-24
Branch: `nightrunner-24th-june-refactor`
Base branch tip at start: `de9940c0`
Plan: `Agentic/Plans/runtime-run-decomposition-plan.md`

## Scope

Phase 0 is documentation-only. It records the current `Run` ownership shape
before code moves so later phase commits can be reviewed against a stable
baseline.

No runtime behavior changed in this phase.

## Worktree And Validation

- Startup status before branch creation was clean on `nightrunner-24th-june`.
- The requested branch `nightrunner-24th-june-refactor` was created from
  `de9940c0`.
- Phase 0 validation: documentation-only, no validation required.
- Intended Phase 1 gate: `tools\validate_fast.bat` because the first code slice
  is type plumbing for runtime render service/state views with no render order
  or scene behavior change.

## Baseline Metrics

Measured from `SkullbonezSource/Runtime/Run.h` before Phase 1 edits:

| Metric | Count | Notes |
|--------|-------|-------|
| `Run.h` total lines | 1870 | Includes comments and helper structs. |
| `#include` directives in `Run.h` | 38 | Broad runtime, render, physics, UI, and scene dependencies are pulled into the header. |
| Candidate direct `Run` member lines | 104 | Lines in the `Run` member block containing `m_`; multi-line declarations are counted by member-name line. |
| `Run` private method declaration starts | 226 | Heuristic count from the private method block. Multi-line declarations count once at the first return-type line when matched. |
| Private method semicolon lines | 238 | Upper-bound cross-check including continuations and declarations missed by return-type matching. |
| Nested render pass classes in `Run` | 13 | `FullscreenQuadPass` through `UiTextPass`. |
| Render pass objects owned directly by `Run` | 13 | `m_fullscreenQuadPass` through `m_uiTextPass`. |
| Cross-subsystem `friend` declarations in `Run.h` | 0 | Existing friend debt is outside `Run.h`. |
| Cross-subsystem friend debt found nearby | 10 | `GameModelCollection.h` has 5, `PhysicsWorld.h` has 4, and `PhysicsScene.h` has 1. `Camera.h` has one same-owner collection friendship. |

## Current Ownership Map

### App Shell

Members:

- CLI and startup override fields: `m_cmdTimeScaleOverride`,
  `m_cmdFixedStep`, `m_cmdSeedOverride`, `m_cmdNoWater`, `m_cmdNoSleep`,
  tornado/cinematic/demo/interactive/frame-count/UI-stress command fields,
  startup capacity/thread fields, and generated-object override fields.
- Composition and app-scope service state: `m_runtimeSettings`, `m_timers`,
  `m_systems`, `m_runtimeCommands`, `m_engineContext`, `m_runtimeViewModel`,
  and `sPerfPass`.

Private methods:

- App/frame orchestration: `Render`, `BindEngineContext`,
  `RefreshRuntimeViewModel`, `UpdateLogic`, `DrainRuntimeCommands`,
  `RegisterBuiltInAssets`, `ResolveSourceAssetPath`, `WindowScreenWidth`,
  `WindowScreenHeight`, and `MoveCamera`.

### Renderer

Members:

- `RunSubsystemState::renderPasses`.
- `m_dxrReflectionTransforms`.
- `m_fullscreenQuadPass`, `m_skyPass`, `m_sceneTargetPass`,
  `m_shadowPass`, `m_reflectionPass`, `m_objectPass`, `m_terrainPass`,
  `m_waterPass`, `m_tornadoVisualPass`, `m_debugOverlayPass`,
  `m_volumetricPass`, `m_tonemapPass`, and `m_uiTextPass`.

Private methods:

- `DrawPrimitives`, `BuildRenderFrameContext`, `ActiveCinematicConfig`,
  `IsCinematicRenderingEnabled`, `ReleaseBackendOwnedRenderResources`,
  `RebuildRegisteredRenderResources`, `LogRenderResourceLifecycleStep`,
  `Textures`, `TextureHandle`, `SelectRenderTexture`,
  `SetViewingOrientation`, `RenderEditorOverlay`,
  `RenderReplayPredictionGhosts`, and render-pose replay apply/restore
  helpers.

First members/methods intended to move:

- Phase 1 only introduces borrowed render service/state views for the render
  path and keeps behavior in `Run`.
- Phase 2A then targets the nested render pass class declarations.
- Phase 2B targets the 13 pass object members and `DrawPrimitives` scheduling.

### Scene

Members:

- `m_sceneController`, scene browser path/name arrays, scene-cycle key state,
  `m_selectedCineModeSceneIndex`, UI model/solver override fields,
  `m_requiredSceneContacts`, and `m_requiredBroadphaseXCells`.

Private methods:

- `SetUpCameras`, `SetUpCamerasFromScene`, `SetUpGameModels`,
  `SetUpSolverObjects`, `SetUpGameModelsFromScene`,
  `SetUpRequiredContactsFromScene`, `SetUpRequiredBroadphaseXCellsFromScene`,
  `UpdateRequiredSceneContacts`, `UpdateRequiredSceneBroadphaseXCells`,
  `RequiredSceneContactsComplete`, `RequiredSceneBroadphaseXCellsComplete`,
  `RefreshSceneBrowserList`, `CurrentSceneBrowserIndex`, `CreateSceneFromUI`,
  `LoadSceneFromBrowserIndex`, `LoadDemoSceneFromUI`,
  `ApplyCinematicModeFromBrowserIndex`, `ApplyAdjacentCinematicMode`,
  `ApplyLiveStyleScene`, `LoadAdjacentSceneFromBrowser`,
  `CaptureSceneRuntimeResetSnapshot`, `RestoreSceneRuntimeResetSnapshot`,
  `ClearSceneRuntimeUIOverrides`, `LoadScene`, `ResetCurrentScene`,
  `ApplyUIModelCountOverride`, `ApplyUISolverObjectCounts`, and
  `AdvanceScene`.

### Replay

Members:

- `m_replay`, `m_solverReplay`, `m_replayEvents`, `m_replayBranch`,
  `m_loadedPresentationReplay`, `m_replayScrubber`, `m_replayCamera`,
  `m_replayPathVisualizer`, `m_replayPrediction`, `m_replayCauseTree`,
  `m_replayVelocityEdit`, `m_replayFocusModelMask`,
  `m_replayPoseBackups`, launcher-visual replay backup fields, mismatch
  reporting fields, and debug replay probe fields.

Private methods:

- `ClearReplayInteractionForRuntimeTransition`,
  `HasActiveReplayInteractionState`, `SaveReplayBufferFromScrubber`,
  `PromptLoadReplayPresentationArtifact`,
  `ResetReplayTimelineForActiveScene`, `NextReplayEventFrameIndex`,
  all `RecordReplay*` helpers, replay capture thunks, replay launcher visual
  sample helpers, restore-target application, replay path/prediction/cause
  tree/velocity edit helpers, replay scrubber helpers, replay render-pose
  apply/restore helpers, solver restore/hash helpers, and debug replay probes.

### Tools

Members:

- `m_rayCastTest`, `m_mousePickup`, `m_editor`, `m_editorTracer`, and
  `m_launcherLaser`.

Private methods:

- Launcher and ray-test methods: `ClearRayCastTestLines`,
  `AddRayCastTestLine`, `TickRayCastTestLines`, `TryRayCastTestHit`,
  `TryLauncherTerrainHit`, `FireRayCastTest`, `FireLauncherLaser`,
  and `FireLauncherProjectile`.
- Manipulator methods: `TryBuildMouseWorldRay`, `TryPickMousePickupModel`,
  `CancelMousePickup`, `TickMousePickupInput`,
  `ApplyMousePickupPhysicsStep`, and `RestoreMousePickupAngularVelocity`.
- Editor methods: terrain placement, preview, mode, keyboard, UI command,
  world click, save hotkey, pick, transform-gizmo, rotation-gizmo, placement,
  and editable scene helpers.

### Diagnostics

Members:

- `m_diagnostics`, `m_capture`, `m_liveStyle`, `m_debug`, `m_uiStress`,
  `m_broadphaseVisualizer`, `m_collisionVisualizer`, and
  `m_physicsDebugVisualizer`.

Private methods:

- `SaveScreenshot`, `LogPerfMemory`, `TickScreenshots`,
  `TickLiveStyleControl`, `TickLiveStyleControlCapture`, `TickAutoCycle`,
  `TickPerfLog`, UI stress random/action helpers, physics-debug pipeline
  stepping, debug overlay toggles, `BeginPhysicsDiagnosticsRun`,
  `EndPhysicsDiagnosticsRun`, and `LogSceneFinished`.

### Physics And World

Members:

- `m_cWorldEnvironment`, `m_cGameModelCollection`, physics fields inside
  `m_runtimeSettings`, physics debug visualizers, and scene contact/broadphase
  gates.

Private methods:

- `TickPhysics`, `AfterPhysicsStep`, `ApplyUIWorldOverride`,
  `ApplyConfiguredWorldEnvironment`, `ApplyNoWaterOverride`,
  `ApplyTornadoDefaultsForActiveScene`, `SyncTornadoFieldToPhysics`,
  `UseDefaultTerrain`, `UseFlatSlopeTerrain`, `UpdateWorldTerrainBounds`,
  `SetUpSolverObjects`, physics portions of scene setup, and replay solver
  snapshot/restore helpers.

### UI And Input Bridge

Members:

- `m_runtimeInput`, `m_interaction`, `m_camera`, `m_UI`, cursor/mode state
  embedded in `RunCameraState`, and scene-cycle key state.

Private methods:

- `TakeInput`, `UpdateRuntimeInputModeAfterAction`,
  `EnterInteractionForCameraMode`, `ApplyRuntimeInteractionTransitionCleanup`,
  `ClearEditorInteractionForRuntimeTransition`,
  `HasActiveEditorInteractionState`, `InspectGizmoInteractionActive`,
  `ReplayInspectionActive`, `ReplayInspectionMouseLookActive`,
  `MouseLookOwnsCursor`, `ShouldHideNativeCursor`, `ApplyCursorOwnership`,
  `ReleaseMouseToUI`, `EnterFlyModeCamera`, `ExitFlyModeCamera`,
  `CameraModeLabel`, `CameraModeEnabledMask`,
  `IsDemoCameraModeAvailable`, `NormalizeCameraModeForCurrentScene`,
  `IsFlyCameraMode`, `IsLauncherCameraMode`, `IsManipulatorCameraMode`,
  `ApplyCameraMode`, and `CycleCameraMode`.

## Phase 1 Guardrail

The next phase should not change pass order, scene loading, replay state, tool
input behavior, or physics stepping. Its expected diff is limited to explicit
borrowed service/view structs near runtime render code and construction of those
views from `Run`.

If Phase 1 touches render output paths or moves pass bodies, escalate the gate
from `tools\validate_fast.bat` to `tools\validate_dx12_renderer.bat`.
