# 01 — Run God-Object Decomposition

Date: 2026-07-08
Status: In Progress
Priority: P0
Owner: Runtime
Source issue: audit iss-01 (severity 5)

## Problem

`Run` is one class that owns and implements ~40 subsystems — window, cameras,
physics, replay, UI, editor, audio, stress fuzzer, launch options, debug toggles
— and is only kept openable by splitting `Run::` across ~16 translation units
(`Run.cpp`, `RunInput.cpp`, `RunFrame.cpp`, `RunRender.cpp`, `RunScene.cpp`,
`RunStress.cpp`, `RunCapture.cpp`, `RunLiveStyle.cpp`, plus `.inl` includes).

Verified evidence:

- [`Run::TakeInput()`](../SkullbonezSource/Runtime/RunInput.cpp:1576) spans
  L1576→~3240 (**~1,664 lines**; next member `DrainRuntimeCommands` at L3241),
  hand-branching every key and poking 25+ subsystem members with no
  keybinding/command table.
- [`RunState.h`](../SkullbonezSource/Runtime/RunState.h) is a shared "state
  shelf" whose own header (L9-10) concedes it is *"a staging boundary, not a
  destination,"* aggregating 250+ mutable public fields reached directly from
  across the Run files (e.g. `m_camera.autoCycleAccum += simulationDt` in
  `RunFrame.cpp`).

This is the flagship amateur symptom of the codebase.

## Goal

`Run` becomes a thin launcher + frame coordinator. Lifecycle, input, scene,
camera, capture, diagnostics, editor, and render policy move to real owners that
hold their own state behind narrow APIs.

## Approach

- [x] **Phase 0 — Inventory.** List every `Run` member and method; classify each
  by owner (input / scene / camera / capture / diagnostics / replay / editor /
  stress / render-policy). Output a one-page ownership map.
- [ ] **Phase 1 — Kill `TakeInput()`.** Replace hand-branching with a data-driven
  binding table: `struct KeyBinding { Key key; InputAction action; ContextMask
  contexts; }`. A single dispatch loop maps pressed keys → actions; each action
  handler lives in its owning subsystem. This one change removes the 1,664-line
  function.
- [ ] **Phase 2 — Move state shelves out of `RunState`.** Relocate each shelf's
  fields into the owner that mutates them; `RunState` shrinks toward empty.
  Delete cross-subsystem field pokes.
- [ ] **Phase 3 — Shrink `Run`.** Reduce it to `Initialise` / `Run` / `Shutdown`
  plus per-frame tick coordination that calls owners.

## Risks

- Input is entangled with editor/replay/camera modes; migrate one context at a
  time. Interaction-automation reports are the behavior guard — they must stay
  green across every phase.

## Step-by-step implementation

This is the largest plan — go slowly, one action-group at a time. The
**interaction-automation suite is your behavior guard**: it must stay green after
every step. Commit per step.

### Phase 0 — Inventory

- [x] **0.1** Produce the `Run` ownership map: list every `Run` member and method
  and classify each by owner (input / scene / camera / capture / diagnostics /
  replay / editor / stress / render-policy). Save it here as a sub-list. No code
  change.

  Ownership map completed 2026-07-08 from `Run.h` declarations and the current
  `Run::` definition spread. `Run.h` declares 40 fields when the debug-only
  `m_replayProbes` and static `sPerfPass` are included. Overloads are called out
  when a shared method name has more than one declaration. No repository
  validation required; documentation-only inventory.

  - **Runtime shell / launch coordination.**
    Members: `m_config`, `m_launchOptions`, `m_defaultCinematicRender`,
    `m_startup`, `m_runtimeSettings`, `m_timers`, `m_runtimeCommands`,
    `m_runtimeViewModel`, `sPerfPass`.
    Methods: `Run`, `~Run`, `Initialise`, `Execute`, `LastSceneLoadResult`,
    `RunSceneLoadOnly`, `SetTimeScaleOverride`, `SetFixedStepOverride`,
    `SetSeedOverride`, `SetInteractiveRunOverride`, `SetFrameCountOverride`,
    `SetAllocationGuardMode`, `SetInitialOverlayMode`, `SetTopTextHidden`,
    `RefreshRuntimeViewModel`, `DrainRuntimeCommands`, `UpdateLogic`,
    `TickScreenshots`, `TickAutoCycle`, `TickSceneAdvance`, `SetViewingOrientation`.
  - **Scene / authored content / asset library.**
    Members: `m_sceneController`, `m_sceneCoordinator`, `m_lastSceneLoadResult`,
    `m_requiredSceneContacts`, `m_requiredBroadphaseXCells`.
    Methods: `SceneState` (mutable and const overloads), `LoadScene`,
    `SaveCurrentSceneDefaults`, `EnterInteractiveSceneRun`,
    `CanSceneAutomationQuit`, `HoldCompletedInteractiveScene`,
    `UpdateRequiredSceneContacts`, `UpdateRequiredSceneBroadphaseXCells`,
    `RequiredSceneContactsComplete`, `RequiredSceneBroadphaseXCellsComplete`,
    `RegisterBuiltInAssets`, `ResolveSourceAssetPath`,
    `SetGeneratedObjectTypeOverride`.
  - **Input / runtime interaction / cursor ownership.**
    Members: `m_inputLatches`, `m_runtimeInput`, `m_interaction`,
    `m_interactionAutomation`.
    Methods: `TakeInput`, `TickInteractionAutomationBeforeInput`,
    `TickInteractionAutomationAfterRender`, `ClearInteractionAutomationInput`,
    `WriteInteractionAutomationReport`, `TryFindInteractionAutomationModel`,
    `TrySetInteractionAutomationReplayPathTarget`,
    `TryProjectInteractionAutomationModel`, `StepPhysicsPipelineStage`,
    `UpdateRuntimeInputModeAfterAction`, `BuildRuntimeInputSnapshot`,
    `RouteRuntimePointerInput`, `EnterInteractionForCameraMode`,
    `ApplyRuntimeInteractionTransitionCleanup`,
    `SetWorldInteractionOwnerAfterInteractionTransition`,
    `ExecuteRuntimeInteractionCommand`, `PublishRuntimeInteractionEvent`,
    `ClearRuntimeInteractionStateForTransition`, `MouseLookOwnsCursor`,
    `ShouldHideNativeCursor`, `ApplyCursorOwnership`, `ReleaseMouseToUI`.
  - **Camera / camera modes / attached follow.**
    Members: `m_camera`, `m_attachedCamera`.
    Methods: `RelativeUpdateCamera`, `MoveCamera`, `CancelCameraLookGesture`,
    `SyncCameraLookGesture`, `EnterFlyModeCamera`, `ExitFlyModeCamera`,
    `CameraModeLabel`, `CameraModeEnabledMask`, `IsDemoCameraModeAvailable`,
    `NormalizeCameraModeForCurrentScene`,
    `SetCameraModeLabelAfterInteractionTransition`, `IsManualCameraMode`,
    `IsFlyCameraMode`, `IsLauncherCameraMode`, `IsManipulatorCameraMode`,
    `IsAttachedCameraMode`, `ApplyCameraMode`, `CycleCameraMode`,
    `ResetAttachedCamera`, `CaptureAttachedCameraReturnState`,
    `RestoreAttachedCameraReturnState`, `TryResolveAttachedCameraTarget`,
    `SetAttachedCameraTarget`, `ClearAttachedCameraTarget`,
    `SeedAttachedCameraTargetFromSelection`, `TryPickAttachedCameraTargetFromMouse`,
    `TickAttachedCameraWorldClick`, `CycleAttachedCameraSubmode`,
    `ToggleAttachedCameraPin`, `TickAttachedCameraOrbitInput`,
    `TickAttachedCamera`, `CaptureAttachedCameraFixedOffset`,
    `CaptureAttachedCameraOrbit`, `TryResolveAttachedCameraRagdollHead`.
  - **Replay / replay inspection / solver restore.**
    Members: `m_replayRuntime`, `m_replayLauncherVisualScratch`,
    `m_solverReplayMismatch`, `m_replayProbes` (debug only).
    Methods: `SetReplayRecording`, `LoadReplayPresentationArtifact`,
    `ResetReplayTimelineForActiveScene`, `BeginReplayToolGesture`,
    `EndReplayToolGesture`, `CancelReplayToolGesture`,
    `CancelReplayToolDragState`, `ClearReplayInteractionForRuntimeTransition`,
    `HasActiveReplayInteractionState`, `TryPickReplayPathTargetFromMouse`,
    `RenderReplayPathVisualizer`, `TickReplayCauseTreeInput`,
    `RenderReplayCauseFocusOverlay`, `TickReplayVelocityEditInput`,
    `RenderReplayVelocityEditOverlay`, `EnterReplayInspectionCamera`,
    `ExitReplayInspectionCamera`, `TickReplayScrubberInput`,
    `RestoreReplayScrubberSelectionAsLive`, `ApplyReplaySolverSampleState`,
    `CaptureCurrentReplaySolverHash`, `RestoreReplayV2ArtifactTargetState`,
    `RestoreReplaySolverSampleAsLive`, `SetReplayScrubProbe`,
    `SetReplayRestoreProbe`, `SetReplaySaveProbe`, `TickReplayScrubProbe`,
    `TickReplayRestoreProbe`, `TickReplaySaveProbe`, `RecordReplayProbeFailure`,
    `ReplayProbeFailed`, `ReplayProbeFailureOwner`,
    `ReplayProbeFailureMessage`, `VerifyLoadedReplayPresentationProbe`,
    `VerifyReplaySolverCheckpointFileProbe`, `VerifyReplaySolverTargetFileProbe`,
    `VerifyReplaySolverBranchFileProbe`, `VerifyReplaySolverFailureFileProbe`.
  - **Editor tools / gizmos / mouse pickup.**
    Members: `m_runtimeTools`.
    Methods: `ClearEditorInteractionForRuntimeTransition`,
    `HasActiveEditorInteractionState`, `InspectGizmoInteractionActive`,
    `TryBuildMouseWorldRay`, `TickEditorViewportAndPlacementScaleInput`,
    `TickEditorWorldClick`, `CancelMousePickup`, `TickMousePickupInput`,
    `ApplyMousePickupPhysicsStep`, `RestoreMousePickupAngularVelocity`.
  - **Render policy / backend resources / capture / live style.**
    Members: `m_systems`, `m_renderBackendView`, `m_renderer`, `m_liveStyle`.
    Methods: `BuildRuntimeRendererBindings`, `ReleaseBackendOwnedRenderResources`,
    `RebuildRegisteredRenderResources`, `LogRenderResourceLifecycleStep`,
    `Render`, `SaveScreenshot`, `SetCinematicRenderingOverride`,
    `SetCinematicShadowsOverride`, `SetDemoHeroStyleOverride`,
    `SetLiveStyleControlDirectory`, `TickLiveStyleControl`,
    `TickLiveStyleControlCapture`, `DumpTextureAssets`.
  - **Diagnostics / UI debug visualization.**
    Members: `m_diagnosticsRuntime`, `m_UI`, `m_debug`,
    `m_broadphaseVisualizer`, `m_collisionVisualizer`,
    `m_physicsDebugVisualizer`.
    Methods: `SetBroadphaseVisualizerEnabled`, `SetPhysicsDebugFlagsOverride`,
    `SetPhysicsDebugTransparentOverride`, `SetPhysicsDebugAlphaOverride`,
    `SetPhysicsDebugContactLingerOverride`, `SetPhysicsRegressionLogOverride`,
    `SetPhysicsCollisionTimeLogOverride`, `SetPhysicsDiagnosticsPath`,
    `LogSceneFinished`, `BeginPhysicsDiagnosticsRun`, `EndPhysicsDiagnosticsRun`,
    `SetMainMemoryDumpPath`.
  - **Stress / deterministic fuzzing.**
    Members: `m_graphicsStress`.
    Methods: `SetUIStressOverride`, `SetGraphicsStressOverride`,
    `RunUIStressActions`, `RunGraphicsStressActions`.
  - **Physics / world / contact audio.**
    Members: `m_simulation`, `m_contactAudio`, `m_cWorldEnvironment`,
    `m_cGameModelCollection`.
    Methods: `SetNoWaterOverride`, `SetNoSleepOverride`,
    `SetNoContactAudioOverride`, `SetTornadoOverride`,
    `SetTornadoVectorFieldOverride`, `TickPhysics`, `AfterPhysicsStep`,
    `UpdateWaterHeightControls`.

### Phase 1 — Kill `TakeInput()` (the flagship)

- [x] **1.1** Define the binding types: `enum class InputAction` covering the ~38
  keys `TakeInput()` branches on, and `struct KeyBinding { Key key; InputAction
  action; ContextMask contexts; }`. No behavior yet. Build. Commit.

  Completion note (2026-07-08): `RuntimeInputAction` already carried the action
  vocabulary, so the code slice added the reusable key/action/context metadata
  around it: `RuntimeInputContextMask`, `RuntimeInputBindingContext`, and
  `RuntimeInputKeyBinding`. The existing local key-memory table now uses the
  shared binding shape but still only updates action-down state; no dispatch
  behavior changed. Build log:
  `TestOutput\agent_logs\plan01_step1_1_build_profile.log` (10.0s), 0 warnings,
  0 errors.
- [x] **1.2** Build the static binding table (data) that reproduces the current
  key→action mapping **exactly**, including context conditions (fly/launcher/
  director/replay modes). No dispatch yet. Build. Commit.

  Completion note (2026-07-08): `kTakeInputKeyboardBindings` now holds the
  current key/action rows and context metadata for keyboard-unblocked actions,
  launcher-only actions, attached-camera actions, director authoring, debug
  launcher repro snapshots gated by replay restore state, UI-after-update ESC,
  capture hotkeys, and the scene-only Backspace reset. The table is still only
  used by the existing action-down memory sync; `TakeInput` dispatch remains the
  old branch code. Build log:
  `TestOutput\agent_logs\plan01_step1_2_build_profile.log` (11.4s), 0 warnings,
  0 errors.
- [ ] **1.3** Replace `TakeInput()`'s hand-branching with a dispatch loop over
  the table, calling one handler per action. Move each action's body into its
  **owning subsystem's** handler — do this **one action-group at a time**,
  keeping behavior identical. Gate: interaction-automation suite +
  `validate_full`. Commit per group. Repeat until `TakeInput()` is only the loop.

  Partial progress:
  - [x] Camera-mode keyboard group (Tab/F/N/M/F1/Enter) now dispatches by looping
    over `kTakeInputKeyboardBindings`; the old branch bodies for camera cycling,
    fly/launcher mode, launcher fire-mode cycling, and attached-camera submode/pin
    were moved behind that table-driven group. Non-camera actions still use their
    existing branches, so this does not complete step 1.3. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_camera_interaction_clicks_rerun.log`
    (19.8s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_camera_validate_full_rerun.log`
    (50.7s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly). An
    earlier `validate_full` attempt failed on `RunInput.cpp` formatting only;
    the file was formatted narrowly with clang-format before the rerun.
  - [x] Director keyboard group (B/J/K/L) now dispatches through the same table
    loop for Director grab/release, phase pose capture, phase stepping, and shot
    list save. Edge capture still happens before Director/authoring context checks,
    and non-Director actions remain on existing branches. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_director_interaction_clicks.log`
    (19.7s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_director_validate_full.log` (50.3s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
  - [x] Diagnostics/visualization keyboard group (1/2/3/4/5/6/C/G/O/P/Q/F7/F8/V)
    now dispatches through the same table loop for water debug modes, terrain and
    water visibility, collision visualization, physics debug overlays, physics
    pipeline stepping, cross-scene pause lock, the retired renderer-switch report,
    and broadphase overlay/track cycling. Non-diagnostics actions remain on
    existing branches, so this does not complete step 1.3. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_diagnostics_interaction_clicks.log`
    (14.4s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_diagnostics_validate_full.log` (50.6s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
  - [x] UI overlay keyboard group (0/F5/F6) now dispatches through the same table
    loop for diagnostics-window visibility, marker histogram visibility, and memory
    waterline visibility. The handlers preserve their cursor-ownership updates and
    mode-action bookkeeping; remaining non-UI actions stay on existing branches.
    Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_ui_overlay_interaction_clicks.log`
    (19.7s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_ui_overlay_validate_full.log` (50.4s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
  - [x] Debug launcher repro keyboard group (Enter in launcher mode, Debug only)
    now dispatches through the same table loop while preserving the replay-restore
    consumption guard and leaving the Profile build inert. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_launcher_repro_interaction_clicks.log`
    (19.8s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_launcher_repro_validate_full.log`
    (50.2s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
  - [x] Scene navigation keyboard group (Left/Right) now dispatches through the
    same table loop. The shared scene-control helper was hoisted without changing
    its body, and the new dispatch case preserves the cinematic-tab-first behavior
    before falling back to adjacent scene loading. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_scene_nav_interaction_clicks.log`
    (19.9s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_scene_nav_validate_full.log` (50.4s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
  - [x] Editor-mode keyboard group (backtick) now dispatches through the same
    table loop while preserving the existing delayed merge with UI editor-toggle
    commands. Alt/replay velocity edit handling remains on the existing branch.
    Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_editor_mode_interaction_clicks.log`
    (19.2s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_editor_mode_validate_full.log` (50.3s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
  - [x] After-UI escape keyboard group (Esc) now dispatches through a table-filtered
    after-UI loop at the original post-UI location, preserving focused-control
    first refusal and the quick double-tap quit path. Gate evidence:
    `TestOutput\agent_logs\plan01_step1_3_escape_interaction_clicks.log` (19.9s,
    both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_step1_3_escape_validate_full.log` (50.2s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly).
	  - [x] Late reset keyboard group (R/Backspace) now dispatches through a
	    table-filtered late loop at the original post-save-hotkey location, preserving
	    the scene-mode-only Backspace alias. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_reset_interaction_clicks.log` (20.0s,
	    both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_reset_validate_full.log` (50.5s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
	    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
	    `physics_regression_solver.csv` matched byte-exactly).
	  - [x] Capture keyboard group (F2/F3) now dispatches through a capture-context
	    table loop at the original post-UI location. The table supplies the key/action
	    pair, while `EditorTools` still owns the scene-snapshot and screenshot command
	    side effects through a single-action helper. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_capture_interaction_clicks.log` (23.4s,
	    both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_capture_validate_full.log` (53.4s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
	    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
	    `physics_regression_solver.csv` matched byte-exactly). An earlier
	    `validate_full` attempt failed on `EditorTools.cpp` formatting only; the
	    touched C++ files were formatted narrowly with clang-format before the rerun.
	  - [x] Editor tool / replay Alt keyboard group now routes the `VK_MENU`
	    `ToggleEditorTool` row through the same table loop. The dispatch pass captures
	    the Alt down/press state from the table row, then the existing post-loop
	    editor/replay branch consumes that state in its original order for editor
	    placement toggles and replay velocity editing. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_editor_tool_interaction_clicks_rerun.log`
	    (19.7s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_editor_tool_validate_full_rerun.log`
	    (50.4s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly). Earlier
	    non-rerun gates passed too, but were superseded by a small cleanup that removed
	    the defensive hardcoded Alt fallback from the unblocked dispatch path.
	  - [x] Binding coverage audit: a structured scan of
	    `kTakeInputKeyboardBindings` found 37 unique table actions and 37 dispatched
	    `case RuntimeInputAction::...` labels, with no missing table actions. Step
	    1.3 remains open because the next slice still has to extract the handlers out
	    of `TakeInput()` and make the function small enough for step 1.4.
	  - [x] Frame-gate helper extraction moved the focus-loss reset path and the
	    post-UI capture/reset keyboard dispatch into named `RunInput.cpp` helpers.
	    `TakeInput()` span dropped to 1,586 lines, so this is structural progress
	    but not step 1.3 or 1.4 completion. A first draft with five new private
	    helpers failed the runtime-boundary ratchet; the final slice keeps only two
	    helpers and leaves the ratchet green. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_frame_gate_extract_interaction_clicks_rerun.log`
	    (24.1s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_frame_gate_extract_validate_full_rerun.log`
	    (54.1s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	  - [x] Diagnostics ownership extraction moved the numeric/F-key diagnostics
	    keyboard handlers and physics-pipeline cursor stepping out of `Run` and into
	    the diagnostics runtime boundary. `Run::StepPhysicsPipelineStage` was deleted,
	    so the private `Run` method count dropped by one while `TakeInput()` shrank to
	    1,457 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_diagnostics_owner_interaction_clicks.log`
	    (15.0s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_diagnostics_owner_validate_full_rerun.log`
	    (65.3s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly). A
	    superseded targeted Profile build caught a `RuntimeInputContext` forward
	    declaration mismatch, and the first `validate_full` attempt failed only on the
	    diagnostics header formatting post-pass; both were fixed before the final
	    rerun.
	  - [x] Diagnostics UI ownership extraction moved the 0/F5/F6 diagnostics-window,
	    marker histogram, and memory overlay keyboard state changes out of
	    `Run::TakeInput()` and into the diagnostics runtime boundary. `Run` now keeps
	    only the cursor refresh and runtime-input action bookkeeping for that group,
	    and the touched-file comment audit removed a stale baseline-term glossary row
	    from `RunInput.cpp`. `TakeInput()` now spans 1,442 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_ui_owner_interaction_clicks.log`
	    (20.0s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_ui_owner_validate_full.log` (53.6s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
	    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
	    `physics_regression_solver.csv` matched byte-exactly). Targeted pre-gate
	    checks also passed:
	    `TestOutput\agent_logs\plan01_ui_shortcut_runtime_boundaries.log` (19.3s) and
	    `TestOutput\agent_logs\plan01_ui_shortcut_build_profile.log` (10.5s).
	  - [x] Launcher repro ownership extraction moved the debug-only Enter status
	    formatting and repro-snapshot feedback window out of `Run::TakeInput()` and
	    into `RuntimeTools`, next to the snapshot writer. The input loop now keeps
	    only the Debug/Profile, launcher-mode, and replay-restore guards before
	    delegating to the launcher owner. The touched-file comment audit also fixed
	    glossary wrapping in `LauncherTools.cpp`. `TakeInput()` now spans 1,423
	    lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_launcher_repro_owner_interaction_clicks.log`
	    (19.2s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_launcher_repro_owner_validate_full.log`
	    (54.3s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_launcher_repro_owner_runtime_boundaries.log`
	    (18.5s) and
	    `TestOutput\agent_logs\plan01_launcher_repro_owner_build_profile.log`
	    (10.7s).
	  - [x] Sound-tab UI command extraction moved contact-audio enable/disable,
	    debug counter, flash-mode, simple-mode, sound-set, sound-band, sample preview,
	    and sample-selection command handling out of `Run::TakeInput()` and into the
	    `RuntimeTuning` UI-adapter boundary. `Run` now passes a borrowed
	    `SoundUICommandContext` and only records the input-mode action when the
	    tuning adapter reports a change. The touched-file comment audit updated
	    `RuntimeTuning`'s learning headers so audio delegates to bounded service
	    setters instead of claiming all helpers clamp directly. `TakeInput()` now
	    spans 1,232 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_sound_ui_owner_interaction_clicks.log`
	    (22.4s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_sound_ui_owner_validate_full.log`
	    (50.7s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_sound_ui_owner_runtime_boundaries.log`
	    (17.4s) and
	    `TestOutput\agent_logs\plan01_sound_ui_owner_build_profile.log` (8.4s).
	  - [x] Tornado Physics-tab UI command extraction moved tornado enable/disable,
	    visual-shell, field-vector, and radius/height/inward/swirl/lift setting
	    handling out of `Run::TakeInput()` and into the `RuntimeTuning` UI-adapter
	    boundary. `Run` now passes a borrowed `TornadoUICommandContext` and only
	    records the input-mode actions reported by `TornadoUICommandResult`, while
	    the helper owns clamping and the single runtime-settings-to-physics sync.
	    The touched-file comment audit updated `RuntimeTuning`'s learning headers
	    and added the borrowed-context lifetime note. `TakeInput()` now spans 1,151
	    lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_tornado_ui_interaction_clicks.log`
	    (16.0s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_tornado_ui_validate_full_rerun.log`
	    (58.1s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly). A
	    first targeted Profile build caught missing `UI::Layout` qualification for
	    the clamp constants, and the first `validate_full` attempt failed only on
	    formatting for the two touched `.cpp` files; both were fixed before the final
	    reruns. Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_tornado_ui_runtime_boundaries_rerun.log`
	    (17.4s), `TestOutput\agent_logs\plan01_tornado_ui_build_profile_rerun.log`
	    (6.2s), and
	    `TestOutput\agent_logs\plan01_tornado_ui_validate_format_after_fix.log`
	    (9.1s).
	  - [x] Physics diagnostic UI command extraction moved collision visualizer,
	    debug-flag mask toggles, pipeline stage stepping, transparent debug bodies,
	    broadphase overlay, terrain-contact probe, debug alpha, and contact-linger
	    command handling out of `Run::TakeInput()` and into the diagnostics runtime
	    boundary. `Run` keeps the original input-mode action ordering at the same
	    call sites, while `DiagnosticsRuntime` now owns the debug presentation field
	    mutations. The touched-file comment audit added the Physics diagnostic
	    command glossary row to `DiagnosticsRuntime`'s learning headers. `TakeInput()`
	    now spans 1,145 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_step1_3_physics_debug_ui_interaction_clicks.log`
	    (16.1s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_step1_3_physics_debug_ui_validate_full.log`
	    (52.2s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_physics_debug_ui_validate_format.log` (9.2s),
	    `TestOutput\agent_logs\plan01_physics_debug_ui_runtime_boundaries.log`
	    (17.4s), and
	    `TestOutput\agent_logs\plan01_physics_debug_ui_build_profile.log` (10.1s).
	  - [x] Physics runtime/tool UI command extraction moved Physics-tab sleep
	    policy, raycast visualization, launcher impulse/projectile-speed tuning,
	    and terrain/object/rolling friction commands out of `Run::TakeInput()` and
	    into `RuntimeTuning`/`RuntimeTools`. `RuntimeTools` returns replay payload
	    snapshots so same-frame launcher slider edits still record config events in
	    the original order, while `RuntimeTuning` owns live friction config syncing
	    through `GameModelCollection`. The touched-file comment audit added the
	    launcher replay-payload invariant and friction action-count invariant.
	    `TakeInput()` now spans 1,108 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_physics_runtime_tools_interaction_clicks.log`
	    (15.7s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_physics_runtime_tools_validate_full.log`
	    (52.2s; project filters/runtime boundaries passed, Profile/Debug builds had
	    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_physics_runtime_tools_validate_format_post_comments.log`
	    (9.0s), `TestOutput\agent_logs\plan01_physics_runtime_tools_runtime_boundaries.log`
	    (17.4s), and `TestOutput\agent_logs\plan01_physics_runtime_tools_build_profile.log`
	    (10.0s).
	  - [x] Presentation/render/water UI command extraction moved text-only,
	    terrain/water visibility, water freeze/flat, scene/render shadow toggles,
	    render default save intent, render-tuning sliders, and water reflection mode
	    handling out of `Run::TakeInput()` and into `RuntimeTuning`. Fixed-step
	    simulation reset intentionally stays in `RunInput` for a later simulation
	    owner slice. The touched-file comment audit added the presentation command
	    glossary/context notes and the accepted-command result invariant. `TakeInput()`
	    now spans 1,077 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_presentation_ui_interaction_clicks.log`
	    (15.8s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_presentation_ui_validate_full.log` (52.3s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
	    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
	    `physics_regression_solver.csv` matched byte-exactly). Targeted pre-gate
	    checks also passed:
	    `TestOutput\agent_logs\plan01_presentation_ui_validate_format_post_comments.log`
	    (9.1s), `TestOutput\agent_logs\plan01_presentation_ui_runtime_boundaries.log`
	    (17.4s), and `TestOutput\agent_logs\plan01_presentation_ui_build_profile.log`
	    (10.5s).
- [ ] **1.4** Confirm `TakeInput()` is under ~200 lines (setup + dispatch loop).

### Phase 2 — Move state shelves out of `RunState`

- [ ] **2.1** For **one shelf at a time**, relocate its fields into the owner
  that mutates them and remove cross-file pokes (e.g. `m_camera.autoCycleAccum`
  from `RunFrame`). Gate: `validate_full`. Commit per shelf.

### Phase 3 — Shrink `Run`

- [ ] **3.1** Reduce `Run` to `Initialise` / `Run` / `Shutdown` plus per-frame
  tick coordination that calls owners. It should no longer implement subsystem
  logic. Gate: `validate_full`. Commit.

## Validation

`tools\validate_full.bat` (Run/Runtime changes). Interaction-automation suite
after each phase.

## Acceptance (structural)

- [ ] No single `Run` function exceeds ~200 lines; `TakeInput` is a table +
  dispatch loop.
- [ ] `Run` public-method and owned-member counts drop materially from the
  audit baseline (~60 public methods, ~40 members).
- [ ] `RunState` field count is measurably reduced; no external file mutates a
  `RunState` sub-field directly.
- [ ] `Run` no longer implements subsystem logic — it coordinates owners.
