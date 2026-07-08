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
- The former `RunState.h` shared "state shelf" aggregated 250+ mutable public
  fields reached directly from across the Run files (for example,
  `m_camera.autoCycleAccum += simulationDt` in `RunFrame.cpp`). Phase 2 has now
  deleted that staging header and moved its shelves into narrower owner headers.

This is the flagship amateur symptom of the codebase.

## Goal

`Run` becomes a thin launcher + frame coordinator. Lifecycle, input, scene,
camera, capture, diagnostics, editor, and render policy move to real owners that
hold their own state behind narrow APIs.

## Approach

- [x] **Phase 0 — Inventory.** List every `Run` member and method; classify each
  by owner (input / scene / camera / capture / diagnostics / replay / editor /
  stress / render-policy). Output a one-page ownership map.
- [x] **Phase 1 — Kill `TakeInput()`.** Replace hand-branching with a data-driven
  binding table: `struct KeyBinding { Key key; InputAction action; ContextMask
  contexts; }`. A single dispatch loop maps pressed keys → actions; each action
  handler lives in its owning subsystem. This one change removes the 1,664-line
  function.
- [x] **Phase 2 — Move state shelves out of `RunState`.** Relocate each shelf's
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
- [x] **1.3** Replace `TakeInput()`'s hand-branching with a dispatch loop over
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
	  - [x] Cinematic-tab UI command extraction moved cinematic rendering toggle,
	    sky-default save intent, style-scene selection, feature toggles, and
	    parameter sliders out of `Run::TakeInput()` and into `RuntimeTuning`.
	    `RunInput` still owns the interactive-scene transition before mode
	    selection so scene flow remains in the same order. The touched-file comment
	    audit added the request-vs-load-result invariant for cinematic mode
	    selection and the accepted-command result invariant. `TakeInput()` now
	    spans 1,057 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_cinematic_ui_interaction_clicks.log` (15.7s,
	    both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_cinematic_ui_validate_full.log` (51.2s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0 warnings
	    and 0 errors, DX12 InfoQueue errors = 0, screenshots matched baselines, and
	    `physics_regression_solver.csv` matched byte-exactly). Targeted pre-gate
	    checks also passed:
	    `TestOutput\agent_logs\plan01_cinematic_ui_validate_format_post_comments.log`
	    (9.1s), `TestOutput\agent_logs\plan01_cinematic_ui_runtime_boundaries.log`
	    (17.5s), and `TestOutput\agent_logs\plan01_cinematic_ui_build_profile.log`
	    (8.3s).
	  - [x] Water-tab world UI command extraction moved gravity, fluid-height,
	    and fluid-density request/clamp/replay recording out of `Run::TakeInput()`
	    and into `RuntimeTuning`. The touched-file comment audit added the partial
	    request invariant so unedited fields keep their current world values.
	    `TakeInput()` now spans 1,043 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_world_water_ui_interaction_clicks.log`
	    (15.5s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_world_water_ui_validate_full.log` (50.8s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_world_water_ui_validate_format_rerun.log`
	    (9.3s), `TestOutput\agent_logs\plan01_world_water_ui_runtime_boundaries.log`
	    (17.4s), and `TestOutput\agent_logs\plan01_world_water_ui_build_profile.log`
	    (8.4s).
	  - [x] Run-tab scalar UI command extraction moved time-scale, random-seed,
	    and worker-thread request handling out of `Run::TakeInput()` and into
	    `RuntimeTuning`. `RunInput` now only logs the accepted command flags, and
	    the touched-file comment audit added the seed/rngState determinism
	    invariant plus the borrowed context lifetime note. `TakeInput()` still
	    spans 1,043 lines after formatter wrapping. Gate evidence:
	    `TestOutput\agent_logs\plan01_run_sim_ui_interaction_clicks.log` (16.0s,
	    both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_run_sim_ui_validate_full.log` (50.8s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_run_sim_ui_validate_format_rerun.log`
	    (9.4s), `TestOutput\agent_logs\plan01_run_sim_ui_runtime_boundaries.log`
	    (17.7s), and `TestOutput\agent_logs\plan01_run_sim_ui_build_profile.log`
	    (8.5s).
	  - [x] Generated-scene count UI command extraction moved model-count and
	    solver ball/box request handling out of `Run::TakeInput()` and into
	    `SceneRuntimeGeneratedControls`. `RunInput` still applies the returned
	    replay/profile follow-up action immediately after each accepted request,
	    preserving the prior command order. The touched-file comment audit added
	    generated UI command vocabulary and partial solver-slider invariants.
	    `TakeInput()` now spans 1,036 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_scene_generated_ui_interaction_clicks.log`
	    (16.1s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_scene_generated_ui_validate_full.log`
	    (49.3s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_scene_generated_ui_validate_format.log`
	    (9.3s), `TestOutput\agent_logs\plan01_scene_generated_ui_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_scene_generated_ui_build_profile.log`
	    (6.5s).
	  - [x] Render-device vsync UI command extraction moved the renderer-tab
	    vsync toggle out of `Run::TakeInput()` and into `RuntimeTuning`. The
	    helper owns the runtime setting flip and optional backend lifecycle
	    notification; `RunInput` only records the accepted action. The touched-file
	    comment audit added render-device command vocabulary and the nullable
	    lifecycle borrow note. `TakeInput()` now spans 1,033 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_render_vsync_ui_interaction_clicks.log`
	    (16.4s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_render_vsync_ui_validate_full.log` (50.5s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_render_vsync_ui_validate_format.log`
	    (9.4s), `TestOutput\agent_logs\plan01_render_vsync_ui_runtime_boundaries.log`
	    (17.6s), and `TestOutput\agent_logs\plan01_render_vsync_ui_build_profile.log`
	    (8.6s).
	  - [x] Scene fixed-step UI command extraction moved the fixed-step toggle
	    and simulation reset out of `Run::TakeInput()` and into `RuntimeTuning`.
	    The helper owns the scene tick-cadence mutation and immediate accumulator
	    reset; `RunInput` only records the accepted action. The touched-file
	    comment audit added scene fixed-step command vocabulary and the immediate
	    reset invariant. `TakeInput()` now spans 1,032 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_fixed_step_ui_interaction_clicks.log`
	    (16.7s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_fixed_step_ui_validate_full.log` (51.1s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_fixed_step_ui_validate_format.log` (9.5s),
	    `TestOutput\agent_logs\plan01_fixed_step_ui_runtime_boundaries.log`
	    (17.6s), and `TestOutput\agent_logs\plan01_fixed_step_ui_build_profile.log`
	    (8.5s).
	  - [x] Scene-tab runtime command queuing moved reset, defaults reset, demo
	    load, save defaults, create scene, and browser index selection command
	    construction out of `Run::TakeInput()` and into `SceneRuntimeCoordinator`.
	    The helper appends the same deferred `RuntimeCommand` payloads in the
	    same order and returns flags for `RunInput` action logging. The
	    touched-file comment audit added scene UI command vocabulary and the
	    queued-command order invariant. `TakeInput()` now spans 1,021 lines. Gate
	    evidence:
	    `TestOutput\agent_logs\plan01_scene_ui_commands_interaction_clicks.log`
	    (17.2s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_scene_ui_commands_validate_full.log` (51.7s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_scene_ui_commands_validate_format.log`
	    (10.2s),
	    `TestOutput\agent_logs\plan01_scene_ui_commands_runtime_boundaries.log`
	    (17.9s), and
	    `TestOutput\agent_logs\plan01_scene_ui_commands_build_profile.log` (10.4s).
	  - [x] Editor placement UI command extraction moved static placement
	    requests, object-type selection with enter-placement intent,
	    place-static toggles, and terrain-align toggles out of `Run::TakeInput()`
	    and into `EditorTools`. `RunInput` still owns editor-mode and placement
	    mode camera/cursor transitions and only records accepted command flags.
	    The touched-file comment audit kept the editor UI command result
	    invariant on the Run-owned transition boundary. `TakeInput()` now spans
	    1,017 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_editor_placement_ui_interaction_clicks.log`
	    (15.9s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_editor_placement_ui_validate_full.log`
	    (52.4s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_editor_placement_ui_validate_format.log`
	    (9.4s),
	    `TestOutput\agent_logs\plan01_editor_placement_ui_runtime_boundaries.log`
	    (17.6s), and
	    `TestOutput\agent_logs\plan01_editor_placement_ui_build_profile.log`
	    (9.5s).
	  - [x] Run camera-mode UI command extraction moved requested camera-mode
	    validation out of `Run::TakeInput()` and into `RuntimeTuning`.
	    `RunInput` still owns applying the accepted mode so scene normalization,
	    attach return-state handling, cursor ownership, and action logging stay on
	    the existing transition path. The touched-file comment audit added Run
	    camera command vocabulary and the result invariant for the Run-owned
	    transition boundary. `TakeInput()` now spans 1,016 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_camera_mode_ui_interaction_clicks.log`
	    (17.1s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_camera_mode_ui_validate_full.log` (50.6s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_camera_mode_ui_validate_format.log` (9.5s),
	    `TestOutput\agent_logs\plan01_camera_mode_ui_runtime_boundaries.log`
	    (17.7s), and
	    `TestOutput\agent_logs\plan01_camera_mode_ui_build_profile.log` (8.7s).
	  - [x] Cinematic mode-selection UI command extraction moved the raw
	    `requestedModeSceneIndex` guard out of `Run::TakeInput()` and into
	    `RuntimeTuning`. `RunInput` still enters interactive scene before the
	    existing cinematic mode apply helper, preserving the prior transition and
	    style-load order. The touched-file comment audit kept cinematic command
	    vocabulary and the accepted-request invariant in the apply helper.
	    `TakeInput()` still spans 1,016 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_cinematic_mode_ui_interaction_clicks.log`
	    (16.5s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_cinematic_mode_ui_validate_full.log`
	    (50.4s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_cinematic_mode_ui_validate_format.log`
	    (9.5s),
	    `TestOutput\agent_logs\plan01_cinematic_mode_ui_runtime_boundaries.log`
	    (17.8s), and
	    `TestOutput\agent_logs\plan01_cinematic_mode_ui_build_profile.log`
	    (8.6s).
	  - [x] Editor mode-transition UI flag extraction moved the raw
	    `toggleEditorMode` and `togglePlacementMode` field checks out of
	    `Run::TakeInput()` and into the existing `EditorTools` pre-mode command
	    result. `RunInput` still owns the keyboard/UI source decision and the
	    camera/cursor transition lambdas. The touched-file comment audit kept the
	    result invariant for Run-owned transitions. `TakeInput()` still spans
	    1,016 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_editor_mode_ui_interaction_clicks.log`
	    (16.4s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_editor_mode_ui_validate_full.log` (51.1s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_editor_mode_ui_validate_format.log` (9.6s),
	    `TestOutput\agent_logs\plan01_editor_mode_ui_runtime_boundaries.log`
	    (17.7s), and
	    `TestOutput\agent_logs\plan01_editor_mode_ui_build_profile.log` (9.8s).
	  - [x] After-UI keyboard dispatch extraction moved the ESC/UI dismissal
	    keyboard lambda and binding loop out of `Run::TakeInput()` into
	    `Run::DispatchAfterUIKeyboardActions()`. UI controls still get first
	    refusal, single ESC still toggles the diagnostics UI, and double ESC still
	    posts quit through the same latch timing path. The touched-file comment
	    audit kept the local ESC ordering comment and the Run.h helper summary.
	    `TakeInput()` now spans 977 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_after_ui_keyboard_interaction_clicks.log`
	    (16.0s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_after_ui_keyboard_validate_full.log` (52.4s;
	    project filters/runtime boundaries passed, Profile/Debug builds had 0
	    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
	    baselines, and `physics_regression_solver.csv` matched byte-exactly).
	    Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_after_ui_keyboard_validate_format_rerun2.log`
	    (9.0s),
	    `TestOutput\agent_logs\plan01_after_ui_keyboard_runtime_boundaries_rerun.log`
	    (17.8s), and
	    `TestOutput\agent_logs\plan01_after_ui_keyboard_build_profile_rerun.log`
	    (10.1s).
	  - [x] Scene-control executor extraction moved the duplicated
	    `SceneRuntimeControlAction` switch out of `Run::TakeInput()`,
	    `Run::DrainRuntimeCommands()`, `Run::TickScreenshots()`, and
	    `Run::TickSceneAdvance()` into the scene-runtime execution helper beside
	    the intent type. The helper preserves `enterInteractiveSceneRun`, clear
	    automation, load-scene, and cinematic browser-style behavior, including
	    the extra `EnterInteractiveSceneRun()` before cinematic mode. The
	    touched-file comment audit added the execution-context lifetime note and
	    kept the executor invariant explaining the remaining Run-owned side
	    effects. `TakeInput()` now spans 965 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_scene_control_executor_interaction_clicks.log`
	    (16.5s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_scene_control_executor_validate_full.log`
	    (53.0s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_scene_control_executor_validate_format.log`
	    (9.1s),
	    `TestOutput\agent_logs\plan01_scene_control_executor_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_scene_control_executor_build_profile.log`
	    (10.0s).
	  - [x] Camera input-frame extraction moved the raw mouse-look delta path
	    and WASD camera-key state application out of `Run::TakeInput()` and into
	    `InputController::ApplyCameraInputFrame()`. `RunInput` still resolves
	    frame policy, pointer ownership, and cursor-ownership cleanup, while
	    `InputController` now owns the camera-local mouse delta and key-state
	    mutation. The touched-file comment audit added the frame-context
	    ownership note and kept the raw-mouse fallback `Why:` comment near the
	    moved code. `TakeInput()` now spans 901 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_camera_input_frame_interaction_clicks.log`
	    (16.3s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_camera_input_frame_validate_full.log`
	    (52.2s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_camera_input_frame_validate_format.log`
	    (9.2s),
	    `TestOutput\agent_logs\plan01_camera_input_frame_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_camera_input_frame_build_profile.log`
	    (10.4s).
	  - [x] UI action-recorder extraction moved repeated UI command-result to
	    `RuntimeInputAction` transition mapping out of the middle of
	    `Run::TakeInput()` into file-local recorder helpers. The command
	    application order remains in `TakeInput()`, and the split tornado and
	    presentation helpers preserve the original transition-history ordering
	    around raycast visualization and sound/water reflection commands. The
	    touched-file comment audit added the mapper ownership/order note.
	    `TakeInput()` now spans 760 lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_ui_action_recorders_interaction_clicks.log`
	    (16.3s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_ui_action_recorders_validate_full.log`
	    (48.9s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_ui_action_recorders_validate_format.log`
	    (9.2s),
	    `TestOutput\agent_logs\plan01_ui_action_recorders_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_ui_action_recorders_build_profile.log`
	    (6.3s).
	  - [x] Post-mapped keyboard shortcut extraction moved the editor-placement
	    and replay velocity-edit ALT aftermath out of `Run::TakeInput()` into a
	    file-local helper with explicit callbacks for the Run-owned transition
	    edges. The helper preserves editor-mode ALT handling, replay velocity-edit
	    enable/disable, live-advance/inspection-camera updates, scrubber
	    visibility, and keyboard memory updates. The touched-file comment audit
	    added the borrowed-context lifetime note. `TakeInput()` now spans 723
	    lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_post_mapped_keyboard_shortcuts_interaction_clicks.log`
	    (15.5s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_post_mapped_keyboard_shortcuts_validate_full.log`
	    (48.9s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_post_mapped_keyboard_shortcuts_validate_format.log`
	    (9.2s),
	    `TestOutput\agent_logs\plan01_post_mapped_keyboard_shortcuts_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_post_mapped_keyboard_shortcuts_build_profile.log`
	    (6.1s).
	  - [x] Mapped keyboard dispatch extraction moved the remaining keyboard
	    binding switch out of `Run::TakeInput()` into a file-local helper fed by
	    borrowed runtime/input/scene context and explicit callbacks for private
	    Run-owned transitions. The helper preserves camera mode cycling,
	    launcher/attached/director shortcuts, diagnostics shortcuts, and
	    left/right scene navigation order. The touched-file comment audit added
	    the borrowed-context lifetime note. `TakeInput()` now spans 495 lines.
	    Gate evidence:
	    `TestOutput\agent_logs\plan01_mapped_keyboard_dispatch_interaction_clicks.log`
	    (16.6s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_mapped_keyboard_dispatch_validate_full.log`
	    (49.1s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_mapped_keyboard_dispatch_validate_format.log`
	    (9.3s),
	    `TestOutput\agent_logs\plan01_mapped_keyboard_dispatch_runtime_boundaries.log`
	    (17.6s), and
	    `TestOutput\agent_logs\plan01_mapped_keyboard_dispatch_build_profile.log`
	    (6.3s).
	  - [x] UI command frame extraction moved the post-keyboard UI update,
	    replay-mouse ownership arbitration, UI command application, generated
	    scene controls, cinematic commands, UI stress, and editor wheel follow-up
	    out of `Run::TakeInput()` into a file-local helper that returns only the
	    world-input suppression result. The touched-file comment audit added the
	    borrowed UI-frame context lifetime note. `TakeInput()` now spans 259
	    lines. Gate evidence:
	    `TestOutput\agent_logs\plan01_ui_frame_helper_interaction_clicks.log`
	    (15.3s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_ui_frame_helper_validate_full.log`
	    (50.6s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_ui_frame_helper_validate_format.log`
	    (9.2s),
	    `TestOutput\agent_logs\plan01_ui_frame_helper_runtime_boundaries.log`
	    (17.5s), and
	    `TestOutput\agent_logs\plan01_ui_frame_helper_build_profile.log`
	    (6.3s).
	  - [x] TakeInput size-target extraction moved editor placement/editor-mode
	    transition bodies, blocked keyboard-memory updates, and the final
	    pointer/camera frame tail out of `Run::TakeInput()` into file-local
	    helpers with explicit Run-owned callback edges. `TakeInput()` now spans
	    199 lines after formatting, completing the Phase 1 size target. The
	    touched-file comment audit added borrowed-context lifetime notes for the
	    new editor transition and pointer/camera helpers. Gate evidence:
	    `TestOutput\agent_logs\plan01_takeinput_size_target_interaction_clicks.log`
	    (16.9s, both interaction reports `ok=1`) and
	    `TestOutput\agent_logs\plan01_takeinput_size_target_validate_full.log`
	    (49.3s; project filters/runtime boundaries passed, Profile/Debug builds
	    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
	    matched baselines, and `physics_regression_solver.csv` matched
	    byte-exactly). Targeted pre-gate checks also passed:
	    `TestOutput\agent_logs\plan01_takeinput_size_target_validate_format.log`
	    (9.2s),
	    `TestOutput\agent_logs\plan01_takeinput_size_target_runtime_boundaries.log`
	    (17.6s), and
	    `TestOutput\agent_logs\plan01_takeinput_size_target_build_profile.log`
	    (6.3s).
- [x] **1.4** Confirm `TakeInput()` is under ~200 lines (setup + dispatch loop).

### Phase 2 — Move state shelves out of `RunState`

- [x] **2.1** For **one shelf at a time**, relocate its fields into the owner
  that mutates them and remove cross-file pokes (e.g. `m_camera.autoCycleAccum`
  from `RunFrame`). Gate: `validate_full`. Commit per shelf.

  Partial progress:
  - [x] Input latch shelf removal deleted `RunInputLatchState` and
    `Run::m_inputLatches`. The left/right scene-cycle latches were dead
    write-only state after mapped keyboard dispatch and were removed instead of
    renamed. ESC quick-tap timing moved into `RuntimeInputContext`, beside
    semantic input edge memory, and `InputController::ResetUnfocusedInput()` no
    longer mutates Run-owned latch refs. The touched-file comment audit added the
    ESC timing ownership note. Gate evidence:
    `TestOutput\agent_logs\plan01_input_latch_shelf_interaction_clicks.log`
    (25.6s, both interaction reports `ok=1`) and
    `TestOutput\agent_logs\plan01_input_latch_shelf_validate_full.log` (53.0s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0
    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_input_latch_shelf_validate_format.log` (9.2s),
    `TestOutput\agent_logs\plan01_input_latch_shelf_runtime_boundaries.log`
    (17.6s), and
    `TestOutput\agent_logs\plan01_input_latch_shelf_build_profile.log` (10.6s).
  - [x] Scene browser/UI override shelf relocation moved `RunSceneBrowserState`
    and `RunSceneUIOverrideState` out of `RunState.h` into
    `Scene/SceneControllerState.h`, next to the `SceneController` owner that
    already stores those fields. Scene helper headers now include the scene-owned
    state header instead of relying on the Run state shelf, and the project-filter
    rule table recognizes the new scene header. The touched-file comment audit
    added the new scene state learning header and kept the borrowed scene
    invariants local to the owner. Gate evidence:
    `TestOutput\agent_logs\plan01_scene_controller_state_validate_full_rerun.log`
    (46.5s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_scene_controller_state_validate_format.log`
    (9.3s),
    `TestOutput\agent_logs\plan01_scene_controller_state_runtime_boundaries.log`
    (17.6s),
    `TestOutput\agent_logs\plan01_scene_controller_state_build_profile.log`
    (10.2s),
    `TestOutput\agent_logs\plan01_scene_controller_state_project_filters.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_scene_controller_state_validate_fast.log`
    (44.8s). An initial `validate_full` stopped on the missing project-filter
    rule for the new header; `tools\validate_project_filters.py` was updated and
    validated before the rerun.
  - [x] Replay capture mismatch shelf moved into `ReplayRuntime`; deleted
    `RunReplayMismatchState`, `Run::m_solverReplayMismatch`, and the
    `RunFrame.cpp` `CompareLatestReplaySamples()` helper. `ReplayRuntime`
    now owns paired solver/presentation mismatch comparison, per-timeline
    diagnostic throttling, and the reset that happens when a new replay timeline
    starts. The touched-file comment audit added the local replay diagnostic
    ownership note. Gate evidence:
    `TestOutput\agent_logs\plan01_replay_mismatch_shelf_validate_full.log`
    (54.1s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_replay_mismatch_shelf_build_profile.log`
    (10.5s),
    `TestOutput\agent_logs\plan01_replay_mismatch_shelf_runtime_boundaries.log`
    (17.6s), and
    `TestOutput\agent_logs\plan01_replay_mismatch_shelf_validate_format.log`
    (9.2s). A post-audit comment-only format rerun also passed:
    `TestOutput\agent_logs\plan01_replay_mismatch_shelf_validate_format_rerun.log`
    (9.1s).
  - [x] Live style/capture harness shelf moved behind `LiveStyleController`;
    deleted `RunLiveStyleControlState` from `RunState.h`. `RunLiveStyle.cpp`
    now delegates control-folder stamps, status-file updates, and pending
    screenshot state to the controller, while `Run` keeps the broader
    interactive-run and screenshot-save side effects. The touched-file comment
    audit added the new controller learning header and kept the harness
    invariants local to the owner. Gate evidence:
    `TestOutput\agent_logs\plan01_live_style_controller_validate_full.log`
    (58.9s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_live_style_controller_build_profile.log`
    (10.4s),
    `TestOutput\agent_logs\plan01_live_style_controller_runtime_boundaries.log`
    (17.6s), and
    `TestOutput\agent_logs\plan01_live_style_controller_project_filters.log`
    (1.2s). The first format pass found `RunLiveStyle.cpp` after the refactor
    (`TestOutput\agent_logs\plan01_live_style_controller_validate_format.log`,
    5.2s); the narrowed clang-format fix was applied and
    `TestOutput\agent_logs\plan01_live_style_controller_validate_format_rerun.log`
    passed in 9.2s.
  - [x] Graphics stress shelf moved behind `GraphicsStressController`; deleted
    `RunGraphicsStressState` from `RunState.h`. The controller now owns the
    launch seed, deterministic random stream, action cadence, scene-load
    cadence, memory-log cadence, and persistent frame/scene-load counters, while
    `RunStress.cpp` remains the executor for scene, UI, render, and live runtime
    mutations. Startup configuration, scene-load resume, WM_QUIT logging, and
    graphics-stress actions now use controller methods instead of direct field
    pokes. The touched-file comment audit added the new controller learning
    header and kept graphics-stress random/cadence invariants local to the
    owner. Gate evidence:
    `TestOutput\agent_logs\plan01_graphics_stress_controller_validate_full.log`
    (58.0s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_graphics_stress_controller_build_profile_rerun.log`
    (10.0s),
    `TestOutput\agent_logs\plan01_graphics_stress_controller_runtime_boundaries.log`
    (17.6s), and
    `TestOutput\agent_logs\plan01_graphics_stress_controller_project_filters.log`
    (1.2s). The first format pass found `RunStress.cpp` after the refactor
    (`TestOutput\agent_logs\plan01_graphics_stress_controller_validate_format.log`,
    5.2s); the narrowed clang-format fix was applied and
    `TestOutput\agent_logs\plan01_graphics_stress_controller_validate_format_rerun.log`
    passed in 9.1s.
  - [x] Demo Director playback state shelf moved from `RunState.h` into
    `DemoDirector.h`, beside the fixed-capacity shot-list, phase, and camera-pose
    records it times. `RunCameraState` still stores the director playback value
    for now, but the state type is no longer declared in the generic Run shelf
    header. The touched-file comment audit expanded the Demo Director learning
    header with playback-state ownership and presentation-only invariants. Gate
    evidence:
    `TestOutput\agent_logs\plan01_demo_director_state_shelf_validate_full.log`
    (56.9s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_demo_director_state_shelf_build_profile.log`
    (14.4s),
    `TestOutput\agent_logs\plan01_demo_director_state_shelf_validate_format.log`
    (9.3s), and
    `TestOutput\agent_logs\plan01_demo_director_state_shelf_runtime_boundaries.log`
    (17.6s).
  - [x] Interaction automation state shelf moved from `RunState.h` into
    `RunInteractionAutomationState.h`. The new header owns the CLI automation
    action enums, script action records, report records, and injected input
    frame state used by `RunInteractionAutomation.cpp`; `Run` still stores the
    harness value until the automation owner is split further. The Visual Studio
    project/filter metadata and `tools\validate_project_filters.py` guardrail
    now recognize the new header. The touched-file comment audit added the new
    automation learning header and preserved the required `RunState.h` inline
    comment alignment after the shelf removal. Gate evidence:
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_validate_full.log`
    (47.2s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_build_profile.log`
    (10.3s),
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_validate_format.log`
    (9.4s),
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_runtime_boundaries.log`
    (17.7s),
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_project_filters_rerun.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_interaction_automation_state_shelf_validate_fast.log`
    (44.2s). The first project-filter pass caught the missing filter rule for
    the new header
    (`TestOutput\agent_logs\plan01_interaction_automation_state_shelf_project_filters.log`,
    1.2s); the guardrail prefix was added before the rerun.
  - [x] Startup state shelf moved from `RunState.h` into
    `RunStartupState.h`. The new header owns the startup-only game-model
    capacity and worker-thread baseline captured from `EngineConfig`; `Run`
    still stores `m_startup`, and scene reload paths keep reading that
    startup baseline through the existing owner field. The Visual Studio
    project/filter metadata and `tools\validate_project_filters.py` guardrail
    now recognize the new header. The touched-file comment audit added the new
    startup-state learning header and kept the remaining `RunState.h` shelf
    comments aligned. Gate evidence:
    `TestOutput\agent_logs\plan01_startup_state_shelf_validate_full.log`
    (47.1s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_startup_state_shelf_build_profile.log`
    (10.4s),
    `TestOutput\agent_logs\plan01_startup_state_shelf_validate_format.log`
    (9.3s),
    `TestOutput\agent_logs\plan01_startup_state_shelf_runtime_boundaries.log`
    (17.7s),
    `TestOutput\agent_logs\plan01_startup_state_shelf_project_filters.log`
    (1.2s), and
    `TestOutput\agent_logs\plan01_startup_state_shelf_validate_fast.log`
    (43.1s).
  - [x] Timer state shelf moved from `RunState.h` into
    `RunTimerState.h`. The new header owns the process-lifetime frame,
    simulation, render, rolling diagnostics, scene-energy, and UI draw-call
    timing values borrowed by frame, input, render, HUD, and automation code.
    `Run` still stores `m_timers`, but `RunState.h` no longer pulls in
    `Timer.h` for unrelated shelves. The Visual Studio project/filter metadata
    and `tools\validate_project_filters.py` guardrail now recognize the new
    header. The touched-file comment audit added the new timing-state learning
    header and aligned its inline comments. Gate evidence:
    `TestOutput\agent_logs\plan01_timer_state_shelf_validate_full.log`
    (47.0s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_timer_state_shelf_build_profile.log`
    (10.4s),
    `TestOutput\agent_logs\plan01_timer_state_shelf_validate_format.log`
    (9.3s),
    `TestOutput\agent_logs\plan01_timer_state_shelf_runtime_boundaries.log`
    (17.5s),
    `TestOutput\agent_logs\plan01_timer_state_shelf_project_filters.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_timer_state_shelf_validate_fast.log`
    (42.8s).
  - [x] Launch options shelf moved from `RunState.h` into
    `RunLaunchOptions.h`. The new header owns CLI/startup policy for time scale,
    fixed-step, generated-scene object type, graphics/UI stress, allocation
    guard mode, and physics-debug visualization overrides that `Run` reapplies
    across scene loads. `RunState.h` no longer inherits allocation-guard or
    generated-scene setup includes for unrelated shelves; scene style contexts
    include the launch-options header explicitly. The Visual Studio
    project/filter metadata and `tools\validate_project_filters.py` guardrail
    now recognize the new header. The touched-file comment audit added the new
    launch-options learning header and aligned its inline comments. Gate
    evidence:
    `TestOutput\agent_logs\plan01_launch_options_shelf_validate_full.log`
    (47.3s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_launch_options_shelf_build_profile.log`
    (10.7s),
    `TestOutput\agent_logs\plan01_launch_options_shelf_validate_format.log`
    (9.5s),
    `TestOutput\agent_logs\plan01_launch_options_shelf_runtime_boundaries.log`
    (17.6s),
    `TestOutput\agent_logs\plan01_launch_options_shelf_project_filters.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_launch_options_shelf_validate_fast.log`
    (43.0s).
  - [x] Debug/overlay shelf moved from `RunState.h` into
    `RunDebugState.h`. The new header owns `OverlayMode`, HUD/debug overlay
    cycle state, water/terrain presentation toggles, physics visualization
    flags, broadphase/collision visualizer toggles, cross-scene pause lock, and
    debug repro HUD message timing. Diagnostics, replay restore, scene reset,
    and scene UI-option code now include the debug-state header where they need
    the concrete fields, and `RunState.h` no longer pulls in physics debug
    visualizer definitions for unrelated shelves. The Visual Studio
    project/filter metadata and `tools\validate_project_filters.py` guardrail
    now recognize the new header. The touched-file comment audit added the new
    debug-state learning header and aligned its inline comments. Gate evidence:
    `TestOutput\agent_logs\plan01_debug_state_shelf_validate_full.log`
    (46.8s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_debug_state_shelf_build_profile.log`
    (10.2s),
    `TestOutput\agent_logs\plan01_debug_state_shelf_validate_format.log`
    (9.3s),
    `TestOutput\agent_logs\plan01_debug_state_shelf_runtime_boundaries.log`
    (17.5s),
    `TestOutput\agent_logs\plan01_debug_state_shelf_project_filters.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_debug_state_shelf_validate_fast.log`
    (44.2s).
  - [x] Runtime settings shelf moved from `RunState.h` into
    `RunRuntimeSettings.h`. The new header owns live render sync toggles, Catto
    sleep policy, contact-audio debug/flash settings, tornado force schedules,
    and tornado visual shell tuning. Runtime view-model, replay restore, and
    scene reset code now include the runtime-settings header where they need
    concrete fields, and `RunState.h` no longer pulls in tornado field
    definitions for unrelated shelves. The Visual Studio project/filter metadata
    and `tools\validate_project_filters.py` guardrail now recognize the new
    header. The touched-file comment audit added the new runtime-settings
    learning header and aligned its inline comments. Gate evidence:
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_validate_full.log`
    (47.0s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_build_profile.log`
    (10.3s),
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_validate_format.log`
    (9.4s),
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_runtime_boundaries.log`
    (17.7s),
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_project_filters.log`
    (1.1s), and
    `TestOutput\agent_logs\plan01_runtime_settings_shelf_validate_fast.log`
    (42.9s).
  - [x] Attached camera state shelf moved from `RunState.h` into
    `AttachedCameraController.h`. The controller header now owns target
    identity, follow/orbit submode, fixed-offset, entry-tween, and return-pose
    state. `Run` still stores `m_attachedCamera`, but callers include the
    attached-camera controller header for concrete fields, and `RunState.h` no
    longer pulls in physics handle definitions for unrelated shelves. No project
    or tool metadata changed for this header-only ownership move. The
    touched-file comment audit kept the controller ownership header current and
    verified the remaining `RunState.h` shelf comments. Gate evidence:
    `TestOutput\agent_logs\plan01_attached_camera_state_shelf_validate_full.log`
    (55.5s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_attached_camera_state_shelf_build_profile.log`
    (10.1s),
    `TestOutput\agent_logs\plan01_attached_camera_state_shelf_validate_format.log`
    (9.4s),
    `TestOutput\agent_logs\plan01_attached_camera_state_shelf_runtime_boundaries.log`
    (17.7s), and
    `TestOutput\agent_logs\plan01_attached_camera_state_shelf_diff_check.log`
    (0.1s).
  - [x] Camera/input state shelf moved from `RunState.h` into
    `RunCameraState.h`. The new header owns operator camera mode, input memory,
    mouse-look reset state, track-ball/auto-screenshot timers, and Director
    playback state. `Run` still stores `m_camera`, while concrete camera/input
    helpers now include `RunCameraState.h`; `RunState.h` now only carries the
    subsystem shelf. The Visual Studio project/filter metadata and
    `tools\validate_project_filters.py` guardrail now recognize the new header.
    The touched-file comment audit added the camera-state learning header,
    refreshed the shrinking `RunState.h` header, and re-aligned inline comments.
    Gate evidence:
    `TestOutput\agent_logs\plan01_camera_state_shelf_validate_full.log`
    (47.3s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_camera_state_shelf_build_profile.log`
    (10.4s),
    `TestOutput\agent_logs\plan01_camera_state_shelf_validate_format.log`
    (9.3s),
    `TestOutput\agent_logs\plan01_camera_state_shelf_runtime_boundaries.log`
    (17.8s),
    `TestOutput\agent_logs\plan01_camera_state_shelf_project_filters.log`
    (1.1s),
    `TestOutput\agent_logs\plan01_camera_state_shelf_validate_fast.log`
    (42.8s), and
    `TestOutput\agent_logs\plan01_camera_state_shelf_diff_check.log`
    (0.3s; whitespace clean, with Git line-ending warnings for project XML).
  - [x] Subsystem/render-resource shelf moved from `RunState.h` into
    `RunSubsystemState.h`, and `RunState.h` was deleted. The new header owns the
    process-lifetime asset, texture, camera, terrain, skybox, startup-service
    borrows, and render pass resource shelf. `Run` still stores `m_systems`, but
    there are no remaining source/project includes of `RunState.h`. The Visual
    Studio project/filter metadata and `tools\validate_project_filters.py`
    guardrail now recognize `RunSubsystemState.h`; source comments that pointed
    at `RunState.h` now name the narrower headers. The touched-file comment
    audit added the subsystem-state learning header and verified the edited
    source/header comments. Gate evidence:
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_validate_full.log`
    (48.1s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_build_profile.log`
    (10.6s),
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_validate_format.log`
    (9.4s),
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_runtime_boundaries.log`
    (17.7s),
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_project_filters_initial.log`
    (1.1s),
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_validate_fast.log`
    (43.2s), and
    `TestOutput\agent_logs\plan01_subsystem_state_shelf_diff_check.log`
    (0.2s; whitespace clean, with Git line-ending warnings for project XML).

### Phase 3 — Shrink `Run`

- [ ] **3.1** Reduce `Run` to `Initialise` / `Run` / `Shutdown` plus per-frame
  tick coordination that calls owners. It should no longer implement subsystem
  logic. Gate: `validate_full`. Commit.

  Partial progress:
  - [x] Startup override public-surface reduction collapsed the pre-`Initialise`
    command-line setter script into `RunStartupOverrides` plus one public
    `Run::ApplyStartupOverrides()` call. The old launch/debug/replay/overlay
    startup setters were deleted from `Run`'s public API rather than moved to
    private helpers, keeping the runtime-boundary private-method ratchet green.
    `Init.cpp` now builds the startup packet from `ParsedArgs`, while `Run`
    owns the same live side effects in the same order. This reduces `Run` by a
    net 32 public methods for the startup surface; owned-member count is
    unchanged, so Phase 3 and the public+member acceptance row remain open.
    Gate evidence:
    `TestOutput\agent_logs\plan01_startup_overrides_validate_full.log`
    (54.3s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_startup_overrides_build_profile_final.log`
    (10.1s),
    `TestOutput\agent_logs\plan01_startup_overrides_validate_format_final2.log`
    (9.3s), and
    `TestOutput\agent_logs\plan01_startup_overrides_runtime_boundaries_final.log`
    (17.7s). The touched-file comment audit covered `Init.cpp`, `Run.cpp`,
    `Run.h`, `RunLaunchOptions.h`, and `RunLiveStyle.cpp`.
  - [x] Startup override apply-size split moved the cohesive launch-policy,
    stress-policy, replay-recording, replay-probe, presentation/debug, and
    diagnostics startup groups into file-local helpers that take explicit owner
    references. `Run::ApplyStartupOverrides()` dropped from 257 measured lines
    to 41 measured lines without adding public or private `Run` methods, so
    the private-method runtime-boundary ratchet remains green. The remaining
    >~200-line `Run::` functions are pre-existing Phase 3 targets:
    `RestoreReplayV2ArtifactTargetState`, interaction automation ticks,
    graphics/UI stress actions, replay save probe, `Execute`, and the
    borderline 201-line `TakeInput` measurement. Gate evidence:
    `TestOutput\agent_logs\plan01_startup_apply_split_validate_full.log`
    (51.2s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_startup_apply_split_build_profile.log`
    (6.8s),
    `TestOutput\agent_logs\plan01_startup_apply_split_validate_format.log`
    (9.4s), and
    `TestOutput\agent_logs\plan01_startup_apply_split_runtime_boundaries.log`
    (17.6s). The touched-file comment audit covered `Run.cpp`.
  - [x] UI stress action split moved the deterministic stress action switch
    into source-local `ApplyUIStressAction()` with explicit owner references.
    `Run::RunUIStressActions()` dropped from 235 measured lines to 85 measured
    lines without adding public or private `Run` methods. `RunGraphicsStressActions()`
    remains 365 measured lines, so Phase 3 stays open for the remaining large
    `Run::` functions. Gate evidence:
    `TestOutput\agent_logs\plan01_ui_stress_split_validate_full.log`
    (~56s by log timestamps; project filters/runtime boundaries passed,
    Profile/Debug builds had 0 warnings and 0 errors, DX12 InfoQueue errors =
    0, screenshots matched baselines, and `physics_regression_solver.csv`
    matched byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_ui_stress_split_build_profile.log` (7.0s),
    `TestOutput\agent_logs\plan01_ui_stress_split_validate_format_post_comment.log`
    (9.3s), and
    `TestOutput\agent_logs\plan01_ui_stress_split_runtime_boundaries.log`
    (17.7s). The touched-file comment audit covered `RunStress.cpp`.
  - [x] Graphics stress action split moved the deterministic render/runtime
    stress switch into source-local `ApplyGraphicsStressAction()` with explicit
    owner references. `Run::RunGraphicsStressActions()` dropped from 365
    measured lines to 199 measured lines without adding public or private `Run`
    methods. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (578), `TickReplaySaveProbe` (311),
    `Execute` (278), `TickInteractionAutomationAfterRender` (258), `TakeInput`
    (201), and `WriteInteractionAutomationReport` (197), so Phase 3 and the
    structural acceptance rows remain open. Gate evidence:
    `TestOutput\agent_logs\plan01_graphics_stress_split_validate_full.log`
    (54.5s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_graphics_stress_split_build_profile.log`
    (6.2s),
    `TestOutput\agent_logs\plan01_graphics_stress_split_validate_format_final.log`
    (9.2s), and
    `TestOutput\agent_logs\plan01_graphics_stress_split_runtime_boundaries.log`
    (17.6s). The touched-file comment audit covered `RunStress.cpp`.
  - [x] Execute-frame UI/post-physics split moved UI text rendering and
    post-physics visualizer updates into source-local helpers with explicit
    owner references and callback-only access to the remaining `Run`-owned
    bookkeeping. `Run::Execute()` dropped from 278 measured lines to 196
    measured lines without adding public or private `Run` methods. The
    remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (578), `TickReplaySaveProbe` (311),
    `TickInteractionAutomationAfterRender` (258), `TakeInput` (201),
    `RunGraphicsStressActions` (199), and `WriteInteractionAutomationReport`
    (197), so Phase 3 and the structural acceptance rows remain open. Gate
    evidence:
    `TestOutput\agent_logs\plan01_execute_split_validate_full.log` (56.9s;
    project filters/runtime boundaries passed, Profile/Debug builds had 0
    warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_execute_postphysics_split_build_profile.log`
    (6.6s),
    `TestOutput\agent_logs\plan01_execute_split_validate_format_final.log`
    (9.3s), and
    `TestOutput\agent_logs\plan01_execute_split_runtime_boundaries.log`
    (17.7s). The touched-file comment audit covered `RunFrame.cpp`.
  - [x] Replay save probe split moved Debug-only event coverage injection and
    v2 artifact save/load validation into source-local helpers with explicit
    owner references. `Run::TickReplaySaveProbe()` dropped from 311 measured
    lines to 42 measured lines without adding public or private `Run` methods.
    The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (578),
    `TickInteractionAutomationAfterRender` (258), `TakeInput` (201),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport`
    (197), and `Execute` (196), so Phase 3 and the structural acceptance rows
    remain open. Gate evidence:
    `TestOutput\agent_logs\plan01_replay_save_probe_split_validate_full.log`
    (57.1s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_replay_save_probe_split_build_debug_final2.log`
    (5.8s),
    `TestOutput\agent_logs\plan01_replay_save_probe_split_validate_format_final.log`
    (9.4s), and
    `TestOutput\agent_logs\plan01_replay_save_probe_split_runtime_boundaries.log`
    (17.6s). The touched-file comment audit covered `RunFrame.cpp`.
  - [x] Interaction automation after-render assertion split moved the
    deterministic assertion switch into source-local
    `EvaluateInteractionAutomationAssertion()` with explicit owner references
    and lazy callback access to the remaining inspect-gizmo predicate.
    `Run::TickInteractionAutomationAfterRender()` dropped from 258 measured
    lines to 94 measured lines without adding public or private `Run` methods.
    The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (578), `TakeInput` (201),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport`
    (197), and `Execute` (196), so Phase 3 and the structural acceptance rows
    remain open. Gate evidence:
    `TestOutput\agent_logs\plan01_interaction_after_render_split_validate_full.log`
    (54.0s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_interaction_after_render_split_build_profile.log`
    (9.1s),
    `TestOutput\agent_logs\plan01_interaction_after_render_split_validate_format.log`
    (9.4s), and
    `TestOutput\agent_logs\plan01_interaction_after_render_split_runtime_boundaries.log`
    (17.6s). The touched-file comment audit covered
    `RunInteractionAutomation.cpp`.
  - [x] Interaction automation before-input replay-control split moved the
    replay scrubber control-click and solver-track scrub injection branches into
    source-local helpers with explicit automation, subsystem, config, scene,
    timer, and replay owner references. `Run::TickInteractionAutomationBeforeInput()`
    dropped from 578 measured lines to 365 measured lines without adding public
    or private `Run` methods. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (365), `TakeInput` (201),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport`
    (197), and `Execute` (196), so Phase 3 and the structural acceptance rows
    remain open. Gate evidence:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_validate_full.log`
    (44.0s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Focused interaction/replay checks passed:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_validate_interaction_clicks.log`
    (17.4s) and
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_validate_replay_scrub.log`
    (21.0s). Targeted pre-gate checks also passed after an initial namespace
    qualification build failure:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_build_profile_rerun.log`
    (9.0s),
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_validate_format.log`
    (9.4s), and
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_controls_runtime_boundaries.log`
    (17.7s). The touched-file comment audit covered
    `RunInteractionAutomation.cpp`.
  - [x] Interaction automation before-input director/camera split moved
    shot-list loading, director playback controls, phase style, camera pose,
    and camera-mode actions into source-local
    `ApplyInteractionAutomationDirectorCameraAction()` with explicit
    automation, subsystem, and camera owner references plus callback-only access
    to the remaining `Run::ApplyCameraMode()` transition. `Run::TickInteractionAutomationBeforeInput()`
    dropped from 365 measured lines to 247 measured lines without adding public
    or private `Run` methods. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946),
    `TickInteractionAutomationBeforeInput` (247), `TakeInput` (201),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport`
    (197), and `Execute` (196), so Phase 3 and the structural acceptance rows
    remain open. Gate evidence:
    `TestOutput\agent_logs\plan01_interaction_before_input_director_camera_validate_full.log`
    (52.5s; project filters/runtime boundaries passed, Profile/Debug builds
    had 0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots
    matched baselines, and `physics_regression_solver.csv` matched
    byte-exactly). Focused interaction check passed:
    `TestOutput\agent_logs\plan01_interaction_before_input_director_camera_validate_interaction_clicks.log`
    (16.1s). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_interaction_before_input_director_camera_build_profile.log`
    (9.1s),
    `TestOutput\agent_logs\plan01_interaction_before_input_director_camera_validate_format.log`
    (9.3s), and
    `TestOutput\agent_logs\plan01_interaction_before_input_director_camera_runtime_boundaries.log`
    (17.6s). The touched-file comment audit covered
    `RunInteractionAutomation.cpp`.
  - [x] Interaction automation before-input replay-state split moved scrubber
    visibility, prediction enable/target/horizon, and path-target velocity nudge
    actions into source-local `ApplyInteractionAutomationReplayStateAction()`
    with explicit automation, timer, replay, and model owner references plus
    callback-only access to the remaining replay target lookup and world-owner
    transition hooks. `Run::TickInteractionAutomationBeforeInput()` dropped from
    247 measured lines to 150 measured lines without adding public or private
    `Run` methods. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (946), `TakeInput` (201),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport` (197),
    and `Execute` (196), so Phase 3 and the structural acceptance rows remain
    open. Gate evidence:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_validate_full.log`
    (43.6s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Focused interaction/replay checks passed:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_validate_interaction_clicks.log`
    (16.4s) and
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_validate_replay_scrub.log`
    (19.4s). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_build_profile.log`
    (9.1s),
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_validate_format.log`
    (9.2s), and
    `TestOutput\agent_logs\plan01_interaction_before_input_replay_state_runtime_boundaries.log`
    (18.1s). The touched-file comment audit covered
    `RunInteractionAutomation.cpp`.
  - [x] Replay restore event-helper split moved Debug restore diagnostics,
    world/launcher/generated replay event handling, editor placement replay, and
    editor transform replay into source-local helpers with explicit owner
    references. `Run::RestoreReplayV2ArtifactTargetState()` dropped from 946
    measured lines to 704 measured lines without adding public or private `Run`
    methods. The slice also fixed the generated-topology restore path by
    resetting `RunSceneState`'s scene-object id cursor after the restore-side
    generated rebuild clears the live collection; otherwise restoring a generated
    artifact from a mismatched live scene can regenerate bodies with shifted
    replay ids. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (704), `TakeInput` (199),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport` (197),
    and `Execute` (194), so Phase 3 and the structural acceptance rows remain
    open. Gate evidence:
    `TestOutput\agent_logs\plan01_replay_restore_event_helpers_validate_full_rerun.log`
    (45.1s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Focused replay v2 artifact validation passed after an initial generated
    topology restore failure exposed the scene-object id cursor bug:
    `TestOutput\agent_logs\plan01_replay_restore_event_helpers_validate_replay_v2_artifact_rerun.log`
    (59.2s; generated topology restore reported `generated_topology_rebuilt=1`
    and `bodies=6`). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_replay_restore_event_helpers_validate_format_rerun.log`
    (9.3s) and
    `TestOutput\agent_logs\plan01_replay_restore_event_helpers_runtime_boundaries_rerun.log`
    (17.6s). The touched-file comment audit covered `RunFrame.cpp`.
  - [x] Replay restore setup-helper split moved v2 artifact loading, target hash
    selection, checkpoint selection, checkpoint/live topology matching,
    generated config lookup, and generated-topology rebuild into source-local
    helpers with explicit owner references. `Run::RestoreReplayV2ArtifactTargetState()`
    dropped from 704 measured lines to 503 measured lines without adding public
    or private `Run` methods. The remaining measured large `Run::` targets are
    `RestoreReplayV2ArtifactTargetState` (503), `TakeInput` (199),
    `RunGraphicsStressActions` (199), `WriteInteractionAutomationReport` (197),
    and `Execute` (194), so Phase 3 and the structural acceptance rows remain
    open. Gate evidence:
    `TestOutput\agent_logs\plan01_replay_restore_setup_helpers_validate_full.log`
    (45.2s; project filters/runtime boundaries passed, Profile/Debug builds had
    0 warnings and 0 errors, DX12 InfoQueue errors = 0, screenshots matched
    baselines, and `physics_regression_solver.csv` matched byte-exactly).
    Focused replay v2 artifact validation passed:
    `TestOutput\agent_logs\plan01_replay_restore_setup_helpers_validate_replay_v2_artifact.log`
    (62.3s). Targeted pre-gate checks also passed:
    `TestOutput\agent_logs\plan01_replay_restore_setup_helpers_validate_format_rerun.log`
    (9.5s) and
    `TestOutput\agent_logs\plan01_replay_restore_setup_helpers_runtime_boundaries.log`
    (17.7s). The touched-file comment audit covered `RunFrame.cpp`.

## Validation

`tools\validate_full.bat` (Run/Runtime changes). Interaction-automation suite
after each phase.

## Acceptance (structural)

- [ ] No single `Run` function exceeds ~200 lines; `TakeInput` is a table +
  dispatch loop.
- [ ] `Run` public-method and owned-member counts drop materially from the
  audit baseline (~60 public methods, ~40 members).
- [x] `RunState` field count is measurably reduced; no external file mutates a
  `RunState` sub-field directly.
- [ ] `Run` no longer implements subsystem logic — it coordinates owners.
