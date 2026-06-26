/*
File: SkullbonezSource/Runtime/Run.h
Purpose:
  Coordinates the main game loop and high-level runtime lifecycle.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  DX11/OpenGL: Retired runtime renderers. Their source backends have been
  removed; old command-line values now fail early.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
  CLI (Command-Line Interface): Text arguments or scripts used to launch
  validation and tooling paths.

Invariants:
  - Run is the composition root for process-lifetime runtime systems.
  - Public startup code should configure Run through the small launch surface
    below instead of reaching into runtime-owned state.

Related:
  - SkullbonezSource/Runtime/Run.cpp
  - SkullbonezSource/Runtime/RunState.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderResources.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>
#include "../Core/Common.h"
#include "CameraCollection.h"
#include "InputController.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "EngineContext.h"
#include "RuntimeInteractionController.h"
#include "RuntimeCommandQueue.h"
#include "RuntimeCameraMode.h"
#include "Render/RuntimeRenderHost.h"
#include "Render/RuntimeRenderInputs.h"
#include "Render/RuntimeRenderer.h"
#include "RunReplayProbeState.h"
#include "RunState.h"
#include "RuntimeViewModel.h"
#include "Replay/ReplayRuntime.h"
#include "Scene/SceneAuthoredSetup.h"
#include "Scene/SceneController.h"
#include "Scene/SceneGeneratedSetup.h"
#include "Scene/SceneRuntimeCoordinator.h"
#include "SimulationController.h"
#include "../Assets/TextureCollection.h"
#include "Window.h"
#include "../Rendering/Text.h"
#include "../World/Terrain.h"
#include "../World/SkyBox.h"
#include "../Maths/GeometricMath.h"
#include "../GameObjects/GameModelCollection.h"
#include "../World/WorldEnvironment.h"
#include "../Rendering/RenderSceneView.h"
#include "../Scene/TestScene.h"
#include "../Physics/Debug/BroadphaseVisualizer.h"
#include "../Physics/Debug/CollisionVisualizer.h"
#include "../Physics/Debug/PhysicsDebugVisualizer.h"
#include "Tools/RuntimeTools.h"
#include "../UI/UI.h"


namespace SkullbonezCore
{
namespace Basics
{
enum class RuntimeInteractionCommandType
{
    None,
    SetEditorSelection
};

enum class RuntimeInteractionSelectionScope
{
    Editor,
    Inspect
};

struct RuntimeInteractionCommand
{
    RuntimeInteractionCommandType type = RuntimeInteractionCommandType::None;
    int modelIndex = -1;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
    bool claimSelectionOwner = true;
};

enum class RuntimeInteractionEventType
{
    None,
    SelectionChanged
};

struct RuntimeInteractionEvent
{
    RuntimeInteractionEventType type = RuntimeInteractionEventType::None;
    int previousModelIndex = -1;
    int modelIndex = -1;
    RuntimeInteractionSelectionScope selectionScope = RuntimeInteractionSelectionScope::Editor;
};

/* -- Skullbonez Run
---------------------------------------------------------------------------------------------------------------------------------------------

    Harness for the Skullbonez Core graphics library.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Run
{

  private:
    // Composition-root state: these members either own a top-level subsystem for
    // process lifetime/order, or keep launch/session choices that coordinate
    // multiple subsystems and therefore do not have one narrower owner yet.
    SceneController m_sceneController;                                     // Owns scene queue and current scene-run state
    SceneRuntimeCoordinator m_sceneCoordinator;                            // Owns scene load/reset/advance selection decisions.
    RunSceneBrowserState m_sceneBrowser;                                   // Discovered scene paths and live cine/concept selection.
    RunInputLatchState m_inputLatches;                                     // Cross-frame key/mouse latches that are not semantic input state.
    RunLaunchOptions m_launchOptions;                                      // CLI/startup policy reapplied across scene loads.
    CinematicRenderConfig m_defaultCinematicRender;                        // engine.cfg cinematic baseline restored by the Demo Scene cine mode
    RunStartupState m_startup;                                             // engine.cfg startup capacity/thread defaults restored by demo resets.
    RunSceneUIOverrideState m_sceneUIOverrides;                            // Live Scene-tab overrides preserved across reset when requested.

    // Subsystem owners below are ordered by lifetime dependency. EngineContext
    // and render-host bindings borrow from these objects; they do not own them.
    DiagnosticsRuntime m_diagnosticsRuntime;                               // Capture, perf, and queryable physics diagnostics owner.
#ifdef _DEBUG
    RunReplayScrubProbeState m_replayScrubProbe;                           // CLI-only SkullScope replay scrub self-test state.
    RunReplayRestoreProbeState m_replayRestoreProbe;                       // CLI-only solver restore hash self-test state.
    RunReplaySaveProbeState m_replaySaveProbe;                             // CLI-only v2 replay artifact save self-test state.
#endif
    RunRuntimeSettings m_runtimeSettings;                                  // Scene/app runtime swap policy toggles
    RunTimerState m_timers;                                                // Frame/simulation timers and rolling timing values
    RunSubsystemState m_systems;                                           // Window, camera, texture, terrain, and pass resource ownership
    RuntimeInputContext m_runtimeInput;                                    // Semantic input mode/action state owned by input routing.
    RuntimeInteractionController m_interaction;                            // Authoritative runtime workspace and world-input owner.
    RunInteractionAutomationState
        m_interactionAutomation;                                           // CLI harness that injects runtime mouse input for regression tests.
    RunCameraState m_camera;                                               // Camera/input state and ball-tracking settings
    AttachedCameraState m_attachedCamera;                                  // Non-serialized object-follow camera state for Attach mode.
    SimulationController m_simulation;                                     // Simulation timestep policy and physics accumulators
    ReplayRuntime m_replayRuntime;                                         // Owns replay recorders, branch provenance, and replay interaction state.
    RunReplayMismatchState m_solverReplayMismatch;                         // Throttles repeated live-vs-solver replay mismatch reports.
    RunLiveStyleControlState m_liveStyle;                                  // Live style tweak/capture harness state
    UI::InGameUI m_UI;                                                     // Encapsulated in-game diagnostics window
    RunDebugState m_debug;                                                 // Runtime debug/overlay toggles
    RuntimeTools m_runtimeTools;                                           // Launcher, editor, manipulator state, and transient render feedback.
    std::vector<RunRequiredContactState> m_requiredSceneContacts;
    std::vector<RunRequiredBroadphaseXCellsState> m_requiredBroadphaseXCells;
    Physics::BroadphaseVisualizer m_broadphaseVisualizer;                  // Spatial grid debug overlay (G key toggle)
    Physics::CollisionVisualizer m_collisionVisualizer;                    // Solid collision/sleep model visualizer (V key toggle)
    Physics::PhysicsDebugVisualizer
        m_physicsDebugVisualizer;                                          // Line overlay for object axes, contact manifolds, and sleep state
    Environment::WorldEnvironment m_cWorldEnvironment;                     // Fluid, gravity, and terrain bounds shared by physics and water.
    GameObjects::GameModelCollection m_cGameModelCollection;               // Scene bodies plus solver-visible object state.
    RuntimeCommandQueue m_runtimeCommands;                                 // Deferred runtime/tool command intent.
    EngineContext m_engineContext;                                         // Bound view over runtime-owned systems.
    RuntimeViewModel m_runtimeViewModel;                                   // Scalar runtime snapshot for presentation/diagnostics.
    std::array<float, MAX_GAME_MODELS * 16> m_dxrReflectionTransforms = {};
    RuntimeRenderHost m_renderHost;                                        // Explicit render-facing service view over Run-owned state.
    RuntimeRenderer m_renderer;                                            // Owns runtime render passes and frame render ordering.

    inline static int sPerfPass = 0;
    void Render();                                                         // Skips 3D in text-only runs, then records passes for the current camera state.
    RunSceneState& SceneState();                                           // Mutable scene-run state owned by SceneController
    const RunSceneState& SceneState() const;                               // Read-only scene-run state owned by SceneController
    void BindEngineContext();                                              // Binds runtime-owned systems into EngineContext
    void RefreshRuntimeViewModel();                                        // Rebuilds scalar presentation state from EngineContext
    void RelativeUpdateCamera( uint32_t hash );                            // Keeps non-selected relative cameras inside terrain height limits.
    void UpdateLogic( float simulationDt, float cameraDt );                // simulationDt drives physics; cameraDt is unscaled wall time.
    void TakeInput();                                                      // Applies focused input to camera, UI, scene cycling, diagnostics, and editor tools.
    void TickInteractionAutomationBeforeInput();                           // Applies scripted mouse/button state before normal input routing.
    void TickInteractionAutomationAfterRender();                           // Runs assertions/screenshots and finishes scripted automation.
    void ClearInteractionAutomationInput();                                // Releases input overrides after completion or failure.
    void WriteInteractionAutomationReport();                               // Writes JSON result for --interaction-report.
    bool TryFindInteractionAutomationModel( const char* name, int& outIndex ) const;
    bool TryProjectInteractionAutomationModel( const char* name, POINT& outMouse ) const;
    bool DrainRuntimeCommands();                                           // Applies queued runtime/tool command intents at the frame boundary.
    SceneRuntimeCoordinatorCallbacks BuildSceneRuntimeCoordinatorCallbacks();
    SceneAuthoredCameraContext BuildSceneAuthoredCameraContext();
    SceneAuthoredModelContext BuildSceneAuthoredModelContext();
    SceneGeneratedCameraContext BuildSceneGeneratedCameraContext();
    SceneGeneratedModelContext BuildSceneGeneratedModelContext();
    void StepPhysicsPipelineStage( int direction );                        // direction is a left/right cursor step for pipeline visualization.
    void UpdateRuntimeInputModeAfterAction(
        RuntimeInputAction action,
        RuntimeInputActionSource source );                                 // Records the mode transition caused by one runtime/tool action.
    RuntimeInputSnapshot BuildRuntimeInputSnapshot( const RuntimeMouseEdges& mouseEdges,
                                                    bool suppressWorldActionThisFrame )
        const;                                                             // Captures pointer/UI/frame-policy input once for routed world input.
    bool
    RouteRuntimePointerInput( const RuntimeInputSnapshot& inputSnapshot,
                              const RuntimeMouseEdges& mouseEdges );       // Routes pointer input after snapshot capture.
    void CancelCameraLookGesture();                                        // Clears controller-owned camera-look pointer capture.
    void SyncCameraLookGesture( const RuntimeInputSnapshot& inputSnapshot,
                                const RuntimeInteractionFramePolicy& inputPolicy,
                                bool mouseLookOwnsCursor );                // Mirrors camera-look policy into pointer capture state.
    void BeginReplayToolGesture( RuntimeInteractionGestureKind kind,
                                 WorldInteractionOwner owner,
                                 RuntimePointerButton button,
                                 int startX,
                                 int startY,
                                 int modelIndex = -1,
                                 int axis = -1,
                                 bool angular = false );                   // Captures typed replay drag ownership.
    void EndReplayToolGesture( RuntimeInteractionGestureKind kind );       // Releases a matching typed replay drag gesture.
    void CancelReplayToolGesture();                                        // Clears any active replay drag gesture from the controller.
    void CancelReplayToolDragState();                                      // Releases controller capture and legacy replay drag booleans together.
    RuntimeInteractionTransition EnterInteractionForCameraMode(
        RunCameraMode mode );                                              // Converts camera/tool requests into controller workspace transitions.
    void ApplyRuntimeInteractionTransitionCleanup(
        const RuntimeInteractionTransition&
            transition );                                                  // Clears stale tool ownership before the new mode consumes input.
    void ClearReplayInteractionForRuntimeTransition();                     // Clears replay-owned scrub, prediction, velocity, cause, and
                                                       // path state.
    void ClearEditorInteractionForRuntimeTransition(
        bool clearSelection );                                             // Clears editor placement/gizmo ownership before leaving Edit.
    bool HasActiveReplayInteractionState() const;                          // True when replay owns transient input or historical presentation.
    bool HasActiveEditorInteractionState() const;                          // True when editor owns placement/gizmo/input state.
    bool InspectGizmoInteractionActive() const;                            // True when Inspect owns live transform-gizmo interaction.
    bool ReplayInspectionActive() const;                                   // True when replay owns inspection camera semantics.
    bool ReplayInspectionMouseLookActive() const;                          // True when replay inspection is consuming mouse-look.
    bool MouseLookOwnsCursor() const;                                      // True while RMB/editor/replay mouse-look temporarily owns the cursor.
    bool ShouldHideNativeCursor() const;                                   // True when the current tool mode should hide the Windows cursor.
    void ApplyCursorOwnership();                                           // Applies current cursor ownership to the system cursor.
    void ReleaseMouseToUI();                                               // Gives mouse focus back to Win32/UI when tools stop owning it.
    void EnterFlyModeCamera();                                             // Switches camera state into free-flight controls.
    void ExitFlyModeCamera();                                              // Restores terrain camera bounds and leaves launcher mode.
    const char* CameraModeLabel( RunCameraMode mode ) const;               // Compact name for UI and transition diagnostics.
    uint32_t CameraModeEnabledMask() const;                                // One bit per camera mode; disabled modes remain visible in UI.
    bool IsDemoCameraModeAvailable() const;                                // True when Demo can track at least one live model.
    RunCameraMode NormalizeCameraModeForCurrentScene(
        RunCameraMode mode ) const;                                        // Clamps passive camera modes to generated-demo vs authored-scene ownership.
    void SetCameraModeLabelAfterInteractionTransition(
        RunCameraMode mode );                                              // Applies the camera label after controller workspace/tool ownership is chosen.
    RuntimeInteractionTransition SetWorldInteractionOwnerAfterInteractionTransition(
        WorldInteractionOwner owner,
        InteractionExitReason reason );                                    // Applies tool-owner transitions through runtime cleanup.
    bool ExecuteRuntimeInteractionCommand(
        const RuntimeInteractionCommand& command );                        // Applies synchronous interaction mutations from routed input.
    void PublishRuntimeInteractionEvent(
        const RuntimeInteractionEvent& event );                            // Emits observation-only command-result events after commands succeed.
    void ClearRuntimeInteractionStateForTransition(
        const RuntimeInteractionTransition& transition );                  // Clears state owned by the interaction being exited.
    bool IsManualCameraMode() const;                                       // True when passive generated-demo systems must not move the view.
    bool IsFlyCameraMode() const;                                          // True when the current mode uses free-flight camera controls.
    bool IsLauncherCameraMode() const;                                     // True when the current mode owns launcher firing semantics.
    bool IsManipulatorCameraMode() const;                                  // True when mouse pickup owns world left-drag semantics.
    bool IsAttachedCameraMode() const;                                     // True when Attach owns follow/pin camera semantics.
    void ApplyCameraMode( RunCameraMode mode,
                          RuntimeInputActionSource source );               // Applies keyboard/UI camera-mode requests.
    void CycleCameraMode();                                                // Tab cycles through enabled explicit camera modes.
    void ResetAttachedCamera();                                            // Clears non-serialized attach target and camera offsets.
    void CaptureAttachedCameraReturnState(
        RunCameraMode previousMode );                                      // Saves the camera mode/pose Attach should restore on exit.
    void RestoreAttachedCameraReturnState();                               // Restores the saved pre-Attach pose when returning to that mode.
    bool TryResolveAttachedCameraTarget(
        int& outModelIndex );                                              // Revalidates/recover target by index, replay id, or exact name.
    void SetAttachedCameraTarget( int modelIndex );                        // Stores exact clicked/seeded model identity and captures offset.
    void ClearAttachedCameraTarget();                                      // Clears follow target but preserves current camera world pose.
    void SeedAttachedCameraTargetFromSelection();                          // Initializes Attach from replay/editor selection when possible.
    bool TryPickAttachedCameraTargetFromMouse();                           // Mouse ray pick through the shared runtime pick service.
    bool
    TickAttachedCameraWorldClick( const RuntimeMouseEdges& mouseEdges,
                                  bool suppressWorldActionThisFrame );     // Consumes Attach left-click target selection.
    void CycleAttachedCameraSubmode();                                     // F1 cycles Fixed, Velocity, and available Eyes modes.
    void ToggleAttachedCameraPin();                                        // Enter pins/unpins camera follow while in Attach.
    void TickAttachedCameraOrbitInput( int unhandledWheelDelta );          // Mouse wheel adjusts Attach orbit distance.
    void TickAttachedCamera();                                             // Applies the active follow solve to CameraCollection.
    void CaptureAttachedCameraFixedOffset( const GameObjects::GameModel& model );
    void CaptureAttachedCameraOrbit(
        const GameObjects::GameModel& model );                             // Seeds upright Attach orbit from the current camera pose.
    bool TryResolveAttachedCameraRagdollHead( int selectedModelIndex, int& outHeadModelIndex ) const;
    void SetUpCameras();                                                   // Creates generated-demo cameras when no scene file supplies them.
    void UpdateRequiredSceneContacts();                                    // Scene automation waits for authored contact gates to appear in live physics
                                        // contacts.
    void UpdateRequiredSceneBroadphaseXCells(
        const Math::CollisionDetection::SpatialGrid::ActiveCell* activeCells,
        int activeCellCount );                                             // Scene automation waits for authored X-cell ranges to appear in the live grid.
    bool RequiredSceneContactsComplete() const;                            // True when there are no gates or all gates have been touched
    bool RequiredSceneBroadphaseXCellsComplete()
        const;                                                             // True when there are no gates or all X-cell ranges have been activated
    void RegisterBuiltInAssets();                                          // Seeds source asset records before renderer-owned resources are rebuilt.
    std::string ResolveSourceAssetPath( Assets::AssetKind kind,
                                        const char* logicalName,
                                        const std::string& relativePath ); // Resolves DATA_ROOT path while preserving
                                                                           // source asset identity for rebuilds.
    void DrawPrimitives();                                                 // Thin compatibility wrapper around RuntimeRenderer::RenderFrame().
    RuntimeRenderHostBindings BuildRuntimeRenderHostBindings();
    RuntimeRenderHostCallbacks BuildRuntimeRenderHostCallbacks();
    RuntimeRenderServices BuildRuntimeRenderServices();
    RuntimeRenderInputs BuildRuntimeRenderInputs();
    CinematicRenderConfig& ActiveCinematicConfig();                        // Mutable cinematic style config for the active scene/run
    const CinematicRenderConfig& ActiveCinematicConfig() const;            // Read-only cinematic style config for the active scene/run
    bool IsCinematicRenderingEnabled() const;                              // True when the HDR/post stack should wrap the main scene
    void ReleaseBackendOwnedRenderResources(
        const char* phaseName );                                           // Ordered GPU-resource release hook while the backend is alive.
    void RebuildRegisteredRenderResources();                               // Recreates renderer resources from source asset records
    void LogRenderResourceLifecycleStep( const char* phase, const char* step )
        const;                                                             // Debug event log record for a named resource-lifetime phase.
    Textures::TextureCollection& Textures();                               // Runtime texture registry accessor used by render passes
    uint32_t TextureHandle( uint32_t textureHash );                        // Resolves a runtime texture hash to a renderer handle
    void SelectRenderTexture( uint32_t textureHash );                      // Runtime texture hash selected for the default draw texture slot.
    int WindowScreenWidth() const;                                         // Client width, falling back to config before window init.
    int WindowScreenHeight() const;                                        // Client height, falling back to config before window init.
    void SetViewingOrientation();                                          // Camera-view setup for the current frame.
    void SaveScreenshot( const char* path );                               // Backbuffer capture path; current encoder writes BMP files.
    bool SaveReplayBufferFromScrubber( RunReplayTrack track );             // Writes one retained in-memory replay track to replays/.
    bool PromptLoadReplayPresentationArtifact( HWND hwnd );                // Open a .skreplay picker for a v2 scrub source.
    bool SaveCurrentSceneDefaults();                                       // UI-controlled scene defaults persisted to the active scene file.
    bool SaveCurrentEditableSceneSnapshot();                               // UI-created scenes persist live models plus starter-scene defaults.
    bool SaveRenderDefaults();                                             // Ordinary Render-tab values persisted to engine.cfg.
    bool SaveSkyDefaults();                                                // Sky-tab cinematic values persisted to engine.cfg.
    void RefreshSceneBrowserList();                                        // Discovers scene files available to the in-game scene dropdown
    int CurrentSceneBrowserIndex() const;                                  // Selected discovered-scene dropdown index.
    bool CreateSceneFromUI( const char* requestedName );                   // Creates and loads a flat starter scene from the Scene tab.
    void LoadSceneFromBrowserIndex( int index );                           // In-game scene dropdown selection loader.
    void LoadDemoSceneFromUI();                                            // Scene-tab entry point for the generated demo scene.
    bool ApplyCinematicModeFromBrowserIndex( int index );                  // Live cine/concept style change; leaves scene objects intact.
    bool ApplyAdjacentCinematicMode( int direction );                      // Cycles live cine/concept looks without rebuilding the scene
    void ApplyLiveStyleScene( const TestScene& styleScene );               // Style-only cinematic/material JSON; no object rebuild.
    void ApplyDemoHeroStyleOverride();                                     // Low-poly hero style override for generated demo mode.
    void LoadAdjacentSceneFromBrowser( int direction );                    // Keyboard scene cycling through the discovered scene dropdown list
    void EnterInteractiveSceneRun();                                       // Locks scene automation into non-quitting interactive mode
    bool CanSceneAutomationQuit() const;                                   // True for CLI suites/tests; false once the user owns scene flow
    void HoldCompletedInteractiveScene();                                  // Keep the current scene alive after interactive automation completes
    bool WriteMainMemoryDump( const char* checkpoint );                    // Writes CLI-requested process/replay/object memory JSON.
    void LoadScene(
        int index,
        bool preserveUIState = false,
        bool suppressExitOnComplete = false,
        bool preserveRuntimeState = false );                               // Queue-indexed scene load; preserve flags keep selected runtime/UI state.
    void ResetCurrentScene(
        bool preserveUIState = false,
        bool suppressExitOnComplete = false,
        bool preserveRuntimeState = true );                                // User-triggered reset/reload of current scene or generated demo mode
    void ApplyUIModelCountOverride( int count );                           // Rebuilds the active generated model pool from the UI slider
    void ApplyUISolverObjectCounts( int balls, int boxes );                // Rebuilds generated solver objects from exact UI counts
    void ApplyUIWorldOverride( float gravity,
                               float fluidHeight,
                               float fluidDensity );                       // Live world/fluid scalar override from UI controls.
    void ApplyConfiguredWorldEnvironment();                                // Restores engine.cfg world/fluid defaults for a fresh scene load.
    void ApplyNoWaterOverride();                                           // Pushes fluid surface below the active terrain when requested
    void ApplyTornadoDefaultsForActiveScene();                             // Centers the tornado around the active inner-water/basin region
    void SyncTornadoFieldToPhysics();                                      // Sends live tornado state to the physics collection
    void UseDefaultTerrain();                                              // Restores the normal height-map terrain when leaving analytic test scenes
    void UseFlatSlopeTerrain( float baseY,
                              float slopeX,
                              float slopeZ );                              // Activates analytic flat-slope terrain for focused physics scenes
    void UpdateWorldTerrainBounds();                                       // Keeps world/fluid helpers aligned with the active terrain bounds
    bool AdvanceScene();                                                   // Advances to the next scene in the queue (returns false if done)
    void MoveCamera( float keyMovementQty,
                     float mouseMovemementQty );                           // Keyboard/mouse deltas dispatched to CameraCollection.
    // Tight light-space frame for nearby object receivers.
    // Depth casters requested from the sun view.
    unsigned int NextUIStressRandom();
    int NextUIStressInt( int maxExclusive );
    float NextUIStressFloat( float minValue, float maxValue );
    void RunUIStressActions();
    void ResetReplayTimelineForActiveScene(
        bool preserveBranchMetadata = false );                             // Scene/model rebuilds start a fresh in-memory replay branch.
    ReplayFrameIndex NextReplayEventFrameIndex() const;                    // Event frame cursor matching the next captured physics tick.
    void RecordReplayEvent( ReplayEventKind kind,
                            ReplayFrameIndex frameIndex,
                            uint32_t flags,
                            int32_t value0,
                            int32_t value1,
                            int32_t value2,
                            int32_t value3,
                            uint64_t data0,
                            const char* text );                            // Appends a bounded v2 event-stream row when replay is active.
    void RecordReplayWorldOverrideEvent(
        float previousGravity,
        float previousFluidHeight,
        float previousFluidDensity,
        float gravity,
        float fluidHeight,
        float fluidDensity );                                              // Records exact world scalar payloads for future event replay.
    void RecordReplayLauncherConfigEvent(
        uint32_t changedFlags );                                           // Records launcher settings that affect future fire events.
    void RecordReplayLauncherFireEvent(
        const Math::Vector::Vector3& rayOrigin,
        const Math::Vector::Vector3& rayDirection,
        const Math::Vector::Vector3& cameraUp );                           // Records camera-derived launcher fire payloads.
    void RecordReplayGeneratedSceneConfigEvent();                          // Records generated-scene object counts and seed metadata.
    void
    RecordReplayEditorPlaceEvent( int objectType,
                                  bool fixedObject,
                                  bool terrainAlign,
                                  int modelCountBefore,
                                  const Math::Vector::Vector3& terrainPoint,
                                  const Math::Vector::Vector3& placementScale,
                                  float placementYawRadians );             // Records editor placement commits for saved v2 replay.
    void
    RecordReplayEditorTransformEvent( int modelIndex,
                                      uint32_t changedFlags,
                                      const GameObjects::GameModel& model,
                                      int scaleAxis,
                                      float scaleFactor );                 // Records committed editor transform/scale gizmo changes.
    void CaptureReplayPhysicsStep();                                       // Capture-only hook after one committed fixed physics tick.
    static void CaptureReplayPhysicsStepThunk( void* userData );
    void AfterPhysicsStep();                                               // Post-step hooks that must see committed physics state.
    static void AfterPhysicsStepThunk( void* userData );
    void ApplyMousePickupPhysicsStep();                                    // Manipulator spring impulse before one fixed physics step.
    void RestoreMousePickupAngularVelocity();                              // Holds grabbed body angular velocity stable during drag.
    static void ApplyMousePickupPhysicsStepThunk( void* userData );
    void BuildReplayLauncherVisualSample( ReplayLauncherVisualSample& outSample ) const;
    void RestoreReplayLauncherVisualSample( const ReplayLauncherVisualSample& sample );
    bool ApplyReplayEventForRestoreTarget(
        const ReplayEventSample& event,
        char* outReason,
        std::size_t reasonSize );                                          // Applies loaded v2 event payloads without recording new replay rows.
    void ClearReplayPathVisualizer();
    bool TryPickReplayPathTargetFromMouse( bool additive, bool clearOnMiss );
    void MarkReplayPredictionDirty();
    void ClearReplayPredictionCache();
    void CancelReplayPredictionJob( bool clearSamples );
    // Prediction work shares the replay visualizer deadline. These calls may
    // leave prediction dirty/building so a later frame can resume without
    // exceeding the current render-frame budget.
    bool BeginReplayPredictionJob( ReplayFrameIndex sourceFrameIndex,
                                   uint64_t sourceSolverHash,
                                   const std::chrono::steady_clock::time_point& budgetStart,
                                   double budgetMilliseconds );
    bool StepReplayPredictionJob( const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds );
    bool CaptureReplayPredictionBodyState( std::vector<RunReplayPredictionBodyBackup>& outBodies );
    bool ApplyReplayPredictionBodyState( const std::vector<RunReplayPredictionBodyBackup>& bodies );
    void CaptureReplayPredictionFrame( ReplayFrameIndex frameIndex );
    void RenderReplayPredictionVisualizer( RunEditorTracer& tracer,
                                           const std::chrono::steady_clock::time_point& budgetStart,
                                           double budgetMilliseconds );
    void RenderReplayPathVisualizer( RunEditorTracer& tracer );
    bool TickReplayCauseTreeInput( HWND hwnd, bool uiBlocksMouse, int wheelDelta );
    bool TryResolveReplayCauseTreeBodyPosition( ReplayBodyId id,
                                                Math::Vector::Vector3& outPosition,
                                                float* outRadius = nullptr ) const;
    bool FocusReplayCauseTreeBody( ReplayBodyId id );
    void ActivateReplayCameraForCauseRow( const RunReplayCauseTreeRow& row, int rowIndex );
    void ClearReplayCameraFocus( bool restoreCamera );
    void RenderReplayCauseFocusOverlay( RunEditorTracer& tracer );
    void SetReplayVelocityEditEnabled( bool enabled );
    bool TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse );
    int ResolveReplayVelocityEditModelIndex() const;
    int HitReplayVelocityLinearAxis( const Math::Vector::Vector3& rayOrigin,
                                     const Math::Vector::Vector3& rayDirection ) const;
    int HitReplayVelocityAngularAxis( const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection ) const;
    bool TryReplayVelocityAxisRayParameter( int axis,
                                            const Math::Vector::Vector3& rayOrigin,
                                            const Math::Vector::Vector3& rayDirection,
                                            float& outAxisT ) const;
    bool TryReplayVelocityAngularRayAngle( int axis,
                                           const Math::Vector::Vector3& rayOrigin,
                                           const Math::Vector::Vector3& rayDirection,
                                           float& outAngle ) const;
    void ApplyReplayVelocityEditDrag( const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection );
    void ApplyReplayVelocityEditToModel( int modelIndex,
                                         const Math::Vector::Vector3& linearVelocity,
                                         const Math::Vector::Vector3& angularVelocity );
    void RenderReplayVelocityEditOverlay( RunEditorTracer& tracer );
    void ArmLoadedReplayPresentationScrubber( float normalized );
    void ResetReplayScrubber();
    void SetReplayLiveAdvanceHeld( bool held );
    void EnterReplayInspectionCamera();
    void ExitReplayInspectionCamera();
    void UpdateReplayInspectionCamera();
    void CompareLatestReplaySamples();
    bool TickReplayScrubberInput( HWND hwnd, bool uiBlocksMouse );
    bool RestoreReplayScrubberSelectionAsLive( double now,
                                               RunReplayV2TargetRestoreResult* outV2Result = nullptr,
                                               char* outReason = nullptr,
                                               std::size_t reasonSize = 0 );
    void ApplyReplayRenderStateForFrame();
    void RestoreReplayRenderStateForFrame();
    void ApplyReplayLauncherVisualSampleForRender( const ReplayLauncherVisualSample& sample );
    void RestoreReplayLauncherVisualForRender();
    bool ApplyReplaySolverSampleState( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize );
    bool CaptureCurrentReplaySolverHash( const ReplaySolverFrameSample& reference,
                                         uint64_t& outSolverHash,
                                         uint64_t& outPresentationHash,
                                         std::size_t& outBodyCount );
    bool RestoreReplayV2ArtifactTargetState( const char* path,
                                             ReplayFrameIndex requestedFrame,
                                             bool makeLiveBranch,
                                             RunReplayV2TargetRestoreResult& outResult,
                                             char* outReason,
                                             std::size_t reasonSize );
    bool
    RestoreReplaySolverSampleAsLive( const ReplaySolverFrameSample& sample, char* outReason, std::size_t reasonSize );

    // --- Per-frame tick helpers (called from Execute()) ---
    void TickPhysics( double dt );                                         // Physics dispatch: fixed-step and variable-step accumulator
    bool TickScreenshots();                                                // Screenshot triggers; returns true when frame should restart (continue)
    void TickLiveStyleControl();                                           // Poll live.style.json/capture.txt and apply look changes without scene reload
    void TickLiveStyleControlCapture();
    void TickAutoCycle();                                                  // Auto-cycle ball capture; posts WM_QUIT when all balls captured
    void TickPerfLog();                                                    // Write per-frame perf CSV row and periodic memory checkpoint
    bool TickSceneAdvance();                                               // Frame count, exit/hold on completion, restarts; returns true to continue
    void UpdateWaterHeightControls( float dt );                            // Slide water surface up/down while held
    bool TryBuildMouseWorldRay( Math::Vector::Vector3& outOrigin, Math::Vector::Vector3& outDirection )
        const;                                                             // Mouse position projected into a world-space ray.
    void TickEditorViewportAndPlacementScaleInput(
        int unhandledWheelDelta );                                         // Updates viewport-look and placement scale/altitude gestures.
    bool TickEditorWorldClick(
        const RuntimeMouseEdges& mouseEdges,
        bool suppressWorldActionThisFrame );                               // Handles editor placement, selection, and gizmo mouse ownership.
    void CancelMousePickup();                                              // Releases manipulator drag/capture state.
    bool TickMousePickupInput(
        HWND hwnd,
        const RuntimeMouseEdges& mouseEdges,
        bool suppressWorldActionThisFrame );                               // Handles manipulator left-click pickup and target updates.
#ifdef _DEBUG
    void LogSceneFinished( const char* reason );
    void BeginPhysicsDiagnosticsRun( const char* scenePath );
    void TickReplayScrubProbe();
    void TickReplayRestoreProbe();
    void TickReplaySaveProbe();
    void EndPhysicsDiagnosticsRun( const char* status );
#endif

  public:
    Run( std::vector<std::string> sceneQueue );                            // sceneQueue empty string selects generated demo mode.
    ~Run();
    void Initialise();                                                     // Initialises shared resources and loads first scene
    void RunSceneLoadOnly( const char* snapshotOutPath = nullptr );        // Scene-load smoke path; skips the frame loop.
    void Execute();                                                        // Main message loop; sceneQueue decides generated demo versus suite playback.
    void SetTimeScaleOverride( float scale );                              // Override timeScale for every scene loaded (CLI --time-scale)
    void SetFixedStepOverride();                                           // Force fixed-step for every scene loaded (CLI --fixed-step)
    void SetSeedOverride( unsigned int seed );                             // Override RNG seed for every scene loaded (CLI --seed)
    void SetNoWaterOverride();                                             // Start scenes with fluid below terrain (CLI --no-water)
    void SetNoSleepOverride();                                             // Disable physics sleeping for every scene loaded (CLI --no-sleep)
    void SetTornadoOverride( bool enabled );                               // Enable/disable tornado mode for loaded scenes (CLI --tornado)
    void SetTornadoVectorFieldOverride( bool enabled );                    // Show/hide tornado velocity vectors at startup
    void SetCinematicRenderingOverride( bool enabled );                    // Force cinematic HDR/post rendering on/off for every scene loaded
    void SetCinematicShadowsOverride( bool enabled );                      // Force shadow maps on/off for every scene loaded
    void SetDemoHeroStyleOverride();                                       // Run generated demo mode with the low-poly hero rendering style
    void SetInteractiveRunOverride();                                      // Keep scene automation from quitting the app (CLI --interactive/--hold)
    void SetLiveStyleControlDirectory( const char* path );                 // Enable live style/capture harness in a control folder
    void SetFrameCountOverride( int frames );                              // Stop scene/demo automation after N frames (CLI --frames)
    void SetUIStressOverride( unsigned int seed, int actionsPerFrame );    // Enable deterministic UI stress from CLI
    void SetInteractionAutomation(
        const char* scriptPath,
        const char* reportPath );                                          // CLI harness for deterministic world-click interaction scripts.
    void SetReplayRecording( bool enabled,
                             int retentionSeconds,
                             const char* hashLogPath );                    // Enable bounded replay capture from CLI.
    void SetMainMemoryDumpPath( const char* path );                        // Write main-memory JSON at shutdown (CLI --memory-dump).
    bool LoadReplayPresentationArtifact( const char* path,
                                         bool activateScrubber );          // Load a v2 presentation artifact as a scrub source.
    void SetInitialOverlayMode( OverlayMode mode );
    void SetTopTextHidden( bool hidden );
    void SetBroadphaseVisualizerEnabled( bool enabled );
    void SetGeneratedObjectTypeOverride( GeneratedObjectTypeOverride objectTypeOverride );
    void SetPhysicsDebugFlagsOverride( uint32_t flags );
    void SetPhysicsDebugTransparentOverride( bool transparent );
    void SetPhysicsDebugAlphaOverride( float alpha );
    void SetPhysicsDebugContactLingerOverride( float seconds );
    void DumpTextureAssets( FILE* out ) const;

#ifdef _DEBUG
    void SetPhysicsRegressionLogOverride( const char* path );              // Override regression CSV path for all scenes
    void SetPhysicsCollisionTimeLogOverride( const char* path );           // Override swept collision-time CSV path for all scenes
    void SetPhysicsDiagnosticsPath(
        const char* path,
        bool fixedStepForcedByDiagnostics );                               // Enable queryable physics diagnostics (CLI --physics-diag)
    void SetReplayScrubProbe( float normalized );                          // Enable CLI-only replay scrub SkullScope probe.
    void SetReplayRestoreProbe( float normalized );                        // Enable CLI-only replay restore hash probe.
    void SetReplaySaveProbe( const char* path );                           // Enable CLI-only v2 replay save probe.
    void VerifyLoadedReplayPresentationProbe( float normalized );          // Validate runtime scrubbing from a loaded v2 file.
    void VerifyReplaySolverCheckpointFileProbe(
        const char* path );                                                // Validate hash-gated restore from a v2 solver checkpoint.
    void VerifyReplaySolverTargetFileProbe(
        const char* path );                                                // Validate checkpoint-plus-event replay to a saved non-checkpoint target.
    void VerifyReplaySolverBranchFileProbe(
        const char* path );                                                // Validate checkpoint-plus-event replay can become a live branch.
    void VerifyReplaySolverFailureFileProbe(
        const char* path );                                                // Validate saved-file restore failures emit SkullScope diagnostics.
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
