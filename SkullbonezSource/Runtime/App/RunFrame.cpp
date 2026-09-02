/*
Purpose:
  Runs one frame of input, simulation, rendering, profiling, and presentation.

Invariants:
  - Frame work updates input, simulation, capture, rendering, and diagnostics
    in a stable order used by validation and replay comparisons.
  - Capture pinning is decided before physics and camera work for that frame.
  - Delegated operations receive concrete operands and retain none.
  - A successful submitted game frame emits exactly one development profiler
    frame mark; failed or capture-only turns emit none.
  - A development surface swap hides the source before the target begins a frame.
  - Run sequences development UI automation but retains only process-wide
    surface selection and application-failure policy.
  - Physics and input owners publish complete policy/results; Run applies them
    without reconstructing or overriding their domain decisions.
  - Frame work enters only through an active Run renderer epoch; missing or
    teardown-closed renderer access terminates before phase dispatch.
  - Recording starts at a pre-input boundary, captures after routing, and commits
    only at the following boundary so F8 and scene-transition turns are excluded.
*/
#include "Run.h"
#include "InteractionAutomationApplication.h"
#include "SceneLoadApplication.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../Automation/RuntimeValidationHarness.h"
#include "../RuntimeFrameViews.h"
#include "../UI/OperatorUiPhase.h"
#include "../UI/RecordedCursorDrawing.h"
#include "../UI/RecordedCursorPresentationPolicy.h"
#include "../UI/RuntimeViewModel.h"
#include "RenderModelFramePublisher.h"
#include "../Startup/Window.h"
#include "../Input/Input.h"
#include "../../Core/WorkerPool.h"
#include "InputFrame.h"
#include "../Replay/ReplayRestoreTransactions.h"
#include "../Planning/ReplayOverlayPackets.h"
#include "../Direction/DemoDirectorPlayback.h"
#include "../Scene/SceneLoadTransaction.h"
#include "../Scene/SceneSaveOperations.h"
#include "../Scene/SceneGeneratedSetup.h"
#include "../../Scene/AuthoredScene.h"

#include "../Capture/CaptureSystem.h"
#include "GraphicsStressApplication.h"
#include "../Editor/EditorTools.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Core/TracyClientOwner.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../DevelopmentTools/ImGuiEditorOwner.h"
#endif
#include "../Scene/SceneCinematicPolicy.h"

#include "../../Core/FatalError.h"
#include "../../Core/Log.h"
#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsEngine.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsDiagnosticsSink.h"
#include "../../Physics/PhysicsTimestep.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../UI/GameUI/UI.h"
#include "../UI/GameUI/UITabEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayTimelineOperations;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
// Why: Profile builds do not emit Debug-only scene-finished telemetry, so
// automation exits need an explicit stdout breadcrumb near the quit request.
void PrintRuntimeExitReason( const char* reason )
{
    printf( "[runtime-exit] %s\n", reason );
    fflush( stdout );
}

float ResolvePresentationAlpha( const SkullbonezCore::Core::EngineConfig& config, bool capturePresentationPinned,
                                float simulationPresentationAlpha )
{
    if ( !config.runtimeRender.presentationInterpolation || capturePresentationPinned )
    {
        return 1.0f;
    }

    return std::clamp( simulationPresentationAlpha, 0.0f, 1.0f );
}

bool PublishInteractionSidecar( const std::filesystem::path& partial, const std::filesystem::path& destination )
{
    // Invariant: only complete sidecars receive their manifest-visible names.
    // The manifest itself is published later, after digesting these files.
    if ( MoveFileExA( partial.string().c_str(), destination.string().c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
    {
        return true;
    }

    std::error_code ignored;
    std::filesystem::remove( partial, ignored );
    return false;
}

DemoCameraPose CaptureDemoDirectorPose( const SkullbonezCore::Environment::CameraCollection& cameras )
{
    DemoCameraPose pose;
    pose.eye = cameras.GetCameraTranslation();
    pose.view = cameras.GetCameraView();
    pose.up = cameras.GetCameraUp();
    return pose;
}

int InferRecordedDemoCamera( const Vector3& eye )
{
    const Vector3 generatedEyes[] = { Vector3( 321.0f, 110.0f, 557.0f ), Vector3( 730.0f, 100.0f, 380.0f ),
                                      Vector3( 900.0f, 110.0f, 900.0f ) };
    int closestIndex = 0;
    float closestDistance = ( std::numeric_limits<float>::max )();

    for ( int index = 0; index < static_cast<int>( DEMO_CAMERA_CYCLE_SLOTS.size() ); ++index )
    {
        const Vector3 delta = eye - generatedEyes[index];
        const float distance = SkullbonezCore::Math::Vector::Dot( delta, delta );

        if ( distance < closestDistance )
        {
            closestDistance = distance;
            closestIndex = index;
        }
    }

    return closestIndex;
}

void RestoreRecordedSceneCameraBaseline( SceneController& sceneController, CameraControlState& camera,
                                         bool recordedSceneMode, int recordedDemoSelectedCamera,
                                         float recordedDemoCameraCycleSeconds )
{
    SceneWorld& world = sceneController.Scene();
    SkullbonezCore::Environment::CameraCollection& cameras = world.Cameras();
    sceneController.State().isSceneMode = recordedSceneMode;

    if ( recordedSceneMode )
    {
        return;
    }

    // A generated demo is serialized through the authored scene format, whose
    // loader cannot know that its single saved pose belonged to a three-slot
    // tracking rig. Rebuild that rig before normal camera-mode restoration.
    const DemoCameraPose savedPose = CaptureDemoDirectorPose( cameras );
    const int selectedCamera = recordedDemoSelectedCamera >= 0 ? recordedDemoSelectedCamera
                                                               : InferRecordedDemoCamera( savedPose.eye );
    cameras.Reset();
    SceneGeneratedSetup::SetUpCameras( world );
    cameras.SelectCamera( DEMO_CAMERA_CYCLE_SLOTS[static_cast<std::size_t>( selectedCamera )], false );
    cameras.CancelTween();
    cameras.SetPrimaryPose( savedPose.eye, savedPose.view, savedPose.up );
    camera.selectedCamera = selectedCamera;
    camera.cameraTime = recordedDemoCameraCycleSeconds;
}

} // namespace

void Run::ApplyDemoDirectorTickResult( const DemoDirectorTickResult& result )
{
    // Invariant: phase-entry style and reveal policy commit before the camera
    // pose for that phase becomes visible to render and capture consumers.
    if ( result.applyStyle )
    {
        AuthoredScene styleScene;
        const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadStyleFromFile( m_resultDiagnostics,
                                                                                               result.stylePath, m_assets,
                                                                                               styleScene );

        if ( loadResult.Ok() )
        {
            m_sceneController.ApplyLiveStyle( m_launchOptions, m_operatorUi->SceneNavigation().browser,
                                              ActiveSceneCinematicConfig( m_sceneController.State(), m_config ),
                                              m_renderDefaults.CinematicBaseline(), styleScene );
        }

        DemoDirectorPlayback::CompleteStyleApplication( m_camera.director, loadResult.Ok(), loadResult.ErrorMessage() );
    }

    if ( result.applyRevealRate )
    {
        ReplayFrameIntent intent;
        intent.applyPredictionRevealRate = true;
        intent.predictionRevealRate = result.requestedRevealRate;
        (void)m_replayRuntime.ApplyFrameIntent( intent );
    }

    if ( result.applyCameraPose )
    {
        m_sceneController.Scene().Cameras().SetPrimaryPose( result.cameraPose.eye, result.cameraPose.view,
                                                            result.cameraPose.up );
    }
}

namespace
{
// Lifetime: this fixed post-step operation receives only its replay-capture
// inputs. It cannot reach unrelated frame owners through the root view slices.
void CaptureReplayPostStep( RuntimeTools& runtimeTools, SkullbonezCore::Runtime::SceneController& sceneController,
                            const RuntimeOverlayDiagnostics& overlays, ReplayRuntime& replayRuntime,
                            SkullbonezCore::Core::Profiler* )
{
    const SceneSessionState& scene = sceneController.State();
    const OverlayDebugState debug = overlays.PresentationSnapshot();
    SkullbonezCore::Environment::CameraCollection& cameras = sceneController.Scene().Cameras();
    SkullbonezCore::Environment::WorldEnvironment& world = sceneController.Scene().Environment();
    PhysicsEngine& physics = sceneController.Scene().Physics();
    const SceneEntityStore& entities = sceneController.Scene().Entities();
    CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
    PROFILE_SCOPED( "Frame/Physics/Step/ReplayCapture" );
    ReplayWorldPresentationSample worldSample;
    worldSample.gravity = world.GetGravity();
    worldSample.fluidHeight = world.GetFluidSurfaceHeight();
    worldSample.fluidDensity = world.GetFluidDensity();
    worldSample.fixedStep = scene.isFixedStep; // Compatibility: Replay stores the request, not resolved pacing.
    worldSample.scenePhysicsEnabled = scene.isScenePhysics;
    worldSample.sceneTextEnabled = scene.isSceneText;
    worldSample.waterHidden = debug.isWaterHidden;
    worldSample.terrainHidden = debug.isTerrainHidden;

    ReplayCameraSample cameraSample;
    cameraSample.eye = cameras.GetCameraTranslation();
    cameraSample.view = cameras.GetCameraView();
    cameraSample.up = cameras.GetCameraUp();

    replayRuntime.CaptureFrame( scene.currentFrame, PHYSICS_FIXED_DT, worldSample, cameraSample, physics,
                                sceneController.Scene().Tornado(), entities, runtimeTools );
}

} // namespace

bool Run::PumpFrameMessages( int& messageExitCode )
{
    constexpr int kMaxMessagesPerFrame = 256;
    int messagesDrained = 0;
    NativeHostMessage message;

    // Hazard: a device or window can flood the thread queue faster than frame
    // work consumes it. The cap defers excess FIFO messages to the next frame.
    while ( messagesDrained < kMaxMessagesPerFrame && m_window.PeekNativeMessage( message ) )
    {
        ++messagesDrained;

        if ( message.quit )
        {
            if ( m_graphicsStress.IsEnabled() )
            {
                std::printf( "[graphics-stress] WM_QUIT received at frame=%d scene_frame=%d scene_loads=%d\n",
                             m_graphicsStress.FramesRun(), m_sceneController.State().currentFrame,
                             m_graphicsStress.SceneLoadsRequested() );
                std::fflush( stdout );
            }

            // Concept: WM_QUIT is the platform stop notification, not the
            // process result. An earlier Run-owned failure remains authoritative.
            m_applicationExit.RequestNormalExit();
            messageExitCode = message.exitCode;
            return true;
        }

        NativeHostMessageRoute route;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
        const DevelopmentTools::ImGuiEditorNativeMessageRoute
            developmentUiRoute = m_imguiEditor.HandleNativeMessage( message.window, message.id, message.wParam,
                                                                    message.lParam );
        route.engineConsumes = developmentUiRoute.decision.engineConsumes;
        route.capturedResult = developmentUiRoute.backendResult;
#endif

        if ( route.engineConsumes )
        {
            if ( message.id == WM_SETFOCUS )
            {
                SkullbonezCore::Hardware::Input::SetSystemCursorVisible(
                    SkullbonezCore::Hardware::Input::IsSystemCursorVisibleRequested() );
            }
            else if ( message.id == WM_KILLFOCUS )
            {
                SkullbonezCore::Hardware::Input::SetSystemCursorVisible( true );
            }
            else if ( message.id == WM_SETCURSOR && LOWORD( message.lParam ) == HTCLIENT )
            {
                const bool focused = GetForegroundWindow() == message.window;
                SkullbonezCore::Hardware::Input::SetSystemCursorVisible(
                    focused ? SkullbonezCore::Hardware::Input::IsSystemCursorVisibleRequested() : true );
                route.engineCursorHandled = true;
            }
        }

        m_window.DispatchNativeMessage( message, route );

        NativeHostEvent event;

        while ( m_window.ConsumeNativeEvent( event ) )
        {
            if ( event.type == NativeHostEventType::MouseWheel )
            {
                SkullbonezCore::Hardware::Input::AccumulateMouseWheelDelta( event.window, event.first );
                continue;
            }

            if ( event.type == NativeHostEventType::RawMouse )
            {
                SkullbonezCore::Hardware::Input::AccumulateRawMouseSample( event.window, event.first, event.second,
                                                                           event.absolute, event.virtualDesktop );
                continue;
            }

            if ( event.first <= 0 || event.second <= 0 )
            {
                continue;
            }

            const SkullbonezCore::Core::SbResult resize = Renderer().RenderFrame().Resize( event.first, event.second );

            if ( !resize.Ok() )
            {
                const char* owner = resize.ErrorOwner()[0] != '\0' ? resize.ErrorOwner() : "Runtime/Window";
                const char* reason = resize.ErrorMessage()[0] != '\0' ? resize.ErrorMessage() : "window resize failed";
                SkullbonezCore::Core::Log().WriteEventf( "window_resize_failed owner=\"%s\" message=\"%s\"", owner, reason );
                std::fprintf( stderr, "[window] Resize failed owner=%s reason=\"%s\"\n", owner, reason );
                std::fflush( stderr );
                SkullbonezCore::Core::Log().FlushAll();
                m_applicationExit.RequestOwnedFailure( resize );
                messageExitCode = 1;
                return true;
            }

            m_window.UpdateProjectionForCurrentClient();
        }
    }

    return false;
}

double Run::BeginFrameTurn()
{
    double secondsPerFrame = std::clamp( m_timers.BeginFrame(), 0.0, 0.05 );
    PROFILE_FRAME_BEGIN( m_profiler );
    PROFILE_COUNTER( m_profiler, "Counter/Presentation/FrameIntervalMs", secondsPerFrame * 1000.0 );

    // Lifetime: every facet is a startup-owned borrow for this synchronous
    // frame turn. The Run-owned renderer epoch fails before any phase can
    // dereference a missing or teardown-closed renderer.
    static_cast<void>( Renderer( "Execute" ) );

    return secondsPerFrame;
}

void Run::AdvanceInteractionRecordingBoundary()
{
    if ( m_interactionRecorder.IsArmed() )
    {
        CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
        const std::filesystem::path scenePath = m_interactionRecorder.ScenePath();
        const std::filesystem::path scenePartial = scenePath.string() + ".partial";
        const Core::SbResult sceneResult = SaveSceneLoadOnlySnapshot( m_resultDiagnostics, scenePartial.string().c_str(),
                                                                      m_sceneController.Scene().GetSaveState(),
                                                                      m_sceneController.State().GetSaveState(),
                                                                      m_overlayDiagnostics->PresentationSnapshot()
                                                                          .GetSaveState() );

        if ( !sceneResult.Ok() || !PublishInteractionSidecar( scenePartial, scenePath ) )
        {
            std::error_code ignored;
            std::filesystem::remove( scenePartial, ignored );
            const Core::SbResult failure = m_interactionRecorder
                                               .Abort( m_resultDiagnostics,
                                                       "failed to save the interaction recording scene snapshot" );
            m_applicationExit.RequestPhaseFailure( failure );
            return;
        }

        InteractionRecordingBaseline baseline;
        baseline.cameraMode = static_cast<int>( m_camera.mode );
        baseline.sceneMode = m_sceneController.State().isSceneMode;
        baseline.demoSelectedCamera = m_camera.selectedCamera;
        baseline.demoCameraCycleSeconds = m_camera.cameraTime;
        baseline.worldInteractionOwner = static_cast<int>( m_interaction.Owner() );
        baseline.uiVisible = m_operatorUi->IsVisible();
        baseline.uiMinimized = m_operatorUi->IsMinimized();
        baseline.activeUiTab = static_cast<int>( m_operatorUi->GetActiveTab() );
        baseline.editorModeEnabled = m_editorTools.Editor().editorModeEnabled;
        baseline.editorPlacementModeEnabled = m_editorTools.Editor().placementModeEnabled;
        baseline.editorPlaceStatic = m_editorTools.Editor().placeStaticObject;
        baseline.editorTerrainAlign = m_editorTools.Editor().autoTerrainAlign;
        baseline.editorObjectType = m_editorTools.Editor().objectType;
        const int selectedEditorModel = PeekSelectedEditorModelIndex( m_editorTools.Editor(),
                                                                      m_sceneController.Scene().BodyStore() );

        if ( selectedEditorModel >= 0 && selectedEditorModel < m_sceneController.Scene().SceneEntityCount() )
        {
            strncpy_s( baseline.editorSelectionName, sizeof( baseline.editorSelectionName ),
                       m_sceneController.Scene().Entities().At( selectedEditorModel ).displayName, _TRUNCATE );
        }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
        baseline.developmentUiSurface = static_cast<int>( m_imguiEditor.SelectedSurface() );
#endif
        const ReplayInputView replay = m_replayRuntime.BuildInputView();
        const bool replayBaselineRequired = replay.activeInteraction || replay.scrubPaused || replay.predictionEnabled ||
                                            replay.hasPathTarget || replay.hasCameraFocus;
        bool replaySidecarWritten = false;

        if ( replayBaselineRequired )
        {
            const std::filesystem::path replayPath = m_interactionRecorder.ReplayPath();
            const std::filesystem::path replayPartial = replayPath.string() + ".partial";
            replaySidecarWritten = m_replayRuntime.SaveInteractionRecordingBaseline( replayPartial.string().c_str() ) &&
                                   PublishInteractionSidecar( replayPartial, replayPath );

            if ( !replaySidecarWritten )
            {
                const Core::SbResult failure = m_interactionRecorder
                                                   .Abort( m_resultDiagnostics,
                                                           "failed to save the active replay interaction baseline" );
                m_applicationExit.RequestPhaseFailure( failure );
                return;
            }
        }

        baseline.replayActive = replayBaselineRequired;
        baseline.replayScrubPaused = replay.scrubPaused;
        baseline.replayLiveAdvanceHeld = replay.liveAdvanceHeld;
        baseline.replayPredictionEnabled = replay.predictionEnabled;
        baseline.replayTrack = static_cast<int>( replay.activeTrack );
        baseline.replayPresentationTrackPosition = replay.presentationTrackPosition;
        baseline.replaySolverTrackPosition = replay.solverTrackPosition;
        const ReplayCauseInspectionView causeInspection = m_replayRuntime.CauseInspectionView();
        baseline.replayCauseInspectionMode = static_cast<int>( causeInspection.Transport().mode );
        baseline.replayCauseSelectedRow = causeInspection.Selection().selectedRow;
        baseline.replayCauseActiveTab = static_cast<int>( causeInspection.Display().activeTab );
        baseline.replayCauseSelectedDetailContactRow = causeInspection.Selection().selectedDetailContactRow;
        baseline.replayCauseSolverDetailFirstRow = causeInspection.Display().solverDetailFirstRow;
        baseline.replayCauseRawRecordFirstRow = causeInspection.Display().rawRecordFirstRow;
        baseline.replayCauseIterationsFirstRow = causeInspection.Display().iterationsFirstRow;
        baseline.replayCauseSourceFrame = causeInspection.Transport().sourceFrame;
        baseline.replayCauseTargetFrame = causeInspection.Transport().targetFrame;
        baseline.replayCausePresentedFrame = causeInspection.Transport().presentedFrame;
        baseline.replayCauseDetailVisible = causeInspection.Display().detailVisible;
        baseline.replayCauseOwnsPause = causeInspection.Transport().ownsPause;
        baseline.replayCauseTransportPending = causeInspection.Transport().transportPending;
        baseline.replayCauseTransportInFlight = causeInspection.Transport().transportInFlight;
        baseline.replayCauseReturnIssued = causeInspection.Transport().returnIssued;
        baseline.replayCauseEasedProgress = causeInspection.Transport().easedProgress;
        baseline.replayCauseDrawerProgress = causeInspection.Display().drawerProgress;

        if ( replay.hasPathTarget && replay.pathTargetModelRow >= 0 &&
             replay.pathTargetModelRow < m_sceneController.Scene().SceneEntityCount() )
        {
            strncpy_s( baseline.replayPathTargetName, sizeof( baseline.replayPathTargetName ),
                       m_sceneController.Scene().Entities().At( replay.pathTargetModelRow ).displayName, _TRUNCATE );
        }

        const Core::SbResult begin = m_interactionRecorder.BeginAtBoundary( m_resultDiagnostics, m_window.ClientWidth(),
                                                                            m_window.ClientHeight(),
                                                                            m_sceneController.LifecyclePacket().generation,
                                                                            baseline, replaySidecarWritten );

        if ( !begin.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( begin );
        }

        return;
    }

    if ( m_interactionRecorder.IsRecording() )
    {
        const Core::SbResult advance = m_interactionRecorder
                                           .AdvanceBoundary( m_resultDiagnostics,
                                                             m_sceneController.LifecyclePacket().generation );

        if ( !advance.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( advance );
        }
        else if ( !m_interactionRecorder.IsRecording() )
        {
            m_operatorUi->SceneNavigation().RefreshInteractionRecordings();
        }
    }
}

void Run::CaptureInteractionRecordingTurn( double secondsPerFrame )
{
    if ( m_interactionRecorder.IsRecording() )
    {
        char semanticAnchor[64] = {};
        const DeviceInputFrame& input = m_inputRouter.DeviceFrame();

        if ( input.hasClientPosition )
        {
            m_operatorUi->CaptureInteractionAnchor( input.clientX, input.clientY, semanticAnchor, sizeof( semanticAnchor ) );
        }

        InteractionAutomationInputSample sample;
        const std::span<const uint64_t> keyWords = input.keys.Words();

        for ( std::size_t index = 0u; index < sample.keyWords.size() && index < keyWords.size(); ++index )
        {
            sample.keyWords[index] = keyWords[index];
        }

        sample.clientX = input.clientX;
        sample.clientY = input.clientY;
        sample.rawMouseX = input.rawMouseX;
        sample.rawMouseY = input.rawMouseY;
        sample.wheelDelta = input.wheelDelta;
        sample.hasClientPosition = input.hasClientPosition;
        sample.appFocused = input.appFocused;
        sample.leftDown = input.leftDown;
        sample.rightDown = input.rightDown;
        sample.middleDown = input.middleDown;
        m_interactionRecorder.CapturePendingTurn( secondsPerFrame, m_window.ClientWidth(), m_window.ClientHeight(), sample,
                                                  semanticAnchor );
    }
}

void Run::BeginFrameDiagnosticsPhase()
{
    // Frame boundary: publish prior-frame GPU counters before resetting the
    // diagnostics storage that records this turn.
    Renderer().BeginProfilerFrame();
    Renderer().RenderDiagnostics().ResetFrameDrawCalls();
}

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
SceneFrameProceedPolicy Run::RunAutomationAndInputPhase( bool& gameUiActive, RecordedCursorFrame& recordedCursor )
{
    recordedCursor = {};
    const uint64_t sceneGenerationBeforeInput = m_sceneController.LifecyclePacket().generation;
    const InteractionAutomationFrameResult result = RunInteractionAutomationBeforeInput();
    const ReplayInputView automationReplayInput = m_replayRuntime.BuildAutomationView().input;

    if ( result.restoreRecordedSceneCameraBaseline )
    {
        RestoreRecordedSceneCameraBaseline( m_sceneController, m_camera, result.recordedSceneMode,
                                            result.recordedDemoSelectedCamera, result.recordedDemoCameraCycleSeconds );
    }

    if ( result.applyDirectorCameraPose )
    {
        m_sceneController.Scene().Cameras().SetPrimaryPose( result.directorCameraPose.eye, result.directorCameraPose.view,
                                                            result.directorCameraPose.up );
    }

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
    const InteractionAutomationDevelopmentUiApplyResult
        developmentUiApply = ApplyInteractionAutomationDevelopmentUiCommands( m_interactionAutomation, result, m_window,
                                                                              m_imguiEditor );

    if ( developmentUiApply.selectSurface )
    {
        SelectDevelopmentUiSurface( developmentUiApply.surface );
    }

    if ( !developmentUiApply.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( developmentUiApply.status );
    }
#endif

    if ( result.applyCameraMode )
    {
        m_inputRouter.ApplyCameraMode( result.cameraMode, RuntimeInputActionSource::Runtime, m_editorTools, m_runtimeTools,
                                       m_interaction, m_attachedCamera, m_camera, m_sceneController, m_replayRuntime,
                                       m_inputRouter.RuntimeContext() );
    }

    if ( result.restoreRecordedSceneCameraBaseline && !result.recordedSceneMode )
    {
        // ApplyCameraMode exits the authored Scene fly workspace loaded from the
        // sidecar and deliberately resets its clock. The recorded generated-demo
        // phase is authoritative after that transition cleanup completes.
        m_camera.cameraTime = result.recordedDemoCameraCycleSeconds;
    }

    (void)m_replayRuntime.ApplyFrameIntent( result.replayIntent );

    if ( result.restoreRecordedReplayBaseline )
    {
        m_replayRuntime.RestoreInteractionRecordingBaseline( result.recordedReplayTrack,
                                                             result.recordedReplayPresentationTrackPosition,
                                                             result.recordedReplaySolverTrackPosition,
                                                             result.recordedReplayScrubPaused,
                                                             result.recordedReplayLiveAdvanceHeld );
    }

    if ( result.setWorldInteractionOwner )
    {
        const SceneSessionState& sceneState = m_sceneController.State();
        const int sceneEntityCount = m_sceneController.Scene().SceneEntityCount();
        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneState.isSceneMode, sceneEntityCount );
        const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( automationReplayInput.restoreCameraMode,
                                                                                sceneState.isSceneMode,
                                                                                cameraModeEnabledMask );

        m_inputRouter.SetWorldInteractionOwner( static_cast<WorldInteractionOwner>( result.worldInteractionOwner ),
                                                static_cast<InteractionExitReason>( result.worldInteractionReason ),
                                                m_editorTools, m_runtimeTools, m_interaction, m_attachedCamera, m_camera,
                                                m_sceneController, m_replayRuntime, normalizedRestoreMode );
    }

    if ( result.restoreRecordedReplayCauseBaseline )
    {
        const InteractionRecordingBaseline& recorded = result.recordedReplayCauseBaseline;
        ReplayInteractionRecordingCauseState cause;
        cause.mode = static_cast<ReplayCauseInspectionMode>( recorded.replayCauseInspectionMode );
        cause.activeTab = static_cast<ReplayCauseInspectorTab>( recorded.replayCauseActiveTab );
        cause.selectedRow = recorded.replayCauseSelectedRow;
        cause.selectedDetailContactRow = recorded.replayCauseSelectedDetailContactRow;
        cause.solverDetailFirstRow = recorded.replayCauseSolverDetailFirstRow;
        cause.rawRecordFirstRow = recorded.replayCauseRawRecordFirstRow;
        cause.iterationsFirstRow = recorded.replayCauseIterationsFirstRow;
        cause.sourceFrame = recorded.replayCauseSourceFrame;
        cause.targetFrame = recorded.replayCauseTargetFrame;
        cause.presentedFrame = recorded.replayCausePresentedFrame;
        cause.detailVisible = recorded.replayCauseDetailVisible;
        cause.ownsPause = recorded.replayCauseOwnsPause;
        cause.transportPending = recorded.replayCauseTransportPending;
        cause.transportInFlight = recorded.replayCauseTransportInFlight;
        cause.returnIssued = recorded.replayCauseReturnIssued;
        cause.easedProgress = recorded.replayCauseEasedProgress;
        cause.drawerProgress = recorded.replayCauseDrawerProgress;

        ReplayWorkspaceFrameInput causeInput;
        causeInput.normalizedCurrentMode = m_camera.mode;
        causeInput.normalizedRestoreMode = m_camera.mode;
        causeInput.now = m_timers.SimulationTotalSeconds();
        causeInput.screenWidth = m_window.ClientWidth();
        causeInput.screenHeight = m_window.ClientHeight();

        if ( !m_replayRuntime.RestoreInteractionRecordingCauseBaseline( cause, causeInput.now, causeInput, m_inputRouter,
                                                                        m_interaction, m_sceneController.Scene(),
                                                                        m_attachedCamera, m_camera,
                                                                        m_runtimeTools.MousePickup() ) )
        {
            const Core::SbResult failure = m_resultDiagnostics
                                               .Failure( "InteractionAutomation",
                                                         "recorded replay cause-inspection baseline could not be restored" );
            m_applicationExit.RequestPhaseFailure( failure );
        }

        // Cause selection uses normal replay ownership and may acquire a pause.
        // Reapply the recorded transport values after that owner-local rebuild.
        m_replayRuntime.RestoreInteractionRecordingBaseline( result.recordedReplayTrack,
                                                             result.recordedReplayPresentationTrackPosition,
                                                             result.recordedReplaySolverTrackPosition,
                                                             result.recordedReplayScrubPaused,
                                                             result.recordedReplayLiveAdvanceHeld );
    }

    if ( !result.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( result.status );
    }

    if ( result.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    const SceneFrameProceedPolicy proceedPolicy = RunInputPhase( &result, gameUiActive );
    const bool sceneReplaced = sceneGenerationBeforeInput != m_sceneController.LifecyclePacket().generation;
    const bool frameFailed = !result.status.Ok() || result.requestQuit || m_applicationExit.ExitRequested();
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    const RunEditorPlacementState& editor = m_editorTools.Editor();
    const PointerPresentationPolicy pointerPolicy = EvaluateRuntimePointerPresentation( m_inputRouter, editor, replayInput );
    const RuntimePointerCaptureOwner captureOwner = m_interaction.PointerCapture();
    const RecordedCursorPointerDisposition
        disposition = ClassifyRecordedCursorPointerDisposition( captureOwner == RuntimePointerCaptureOwner::CameraLook,
                                                                captureOwner == RuntimePointerCaptureOwner::ToolGesture,
                                                                m_inputRouter.RuntimeContext().CurrentMode() ==
                                                                    RuntimeInputMode::EditorViewportLook,
                                                                replayInput.inspectionActive &&
                                                                    pointerPolicy.mouseLookOwnsCursor,
                                                                editor.editorModeEnabled && editor.placementModeEnabled &&
                                                                    editor.placementPreviewVisible &&
                                                                    pointerPolicy.hideNativeCursor,
                                                                pointerPolicy.mouseLookOwnsCursor );
    const RecordedCursorPlaybackPhase
        phase = ClassifyRecordedCursorPlaybackPhase( m_interactionAutomation.enabled,
                                                     m_interactionAutomation.recordedManifest,
                                                     m_interactionAutomation.status.failed, m_interactionAutomation.finished,
                                                     m_interactionAutomation.recordedFramePublished,
                                                     result.recordedCursor.publishedRealTurn );
    recordedCursor = FilterRecordedCursorFrame( result.recordedCursor, phase, disposition, m_window.ClientWidth(),
                                                m_window.ClientHeight(), sceneReplaced, frameFailed );
    return proceedPolicy;
}
#endif

float Run::RunSimulationPhase( double secondsPerFrame, const SceneFrameProceedPolicy& proceedPolicy,
                               bool& capturePresentationPinned )
{
#if defined( SKULLBONEZ_SKARNESS )
    const uint64_t sceneGeneration = m_sceneController.LifecyclePacket().generation;

    if ( m_skarness.BeginPhysicsSceneGeneration( sceneGeneration ) )
    {
        Physics::PhysicsEngine& physics = m_sceneController.Scene().Physics();
        physics.SetPhysicsDiagnosticsPath( m_skarness.PhysicsTracePath() );
        physics.SetPhysicsDiagnosticsRunId( m_skarness.RunId() );
    }
#endif
    m_sceneController.Scene().BeginCollisionVisualFrame();

    // Invariant: capture pinning is fixed before physics and camera work. A
    // scheduled screenshot renders exact solver poses for this whole turn.
    capturePresentationPinned = m_capture.RequiresDeterministicPresentation( m_sceneController.State().isSceneMode,
                                                                             m_sceneController.State().currentFrame,
                                                                             m_timers.SceneElapsedSeconds() * 1000.0 ) ||
                                ( m_sceneController.State().isSceneMode && m_camera.autoCycleInterval > 0.0f ) ||
                                m_liveStyle.HasPendingCapture()
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
                                || InteractionAutomationWillCaptureAfterRender( m_interactionAutomation,
                                                                                m_sceneController.State().currentFrame )
#endif
        ;

    float interpolationAlpha = 1.0f;
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Physics );
        interpolationAlpha = TickPhysics( secondsPerFrame, capturePresentationPinned, proceedPolicy );
    }
    {
        // Invariant: prediction publication completes before overlay and render
        // construction. Render cannot decide whether the private engine advances.
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Replay );
        ApplyStartupPredictionRequest();
        m_replayRuntime.UpdatePrediction( m_sceneController.Scene().Physics(), m_sceneController.Scene().Tornado(),
                                          m_sceneController.Scene().Entities(), m_config,
                                          m_sceneController.Scene().Environment().GetPhysicsWorldForces(),
                                          m_sceneController.State().predictionAllBodiesSpaceSeed
                                              ? ReplayPredictionPathPresentation::AllBodiesSpace
                                              : ReplayPredictionPathPresentation::SelectedCausalTree,
                                          m_workerPool, m_sceneController.State().isScenePhysics,
                                          m_timers.SceneElapsedSeconds(), m_timers.SimulationTotalSeconds() );

        // Why: this frame-side admission has its own five-millisecond check;
        // the worker slice deliberately retains the separately ratified clock.
        const auto continuousForecastBudgetStart = std::chrono::steady_clock::now();
        (void)m_continuousForecast.AdvanceFrame( continuousForecastBudgetStart );
    }
    SceneWorld& scene = m_sceneController.Scene();
    scene.EndCollisionVisualFrame();

    SceneAutomationGateTracker& sceneGates = m_validationHarness->SceneGates();

    if ( sceneGates.RequiresBroadphaseXCellObservation() )
    {
        PhysicsBroadphaseActiveCell activeCells[PHYSICS_BROADPHASE_ACTIVE_CELL_CAPACITY];
        const int activeCellCount = PhysicsEngine::ReadBroadphaseActiveCells( scene.Physics(), activeCells );
        sceneGates.UpdateRequiredBroadphaseXCells(
            std::span<const PhysicsBroadphaseActiveCell>( activeCells, static_cast<std::size_t>( activeCellCount ) ) );
    }

    sceneGates.UpdateRequiredSleepingDynamicBodies( scene.BodyStore().HotFields().awake );
    sceneGates.UpdateRequiredContacts( SceneAutomationGatePhysicsView { scene.BodyStore(), scene.Colliders(),
                                                                        PhysicsEngine::ReadDebugContacts(
                                                                            scene.Physics() ) },
                                       m_config.bodySimulation.contactEpsilon );

    return interpolationAlpha;
}

float Run::PrepareRenderPhase( bool gameUiActive, bool capturePresentationPinned, float interpolationAlpha )
{
    // Concept: graphics stress is render/runtime churn, not UI command work.
    // This top-level phase coordinates its concrete planning, load, action, and
    // diagnostics operations without delegating the composition root.
    GraphicsStressController& graphicsStress = m_graphicsStress;

    if ( PrepareGraphicsStressChurn( graphicsStress, m_window, Renderer(), Renderer().RenderDiagnostics() ) )
    {
        const GraphicsStressSceneLoadPlan stressLoad = PlanGraphicsStressSceneLoad( graphicsStress, m_sceneController,
                                                                                    *m_operatorUi );

        if ( stressLoad.request.accepted )
        {
            PrepareSceneScopedOwnersForTransition();
            SceneLoadTransaction sceneLoad;
            sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                             ProjectScenePresentationValues( m_overlayDiagnostics->PresentationSnapshot() ),
                                             { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled() },
                                             Renderer().RendererName(), m_timers.SimulationTotalSeconds() );

            const bool loaded = LoadSceneRequest( sceneLoad, stressLoad.request ).Ok();

            if ( !gameUiActive )
            {
                sceneLoad.PreserveInactiveDevelopmentUi();
            }

            ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, m_sceneController.LifecyclePacket(),
                                               m_timers );
            ApplySceneLoadRuntimeReactions( sceneLoad );

            {
                const SceneLoadResult& presentation = BeginSceneLoadPresentation( sceneLoad, *m_validationHarness,
                                                                                  m_sceneController );
                const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
                ApplySceneLoadRenderPresentation( lifecycle, &Renderer().RenderDevice(), Renderer().VsyncEnabled() );
                ApplySceneLoadWindowUiPresentation( presentation, m_window, *m_operatorUi );
                ApplySceneLoadGraphicsStressPresentation( lifecycle, m_graphicsStress, m_graphicsStressSceneObserver,
                                                          m_launchOptions );
                sceneLoad.CompletePresentation();
            }

            if ( loaded )
            {
                graphicsStress.RecordSceneLoad();
                printf( "[graphics-stress] scene_load=%d frame=%d source=%s selected_index=%d action_index=%d\n",
                        graphicsStress.SceneLoadsRequested(), graphicsStress.FramesRun(), stressLoad.selectedSceneSource,
                        stressLoad.selectedSceneIndex, stressLoad.request.index );
            }
            else
            {
                printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                        graphicsStress.FramesRun(), stressLoad.selectedSceneSource, stressLoad.selectedSceneIndex );
            }

            fflush( stdout );
        }
        else if ( stressLoad.scheduled )
        {
            printf( "[graphics-stress] scene_load_skipped frame=%d source=%s selected_index=%d\n",
                    graphicsStress.FramesRun(), stressLoad.selectedSceneSource, stressLoad.selectedSceneIndex );

            fflush( stdout );
        }

        if ( gameUiActive )
        {
            m_operatorUi->SetVisible( true, m_timers.SimulationTotalSeconds() );
            m_operatorUi->SetMinimized( false, m_timers.SimulationTotalSeconds() );
        }

        m_sceneController.EnterInteractiveRun();
        ApplyGraphicsStressActions( graphicsStress );

        FinishGraphicsStressFrame( graphicsStress, m_diagnosticsRuntime, m_timers.Publish(), m_sceneController,
                                   m_replayRuntime.CollectMemoryStats(), Renderer().RenderDiagnostics() );
    }

    const float presentationAlpha = ResolvePresentationAlpha( m_config, capturePresentationPinned, interpolationAlpha );

    if ( Renderer().PipelineSyncEnabled() )
    {
        PROFILE_BEGIN( "Frame/PipelineSync" );
        SkullbonezCore::Core::SbResult finishResult = SkullbonezCore::Core::SbResult::Success();
        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
            finishResult = Renderer().RenderFrame().FinishAndReopen( Renderer().RenderDiagnostics() );
        }
        PROFILE_END( "Frame/PipelineSync" );

        if ( !finishResult.Ok() )
        {
            m_timers.FinishPresentedFrame();
            PROFILE_FRAME_END( m_profiler );
            m_applicationExit.RequestPhaseFailure( finishResult );
            return presentationAlpha;
        }
    }

    return presentationAlpha;
}


void Run::ApplyGraphicsStressActions( GraphicsStressController& graphicsStress )
{
    const int actionCount = graphicsStress.InDescriptorChurnQuietWindow() ? 0 : graphicsStress.ActionCount();

    for ( int index = 0; index < actionCount; ++index )
    {
        const int action = graphicsStress.NextAction();

        switch ( GraphicsStressOwnerForAction( action ) )
        {
        case GraphicsStressActionOwner::Invalid:
            break;
        case GraphicsStressActionOwner::Cinematic:
            ApplyGraphicsStressCinematicAction( action, graphicsStress, m_launchOptions, m_config, m_sceneController );
            break;
        case GraphicsStressActionOwner::SceneBrowser:
            ApplyGraphicsStressSceneBrowserAction( graphicsStress, m_assets, m_launchOptions, m_config, m_sceneController,
                                                   *m_operatorUi, m_renderDefaults.CinematicBaseline() );
            break;
        case GraphicsStressActionOwner::Renderer:
            ApplyGraphicsStressRendererAction( action, Renderer() );
            break;
        case GraphicsStressActionOwner::PresentationOverlay:
            ApplyGraphicsStressPresentationOverlayAction( action, graphicsStress, *m_overlayDiagnostics,
                                                          m_timers.Publish() );
            break;
        case GraphicsStressActionOwner::TimeScale:
            ApplyGraphicsStressTimeScaleAction( graphicsStress, m_sceneController, *m_operatorUi, m_simulation );
            break;
        case GraphicsStressActionOwner::World:
        {
            const GraphicsStressRuntimeActionResult stressResult = ApplyGraphicsStressWorldAction( graphicsStress,
                                                                                                   m_sceneController );
            if ( stressResult.worldOverrideChanged )
            {
                m_replayRuntime.SubmitEvent(
                    ReplayEventCommandOperations::BuildWorldOverride( stressResult.previousGravity,
                                                                      stressResult.previousFluidHeight,
                                                                      stressResult.previousFluidDensity,
                                                                      stressResult.gravity, stressResult.fluidHeight,
                                                                      stressResult.fluidDensity ) );
            }
            break;
        }
        case GraphicsStressActionOwner::GeneratedScene:
            ApplyGraphicsStressGeneratedSceneAction( graphicsStress, m_launchOptions );
            break;
        case GraphicsStressActionOwner::Tornado:
            ApplyGraphicsStressTornadoAction( action, graphicsStress, m_sceneController );
            break;
        case GraphicsStressActionOwner::OperatorUi:
            ApplyGraphicsStressOperatorUiAction( action, graphicsStress, *m_operatorUi );
            break;
        case GraphicsStressActionOwner::ScenePhysics:
            ApplyGraphicsStressScenePhysicsAction( action, m_sceneController, m_simulation );
            break;
        case GraphicsStressActionOwner::RuntimeTool:
            ApplyGraphicsStressRuntimeToolAction( m_runtimeTools );
            break;
        case GraphicsStressActionOwner::Camera:
            ApplyGraphicsStressCameraAction( graphicsStress, m_camera );
            break;
        case GraphicsStressActionOwner::RuntimeOverlay:
            ApplyGraphicsStressRuntimeOverlayAction( action, graphicsStress, *m_overlayDiagnostics );
            break;
        }
    }
}


RuntimeRenderFrameViews Run::PublishRenderModelsPhase()
{
    return PublishRenderModelFrame( m_sceneController.Scene(), m_workerPool, m_config );
}

void Run::RenderWorldPhase( const RuntimeRenderFrameViews& renderFrame, float presentationAlpha )
{
    PROFILE_BEGIN( "Frame/Render" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );
        DRAW_CALL_TRACE_SCOPE( Renderer().RenderDiagnostics(), "Frame/Render" );

        // Invariant: graph ownership begins before Render can take its text-only
        // path. World, UI, capture, and Present close the same graph exactly once.
        Renderer().BeginFrameGraph();
        Render( renderFrame, presentationAlpha );
    }
    PROFILE_END( "Frame/Render" );
}

void Run::RunPostDrawDiagnosticsPhase( bool gameUiActive )
{
    // Invariant: the F11 request was formed during input after its candidate was
    // applied. Draining after world/UI draw and before Present captures that
    // exact frame without lending renderer authority to LookLabController.
    CompleteLookLabPostRenderCaptures();

    PROFILE_BEGIN( "Frame/PostDraw/LiveStyleCapture" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );

        if ( m_liveStyle.HasPendingCapture() )
        {
            const SkullbonezCore::Core::SbResult
                captureResult = m_capture.SaveScreenshot( BackbufferCapture(), m_liveStyle.PendingScreenshotPath() );

            if ( captureResult.Ok() )
            {
                m_liveStyle.MarkCaptureSaved();
            }
            else
            {
                m_liveStyle.MarkCaptureFailed( captureResult.ErrorMessage() );
            }
        }
    }
    PROFILE_END( "Frame/PostDraw/LiveStyleCapture" );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    PROFILE_BEGIN( "Frame/PostDraw/InteractionAutomation" );
    const InteractionAutomationFrameResult automationAfterRender = RunInteractionAutomationAfterRender( gameUiActive );

    if ( !automationAfterRender.status.Ok() )
    {
        m_applicationExit.RequestPhaseFailure( automationAfterRender.status );
    }

    if ( automationAfterRender.requestQuit )
    {
        PostQuitMessage( 0 );
    }

    PROFILE_END( "Frame/PostDraw/InteractionAutomation" );
#else
    (void)gameUiActive;
#endif
}

void Run::CompleteLookLabPostRenderCaptures()
{
    CaptureController& capture = m_capture;

    if ( capture.PendingPostRenderCount() == 0 )
    {
        return;
    }

    PROFILE_BEGIN( "Frame/PostDraw/LookLabCapture" );
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );
        PostRenderCaptureBatchResult batch = capture.DrainPostRenderRequests( BackbufferCapture() );

        for ( std::size_t index = 0; index < batch.count; ++index )
        {
            const PostRenderCaptureResult& captured = batch.results[index];

#if defined( SKULLBONEZ_SKARNESS )
            if ( captured.request.owner == PostRenderCaptureOwner::ExternalAutomation )
            {
                m_skarness.CompleteCapture( captured.request.token, captured.status.Ok(),
                                            captured.status.Ok() ? nullptr : captured.status.ErrorMessage() );
                continue;
            }
#endif

            if ( captured.request.owner != PostRenderCaptureOwner::LookLab )
            {
                continue;
            }

            const Core::SbResult completion = m_lookLab.CompleteSaveCapture( m_resultDiagnostics, captured.request.token,
                                                                             captured.status );

            PublishLookLabStatusView();

            if ( !completion.Ok() )
            {
                std::fprintf( stderr, "%s: %s\n", completion.ErrorOwner(), completion.ErrorMessage() );
            }
        }
    }
    PROFILE_END( "Frame/PostDraw/LookLabCapture" );
}

void Run::FinishFrameWorkPhase( const SceneFrameProceedPolicy& proceedPolicy )
{
    PROFILE_BEGIN( "Frame/PostDraw/AutoCycle" );
    TickAutoCycle( proceedPolicy );
    PROFILE_END( "Frame/PostDraw/AutoCycle" );
    m_timers.FinishFrameWork();
}

void Run::PresentFramePhase()
{
    PROFILE_BEGIN( "Frame/VsyncWait" );
    SkullbonezCore::Core::SbResult presentResult = SkullbonezCore::Core::SbResult::Success();
    {
        CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Render );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
        if ( RecordedInteractionRequiresFreeRunningPresent( m_interactionAutomation.enabled,
                                                            m_interactionAutomation.recordedManifest ) )
        {
            // Invariant: every recorded input turn is still rendered and traced once;
            // only display-refresh pacing is removed from command-line playback.
            Renderer().RenderDevice().SetVsyncEnabled( false );
        }
#endif

        // Invariant: the production graph has one declaration-only Present edge;
        // finalize it before the swap-chain owner submits this frame.
        Renderer().FinalizeFrameGraph();
        presentResult = Renderer().RenderFrame().Present( Renderer().RenderDiagnostics() );
    }
    PROFILE_END( "Frame/VsyncWait" );

    if ( !presentResult.Ok() )
    {
        m_timers.FinishPresentedFrame();
        PROFILE_FRAME_END( m_profiler );
        m_applicationExit.RequestPhaseFailure( presentResult );
        return;
    }

    // Invariant: Tracy counts submitted game frames, not attempted render turns,
    // capture-only continues, or failed Presents.
    SKORE_TRACY_MARK_SUBMITTED_FRAME();
    m_timers.FinishPresentedFrame();
    PROFILE_FRAME_END( m_profiler );
}

bool Run::CompleteFramePhase( const SceneFrameProceedPolicy& proceedPolicy )
{
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const RuntimeProfilerFrameTimes profilerTimes = m_diagnosticsRuntime.SampleProfilerFrameTimes();
        m_timers.RecordProfilerSample( profilerTimes.physicsTimeSeconds, profilerTimes.renderTimeSeconds,
                                       profilerTimes.gpuFrameWorkMs );
    }
#endif
    const bool perfLogSucceeded = m_diagnosticsRuntime.TickPerfLog( m_sceneController.PerfPass() + 1,
                                                                    m_sceneController.State().currentFrame + 1,
                                                                    m_timers.Publish().physicsSeconds,
                                                                    m_timers.Publish().renderSeconds );

    if ( !perfLogSucceeded )
    {
        (void)ApplyPerfLogArtifactStatus( m_resultDiagnostics, m_applicationExit, false );
        PostQuitMessage( 1 );
        return false;
    }

    return TickSceneAdvance( proceedPolicy );
}

SkullbonezCore::Core::SbResult Run::Execute()
{
    if ( m_skipExecute )
    {
        return ResolveExecuteExit( 0 );
    }

    if ( m_applicationExit.ExitRequested() )
    {
        return ResolveExecuteExit( 0 );
    }

    int messageExitCode = 0;

    for ( ;; )
    {
        if ( PumpFrameMessages( messageExitCode ) )
        {
            break;
        }

        CoreAllocation::RuntimeAllocationScope frameAllocationScope {
            CoreAllocation::RuntimeAllocationPhase::SteadyGameplay };

        double secondsPerFrame = BeginFrameTurn();
        AdvanceInteractionRecordingBoundary();

        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        BeginFrameDiagnosticsPhase();
        PROFILE_BEGIN( "Frame/Input" );
        bool gameUiActive = true;
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
        RecordedCursorFrame recordedCursor;
        const SceneFrameProceedPolicy proceedPolicy = RunAutomationAndInputPhase( gameUiActive, recordedCursor );

#else
        const SceneFrameProceedPolicy proceedPolicy = RunInputPhase( nullptr, gameUiActive );
#endif
        CaptureInteractionRecordingTurn( secondsPerFrame );
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

        if ( m_interactionAutomation.recordedManifest && m_interactionAutomation.recordedFramePublished )
        {
            secondsPerFrame = m_interactionAutomation.recordedDeltaSeconds;
        }
#endif
        PROFILE_END( "Frame/Input" );

        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        bool capturePresentationPinned = false;
        const float interpolationAlpha = RunSimulationPhase( secondsPerFrame, proceedPolicy, capturePresentationPinned );
        const float presentationAlpha = PrepareRenderPhase( gameUiActive, capturePresentationPinned, interpolationAlpha );

        // Invariant: every frame phase below has a status-free return. Failure
        // is observable only through the ApplicationExitState latch.
        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        RuntimeRenderFrameViews renderFrame = PublishRenderModelsPhase();
        const RuntimeRenderFramePolicy debugFramePolicy = ProjectRenderFramePolicy(
            m_overlayDiagnostics->BuildFramePolicy( m_timers.SceneElapsedSeconds(), m_timers.SimulationTotalSeconds() ) );
        Renderer().UpdateDebugVisualizers( static_cast<float>( secondsPerFrame ), renderFrame.debug, debugFramePolicy );

        // Fixed boundary: diagnostics advance once from completed frame-model
        // facts before Render or either optional UI surface can branch.
        m_timers.SampleFrame( { secondsPerFrame, renderFrame.diagnostics.sceneKineticEnergy } );
        const RuntimeFrameMetricsSnapshot frameMetrics = m_timers.Publish();
        RenderWorldPhase( renderFrame, presentationAlpha );
        const OperatorUiProcessCommands operatorUiCommands = RenderOperatorUiPhase( renderFrame, presentationAlpha,
                                                                                    capturePresentationPinned,
                                                                                    secondsPerFrame, gameUiActive,
                                                                                    frameMetrics );

        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        // App applies process effects only after every presenter has released
        // the phase owner's detached values and returned its typed commands.
        ApplyOperatorUiProcessCommands( operatorUiCommands );

#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
        {
            UI::UIDrawList recordedCursorDrawList;
            ComposeRecordedCursorDrawList( recordedCursorDrawList, recordedCursor, m_window.ClientWidth(),
                                           m_window.ClientHeight() );
            const UI::UIDrawList::Stats recordedCursorDrawStats = recordedCursorDrawList.GetStats();
            bool recordedCursorSubmitted = false;

            // Invariant: RenderOperatorUiPhase has already submitted GameUI,
            // overlays, UI finalization, and ImGui. This unconditional App seam
            // stays ahead of diagnostics, every screenshot path, and Present.
            if ( !recordedCursorDrawList.Empty() )
            {
                Renderer().SubmitUiDrawList( recordedCursorDrawList, { m_window.ClientWidth(), m_window.ClientHeight() } );
                recordedCursorSubmitted = true;
            }

            // Automation owns the trace stream, while App supplies only the
            // detached result it just composed and submitted. No native pointer
            // or retained renderer state crosses this evidence boundary.
            m_interactionAutomation
                .recordedCursorPresentation = ObserveRecordedCursorPresentation( recordedCursor,
                                                                                 recordedCursorDrawStats.commandCount,
                                                                                 RECORDED_CURSOR_DRAW_COMMAND_HIGH_WATER,
                                                                                 recordedCursorSubmitted );
        }
#endif

        RunPostDrawDiagnosticsPhase( gameUiActive );

        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        {
            CoreAllocation::RuntimeAllocationScope allocationScope( CoreAllocation::RuntimeAllocationPhase::Capture );

            if ( TickScreenshots( proceedPolicy ) )
            {
                continue;
            }
        }
        FinishFrameWorkPhase( proceedPolicy );
        PresentFramePhase();
#if defined( SKULLBONEZ_SKARNESS )
        PublishSkarnessFrameState();

        if ( m_skarness.TakeStopRequested() )
        {
            PostQuitMessage( 0 );
        }
#endif

        if ( m_applicationExit.ExitRequested() )
        {
            return ResolveExecuteExit( 0 );
        }

        if ( CompleteFramePhase( proceedPolicy ) )
        {
            continue;
        }
    }

    return ResolveExecuteExit( messageExitCode );
}


#if defined( SKULLBONEZ_SKARNESS )
void Run::PublishSkarnessFrameState()
{
    if ( !m_skarness.Enabled() )
    {
        return;
    }

    const ReplaySkarnessState replay = m_replayRuntime.BuildSkarnessState();
    const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
    SkarnessFrameState state;
    state.sceneGeneration = lifecycle.generation;
    state.sceneFrame = m_sceneController.State().currentFrame;
    const std::string* scenePath = m_sceneController.CurrentPath();
    strncpy_s( state.scenePath, sizeof( state.scenePath ), scenePath ? scenePath->c_str() : "", _TRUNCATE );
    state.sceneObjectCount = m_sceneController.Scene().SceneEntityCount();
    state.physicsBodyCount = Physics::PhysicsEngine::ReadBodies( m_sceneController.Scene().Physics() ).Count();
    state.sceneLifecycleEvent = static_cast<int>( lifecycle.event );
    state.sceneReady = SceneLifecycleReached( lifecycle.event, SceneRuntimeLifecycleEvent::AfterSceneActivated );
    state.sceneMode = m_sceneController.State().isSceneMode;
    state.simulationSeconds = m_timers.SimulationTotalSeconds();
    state.paused = m_skarness.Paused();
    state.replayCaptureEnabled = replay.input.captureEnabled;
    state.replayScrubPaused = replay.input.scrubPaused;
    state.replayPlaybackPaused = replay.input.liveAdvanceHeld;
    state.predictionEnabled = replay.input.predictionEnabled;
    state.predictionBuilding = replay.predictionBuilding;
    state.predictionComplete = replay.predictionComplete;
    state.predictionDirty = replay.predictionDirty;
    state.predictionRestartPending = replay.predictionRestartPending;
    state.predictionGenerationPermitted = replay.predictionGenerationPermitted;
    state.predictionHighDetail = replay.predictionHighDetail;
    state.velocityEditEnabled = replay.input.velocityEditEnabled;
    state.ragdollVisualsEnabled = replay.ragdollVisualsEnabled;
    state.pastPathVisible = replay.pastPathVisible;
    state.hasPathTarget = replay.input.hasPathTarget;
    state.pathTargetId = replay.input.pathTargetId;
    state.pathTargetModelRow = replay.input.pathTargetModelRow;
    state.predictionHorizonSeconds = replay.predictionHorizonSeconds;
    state.predictionRevealProgress = replay.input.predictionRevealProgress;
    state.predictionGeneration = replay.predictionGeneration;
    state.predictionSourceTargetId = replay.predictionSourceTargetId;
    state.predictionSourceFrame = replay.predictionSourceFrame;
    state.predictionSourceSolverHash = replay.predictionSourceSolverHash;
    state.committedPredictionFrames = replay.committedPredictionFrames;
    state.incompleteContactFrameCount = replay.incompleteContactFrameCount;
    state.publishedPredictionTargetId = replay.publishedPredictionTargetId;
    state.publishedPredictionFrames = replay.publishedPredictionFrames;
    state.trajectoryRecordCount = replay.trajectoryRecordCount;
    state.selectedFutureRootPointCount = replay.selectedFutureRootPointCount;
    state.contactChildIncomingCount = replay.contactChildIncomingCount;
    state.contactChildOutgoingCount = replay.contactChildOutgoingCount;
    state.childOutgoingPreEntryPointCount = replay.childOutgoingPreEntryPointCount;
    state.retainedEntryMarkerCount = replay.retainedEntryMarkerCount;
    state.retainedEndMarkerCount = replay.retainedEndMarkerCount;
    state.drawnCollisionWireframeCount = replay.drawnCollisionWireframeCount;
    state.drawnEndingWireframeCount = replay.drawnEndingWireframeCount;
    state.collisionWireframePathMismatchCount = replay.collisionWireframePathMismatchCount;
    state.endingWireframePathMismatchCount = replay.endingWireframePathMismatchCount;
    state.futureNodeCount = replay.futureNodeCount;
    state.retainedLineFloatCount = replay.retainedLineFloatCount;
    state.retainedRibbonVertexFloatCount = replay.retainedRibbonVertexFloatCount;
    state.retainedPathGeometrySaturated = replay.retainedPathGeometrySaturated;
    state.visualPacketHasGeometry = replay.visualPacketHasGeometry;
    state.trajectorySubmitted = replay.trajectorySubmission.hasSubmission;
    state.submittedSegmentCount = replay.trajectorySubmission.segmentCount;
    state.submittedVertexCount = replay.trajectorySubmission.vertexCount;
    state.submittedFutureTreeReady = replay.trajectorySubmission.futureTreeReadyLastFrame;

    CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );
    m_skarness.PublishFrameState( state );
}
#endif

float Run::TickPhysics( double secondsPerFrame, bool capturePresentationPinned,
                        const SceneFrameProceedPolicy& proceedPolicy )
{
    // Why: simulation pacing is a reactive frame concern. Sampling the ledger
    // here keeps SimulationSystem out of every cold scene-load call surface.
    const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
    m_simulation.ObserveSceneLifecycle( lifecycle.generation,
                                        SceneLifecycleReached( lifecycle.event,
                                                               SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();

    if ( replayInput.scrubPaused )
    {
        PROFILE_SCOPED( "Frame/Replay/ScrubCamera" );
        UpdateLogic( 0.0f, static_cast<float>( secondsPerFrame ), 1.0f );
        return 1.0f;
    }

    const bool replayLiveAdvanceHeld = replayInput.liveAdvanceHeld;
    const RuntimeInputSnapshot& inputSnapshot = m_inputRouter.RuntimeSnapshot();
    const bool stepRequested = proceedPolicy.stepRequested;
    const bool replayCapture = replayInput.captureEnabled;
    const OverlayDebugState overlayPresentation = m_overlayDiagnostics->PresentationSnapshot();

    // Why: the saturated Replay count remains live every step, but payload rows
    // are observational work needed only by capture or pipeline presentation.
    m_sceneController.Scene().Physics().SetPipelineTraceFullRecordConsumerActive(
        replayCapture || ( overlayPresentation.physicsDebugFlags & PHYSICS_DEBUG_PIPELINE ) != 0u );
#ifdef _DEBUG
    const bool physicsCapture = m_diagnosticsRuntime.PerfLog().physicsRegressionLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PerfLog().physicsCollisionTimeLogOverride[0] != '\0' ||
                                m_diagnosticsRuntime.PhysicsDiagnostics().isEnabled;
#else
    constexpr bool physicsCapture = false;
#endif
    RuntimeInteractionFrameInput interactionFrameInput;
    interactionFrameInput.scenePhysicsEnabled = m_sceneController.State().isScenePhysics;
    interactionFrameInput.stepHeld = stepRequested;
    interactionFrameInput.replayScrubbedHistoricalSample = false;
    interactionFrameInput.replayLiveHeldAtCurrentFrame = replayLiveAdvanceHeld;
    interactionFrameInput.crossScenePauseLocked = proceedPolicy.crossScenePauseLocked;
    interactionFrameInput.rightMouseLookHeld = inputSnapshot.pointer.rightDown;
    interactionFrameInput.editorViewportLookActive = m_editorTools.Editor().viewportLookActive;
    interactionFrameInput.replayInspectionLookActive = inputSnapshot.frameInput.replayInspectionLookActive;
    interactionFrameInput.forcePhysicsRunning = physicsCapture;
    interactionFrameInput.sceneTimeScale = m_sceneController.State().timeScale;
    const RuntimeInteractionFramePolicy policy = m_interaction.BuildFramePolicy( interactionFrameInput );
    const bool manipulatorPhysics = policy.manipulatorActive;
    const auto physicsWorldForces = m_sceneController.Scene().Environment().GetPhysicsWorldForces();
    constexpr bool canStepPhysics = true;
    const SceneSessionState& sceneState = m_sceneController.State();

    // Invariant: a scene-session lockstep request becomes effective only for
    // finite unattended captures. Live, unlimited, and operator-controlled scenes
    // remain wall-clock paced; an explicit startup request still selects render-frame lockstep.
    const SimulationPacingPolicy pacingPolicy = ResolveSimulationPacingPolicy( m_launchOptions.fixedStep,
                                                                               sceneState.isFixedStep,
                                                                               sceneState.targetFrameCount,
                                                                               sceneState.isInteractiveRun );
    const SimulationTickResult tick = m_simulation.Tick(
        SimulationTickInput { secondsPerFrame, policy.physicsTimeScale, m_sceneController.State().isSceneMode,
                              m_sceneController.State().isScenePhysics, pacingPolicy, policy.physicsAdvance, stepRequested,
                              canStepPhysics } );

    const float presentationAlpha = ResolvePresentationAlpha( m_config, capturePresentationPinned, tick.presentationAlpha );

    if ( tick.committedPhysicsTicks > 0 && canStepPhysics )
    {
        PROFILE_BEGIN( "Frame/Physics" );

        // Why: SimulationSystem decides the Physics tick count but never executes
        // those steps. Runtime advances the store-owned physics state directly,
        // then applies the remaining model-owned presentation sync as explicit edge work.
        for ( int tickIndex = 0; tickIndex < tick.committedPhysicsTicks; ++tickIndex )
        {
            PROFILE_SCOPED( "Frame/Physics/Step" );
            {
                PROFILE_SCOPED( "Frame/Physics/Step/PresentationCaptureBegin" );
                m_sceneController.Scene().BeginPhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics )
            {
                m_runtimeTools.ApplyMousePickupPhysicsStep( m_sceneController.Scene(), m_inputRouter, m_interaction );
            }

            SkullbonezCore::Rendering::RenderInstanceStore& contactPresentation = m_sceneController.Scene()
                                                                                      .MutableRenderInstances();

            contactPresentation.TickContactFeedback( m_sceneController.Scene().SceneEntityCount(), PHYSICS_FIXED_DT );
            const ScenePhysicsPostStepOutput postStep = m_sceneController.Scene().StepPhysics( PHYSICS_FIXED_DT,
                                                                                               physicsWorldForces,
                                                                                               m_workerPool );

            // The physics owner publishes a bounded span; the presentation owner
            // consumes it before the next step can replace those dense-row facts.
            for ( int modelIndex : postStep.fixedContactModelIndices )
            {
                contactPresentation.NotifyFixedContact( modelIndex, 0.5f );
            }

            {
                PROFILE_SCOPED( "Frame/Physics/Step/PresentationCaptureComplete" );
                m_sceneController.Scene().CompletePhysicsStepPresentationCapture();
            }

            if ( manipulatorPhysics || replayCapture )
            {
                AfterPhysicsStep();
            }
        }

        PROFILE_END( "Frame/Physics" );
    }

    m_runtimeTools.TickRayCastTestLines( static_cast<float>( secondsPerFrame ) );
    m_runtimeTools.Laser().Update( static_cast<float>( secondsPerFrame ) );

    if ( tick.shouldUpdateLogic )
    {
        UpdateLogic( tick.simulationDt, tick.cameraDt, presentationAlpha );
    }
    else
    {
        // Why: Scene-mode, no-physics harnesses intentionally skip simulation
        // UpdateLogic, but Director is presentation state. It still needs phase
        // style/camera entry work so authored show decks behave in static scenes.
        const ReplayInputView directorReplayInput = m_replayRuntime.BuildInputView();
        DemoDirectorPredictionView directorPrediction;
        directorPrediction.revealAvailable = directorReplayInput.predictionRevealAvailable;
        directorPrediction.revealProgress = directorReplayInput.predictionRevealProgress;
        const DemoCameraPose currentPose = CaptureDemoDirectorPose( m_sceneController.Scene().Cameras() );
        const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick( m_camera.director,
                                                                                  m_camera.mode == RunCameraMode::Director,
                                                                                  directorPrediction, currentPose,
                                                                                  static_cast<float>( secondsPerFrame ) );
        ApplyDemoDirectorTickResult( directorResult );
    }

    return tick.presentationAlpha;
}


void Run::AfterPhysicsStep()
{
    m_runtimeTools.RestoreMousePickupAngularVelocity( m_sceneController.Scene(), m_inputRouter, m_interaction );
    const bool replayCaptured = m_replayRuntime.BuildInputView().captureEnabled;

    if ( replayCaptured )
    {
        CaptureReplayPostStep( m_runtimeTools, m_sceneController, *m_overlayDiagnostics, m_replayRuntime, m_profiler );
    }

#ifdef _DEBUG

    if ( replayCaptured )
    {
        RuntimeOverlayPresentationEdit presentationEdit = m_overlayDiagnostics->EditPresentation();
        SceneWorld& sceneWorld = m_sceneController.Scene();
        SceneSessionState& sceneState = m_sceneController.State();
        auto& sceneOverrides = m_operatorUi->SceneNavigation().overrides;
        const bool sceneMode = sceneState.isSceneMode;
        const int sceneEntityCount = sceneWorld.SceneEntityCount();
        const int sceneObjectCapacity = SkullbonezCore::Core::ActiveSceneObjectCapacity( m_config );
        GeneratedObjectTypeOverride& generatedObjectTypeOverride = m_launchOptions.generatedObjectTypeOverride;
        const uint32_t generatedObjectTypeOverrideBits = static_cast<uint32_t>( generatedObjectTypeOverride );

        const uint32_t cameraModeEnabledMask = RuntimeCameraModeEnabledMask( sceneMode, sceneEntityCount );

        const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
        const RunCameraMode normalizedRestoreMode = NormalizeRuntimeCameraMode( replayInput.restoreCameraMode, sceneMode,
                                                                                cameraModeEnabledMask );

        const ReplaySceneTimelineResetInput timelineReset = DescribeReplaySceneTimeline( m_sceneController, sceneOverrides,
                                                                                         sceneState, sceneObjectCapacity,
                                                                                         generatedObjectTypeOverrideBits );

        // Why: ReplayRuntime owns probe sequencing and bounded failure state;
        // the application exit latch only preserves that first owned failure
        // while WM_QUIT unwinds the frame loop.
        ReplayProbeRestoreCameraState probeRestoreCamera { m_camera, normalizedRestoreMode,
                                                           m_attachedCamera.State().activeFollow };
        const ReplayProbeTickResult probeResult = m_replayRuntime.TickProbes( m_sceneController, presentationEdit.State(),
                                                                              m_editorTools, m_runtimeTools, m_config,
                                                                              m_assets, timelineReset, m_diagnosticsRuntime,
                                                                              m_inputRouter, m_interaction,
                                                                              probeRestoreCamera );

        if ( !probeResult.status.Ok() )
        {
            m_applicationExit.RequestPhaseFailure( probeResult.status );
            PostQuitMessage( 0 );
            return;
        }

        if ( probeResult.resetCurrentScene )
        {
            m_sceneController.SubmitResetCurrentScene();
        }

        if ( probeResult.enterInteractive )
        {
            m_sceneController.EnterInteractiveRun();
            m_capture.DisableAutomationExit();
        }
    }
#endif
}


#ifdef _DEBUG
void Run::LogSceneFinished( const char* reason )
{
    SceneSessionState& scene = m_sceneController.State();
    const std::string* currentPath = m_sceneController.CurrentPath();
    const char* scenePath = currentPath && !currentPath->empty() ? currentPath->c_str() : "generated";

    if ( m_diagnosticsRuntime.LogSceneFinished( ProjectSceneDiagnosticFacts( scene ), scenePath,
                                                &Renderer().RenderDiagnostics(), reason ) )
    {
        scene.isFinishLogged = true;
    }
}
#endif


bool Run::TickScreenshots( const SceneFrameProceedPolicy& proceedPolicy )
{
    PROFILE_BEGIN( "Frame/PostDraw/Screenshots" );

    if ( !proceedPolicy.proceedAllowed )
    {
        PROFILE_END( "Frame/PostDraw/Screenshots" );
        return false;
    }

    const std::string* scenePath = m_sceneController.CurrentPath();
    const ScreenshotFrameInput captureFrame { m_sceneController.State().isSceneMode,
                                              m_sceneController.State().isInteractiveRun,
                                              m_sceneController.State().currentFrame,
                                              m_timers.SceneElapsedSeconds() * 1000.0,
                                              scenePath ? scenePath->c_str() : nullptr };
    const RuntimeCaptureResult result = m_capture.TickScreenshots( captureFrame, BackbufferCapture() );

    if ( result.restartFrame )
    {
        // Capture automation can synchronously replace scene-owned render
        // resources below. Close and clear graph borrows before that mutation;
        // this restart path deliberately records no Present declaration.
        Renderer().FinalizeCaptureOnlyFrameGraph();
    }

    PROFILE_END( "Frame/PostDraw/Screenshots" );

    if ( !result.captureResult.Ok() )
    {
        // Recoverable error: capture readback/file IO failed after rendering, so terminate
        // automation with diagnostics instead of marking the scene complete.
        fprintf( stderr, "%s: %s\n", result.captureResult.ErrorOwner(), result.captureResult.ErrorMessage() );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because screenshot capture failed." );
        m_applicationExit.RequestPhaseFailure( result.captureResult );
        PostQuitMessage( 1 );
        return false;
    }

    if ( result.restartFrame )
    {
        PROFILE_FRAME_END( m_profiler );
    }

#ifdef _DEBUG

    if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
    {
        LogSceneFinished( "screenshot_and_exit" );
    }
    else if ( result.completion == RuntimeCaptureCompletion::Screenshot )
    {
        LogSceneFinished( "screenshot" );
    }
#endif

    switch ( result.automation )
    {
    case RuntimeCaptureAutomation::Quit:

        if ( result.completion == RuntimeCaptureCompletion::ScreenshotAndExit )
        {
            PrintRuntimeExitReason( "Exiting because screenshot-and-exit capture completed." );
        }
        else if ( result.completion == RuntimeCaptureCompletion::AutoCycle )
        {
            PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture completed." );
        }

        PostQuitMessage( 0 );
        break;
    case RuntimeCaptureAutomation::AdvanceSceneOrQuit:
    {
        const SceneLoadRequest request = m_sceneController.AdvanceScene( m_diagnosticsRuntime.PerfTestActive(),
                                                                         m_sceneController.State().isInteractiveRun );

        bool advanced = false;

        if ( request.HasLoad() )
        {
            PrepareSceneScopedOwnersForTransition();
            SceneLoadTransaction sceneLoad;
            sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                             ProjectScenePresentationValues( m_overlayDiagnostics->PresentationSnapshot() ),
                                             { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled() },
                                             Renderer().RendererName(), m_timers.SimulationTotalSeconds() );

            advanced = LoadSceneRequest( sceneLoad, request ).Ok();

            ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, m_sceneController.LifecyclePacket(),
                                               m_timers );
            ApplySceneLoadRuntimeReactions( sceneLoad );

            {
                const SceneLoadResult& presentation = BeginSceneLoadPresentation( sceneLoad, *m_validationHarness,
                                                                                  m_sceneController );
                const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
                ApplySceneLoadRenderPresentation( lifecycle, &Renderer().RenderDevice(), Renderer().VsyncEnabled() );
                ApplySceneLoadWindowUiPresentation( presentation, m_window, *m_operatorUi );
                ApplySceneLoadGraphicsStressPresentation( lifecycle, m_graphicsStress, m_graphicsStressSceneObserver,
                                                          m_launchOptions );
                sceneLoad.CompletePresentation();
            }
        }

        if ( !advanced )
        {
            if ( result.completion == RuntimeCaptureCompletion::Screenshot )
            {
                PrintRuntimeExitReason( "Exiting because scene screenshot capture completed and no next scene is queued." );
            }

            PostQuitMessage( 0 );
        }

        break;
    }
    case RuntimeCaptureAutomation::HoldInteractive:
        m_sceneController.MarkInteractiveRunComplete();
        m_capture.DisableAutomationExit();
        m_camera.StopAutoCycle();
        break;
    case RuntimeCaptureAutomation::None:
        break;
    }

    return result.restartFrame;
}


void Run::TickAutoCycle( const SceneFrameProceedPolicy& proceedPolicy )
{
    if ( !proceedPolicy.proceedAllowed )
    {
        return;
    }

    const AutoCycleCaptureInput captureInput { m_sceneController.State().isSceneMode,
                                               m_sceneController.State().isInteractiveRun,
                                               m_sceneController.Scene().SceneEntityCount(),
                                               m_camera.autoCycleInterval,
                                               m_camera.autoCycleAccum,
                                               m_camera.autoCycleShotsTaken,
                                               m_camera.trackBallRow.value };
    AutoCycleCaptureUpdate captureUpdate;
    const RuntimeCaptureResult result = m_capture.TickAutoCycle( captureInput, captureUpdate, BackbufferCapture() );
    if ( captureUpdate.apply )
    {
        m_camera.autoCycleAccum = captureUpdate.accumulatedSeconds;
        m_camera.autoCycleShotsTaken = captureUpdate.shotsTaken;
        m_camera.trackBallRow.value = captureUpdate.trackedBallIndex;
    }

    if ( !result.captureResult.Ok() )
    {
        // Recoverable error: auto-cycle captures are validation side effects; failed file
        // output exits the run rather than recording a false capture success.
        fprintf( stderr, "%s: %s\n", result.captureResult.ErrorOwner(), result.captureResult.ErrorMessage() );
        fflush( stderr );
        PrintRuntimeExitReason( "Exiting because auto-cycle screenshot capture failed." );
        m_applicationExit.RequestPhaseFailure( result.captureResult );
        PostQuitMessage( 1 );
        return;
    }

    if ( result.completion != RuntimeCaptureCompletion::AutoCycle )
    {
        return;
    }

#ifdef _DEBUG
    LogSceneFinished( "auto_cycle" );
#endif

    if ( result.automation == RuntimeCaptureAutomation::Quit )
    {
        PostQuitMessage( 0 );
    }
    else if ( result.automation == RuntimeCaptureAutomation::HoldInteractive )
    {
        m_sceneController.MarkInteractiveRunComplete();
        m_capture.DisableAutomationExit();
        m_camera.StopAutoCycle();
    }
}


bool Run::TickSceneAdvance( const SceneFrameProceedPolicy& proceedPolicy )
{
    const SceneAutomationGateStatus automationGateStatus = m_validationHarness->SceneGates().Status();
    const SceneFrameAdvanceResult
        result = m_sceneController.AdvanceFrame( automationGateStatus, proceedPolicy.proceedAllowed,
                                                 m_diagnosticsRuntime.PerfTestActive(),
                                                 m_capture.Screenshot().isScreenshotSaved,
                                                 RunCameraModeUsesManualControls( m_camera.mode,
                                                                                  m_attachedCamera.State().activeFollow,
                                                                                  m_camera.director.grabbed ),
                                                 m_timers.SceneElapsedSeconds() );

    if ( result.reportMissingRequirements )
    {
        m_validationHarness->SceneGates().PrintMissingRequirements();
    }

#ifdef _DEBUG

    if ( result.finishReason )
    {
        LogSceneFinished( result.finishReason );
    }
#endif

    if ( result.holdInteractive )
    {
        m_capture.DisableAutomationExit();
        m_camera.StopAutoCycle();
    }

    SkullbonezCore::Core::SbResult loadResult = SkullbonezCore::Core::SbResult::Success();
    bool loadSucceeded = true;

    if ( result.loadRequest.HasLoad() )
    {
        PrepareSceneScopedOwnersForTransition();
        SceneLoadTransaction sceneLoad;
        sceneLoad.CaptureSubmittedState( m_camera, CaptureSceneLoadNavigationState( m_operatorUi->SceneNavigation() ),
                                         ProjectScenePresentationValues( m_overlayDiagnostics->PresentationSnapshot() ),
                                         { Renderer().VsyncEnabled(), Renderer().PipelineSyncEnabled() },
                                         Renderer().RendererName(), m_timers.SimulationTotalSeconds() );

        loadResult = LoadSceneRequest( sceneLoad, result.loadRequest );
        loadSucceeded = loadResult.Ok();

        ApplyRuntimeFrameMetricsLifecycle( m_metricsSceneLifecyclePolicy, m_sceneController.LifecyclePacket(), m_timers );
        ApplySceneLoadRuntimeReactions( sceneLoad );

        {
            const SceneLoadResult& presentation = BeginSceneLoadPresentation( sceneLoad, *m_validationHarness,
                                                                              m_sceneController );
            const SceneLifecyclePacket& lifecycle = m_sceneController.LifecyclePacket();
            ApplySceneLoadRenderPresentation( lifecycle, &Renderer().RenderDevice(), Renderer().VsyncEnabled() );
            ApplySceneLoadWindowUiPresentation( presentation, m_window, *m_operatorUi );
            ApplySceneLoadGraphicsStressPresentation( lifecycle, m_graphicsStress, m_graphicsStressSceneObserver,
                                                      m_launchOptions );
            sceneLoad.CompletePresentation();
        }
    }

    if ( loadSucceeded && result.restartSimulationTimerAfterLoad )
    {
        m_timers.RestartSceneClock();
    }

    const SceneAdvanceExitDisposition exitDisposition = ResolveSceneAdvanceExitDisposition( result.requestQuit,
                                                                                            loadSucceeded,
                                                                                            result.quitIfLoadFails );
    const SceneAdvanceExitAction exitAction = ApplySceneAdvanceExitDisposition( exitDisposition, loadResult,
                                                                                m_applicationExit );

    if ( exitAction.postQuit )
    {
        PostQuitMessage( exitAction.messageExitCode );
    }

    if ( !loadSucceeded && !result.quitIfLoadFails )
    {
        return false;
    }

    return result.restartFrame;
}


void Run::UpdateLogic( float simulationDt, float cameraDt, float presentationAlpha )
{
    m_camera.AdvanceAutoCycleClock( m_sceneController.State().isSceneMode, simulationDt );
    const AttachedCameraState& attachedState = m_attachedCamera.State();
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( m_camera.mode ) && attachedState.activeFollow &&
                                         attachedState.submode != AttachedCameraSubmode::RagdollEyes;
    const bool flyControlsActive = RunCameraModeUsesFlyControls( m_camera.mode, attachedState.activeFollow,
                                                                 m_camera.director.grabbed );
    const bool manualControlsActive = RunCameraModeUsesManualControls( m_camera.mode, attachedState.activeFollow,
                                                                       m_camera.director.grabbed );
    m_camera.TickControls( m_sceneController.Scene(), m_attachedCamera, cameraDt, presentationAlpha,
                           m_sceneController.Scene().Environment().GetFluidSurfaceHeight(), attachedOrbitOwnsCamera,
                           flyControlsActive, m_editorTools.Editor().editorModeEnabled,
                           m_editorTools.Editor().viewportLookActive, manualControlsActive,
                           m_sceneController.State().isSceneMode );

    DemoDirectorPredictionView directorPrediction;
    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    directorPrediction.revealAvailable = replayInput.predictionRevealAvailable;
    directorPrediction.revealProgress = replayInput.predictionRevealProgress;
    const DemoCameraPose currentPose = CaptureDemoDirectorPose( m_sceneController.Scene().Cameras() );
    const DemoDirectorTickResult directorResult = DemoDirectorPlayback::Tick( m_camera.director,
                                                                              m_camera.mode == RunCameraMode::Director,
                                                                              directorPrediction, currentPose, cameraDt );
    ApplyDemoDirectorTickResult( directorResult );

    m_sceneController.Scene()
        .Environment()
        .ApplyFluidSurfaceAdjustment( m_inputRouter.RuntimeSnapshot().fluidSurfaceAdjustment, simulationDt );
}
