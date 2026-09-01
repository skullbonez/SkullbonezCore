//   - Agentic/Reference/engine-glossary.md//
// File: SkullbonezTests/TestRuntimeValueSeams.cpp
// Purpose:
//   Lock CPU-only interaction, render-policy, and replay-overlay value seams.
//
// Summary:
//   These tests exercise the frame-policy matrix and the screen-space control
//   packets shared by replay input and drawing, including the cause hierarchy's
//   ratified 84 px safe top. They deliberately avoid the engine loop and
//   renderer so failures identify owner logic directly.
//
// Glossary:

//   Control surface: Fixed-capacity ordered table of replay controls and hit
//     regions rebuilt for one frame.
//   Historical sample: Retained replay state that must not advance live physics.
//
// Invariants:
//   - Historical or disabled scenes always publish zero physics time scale.
//   - Cross-scene pause policy outranks live tool owners and releases only for
//     the sampled step input.
//   - Replay hit regions and drawn rectangles come from the same layout helpers.
//   - Disabled front-most controls consume the pointer without publishing an
//     actionable hot control behind them.
//   - Every gesture kind reaches a compatible owner/capture transition, while
//     every owner/payload/capture rejection leaves all controller state unchanged.
//   - Replay prediction retains a 20-second default while the control surface
//     exposes the independently bounded 120-second operator maximum.
//
// Related:
//   - SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp
//   - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp
//   - SkullbonezSource/Runtime/App/SceneLoadApplication.h
//   - SkullbonezSource/Runtime/UI/RuntimeUiSurface.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/App/InputFrame.h"
#include "../SkullbonezSource/Runtime/App/GraphicsStressApplication.h"
#include "../SkullbonezSource/Runtime/App/SceneLoadApplication.h"
#include "../SkullbonezSource/Runtime/Camera/CameraControlState.h"
#include "../SkullbonezSource/Runtime/Input/InputController.h"
#include "../SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayCoordination.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayPresentation.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRuntimePackets.h"
#include "../SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h"
#include "../SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h"
#include "../SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h"
#include "../SkullbonezSource/Runtime/Scene/SceneController.h"
#include "../SkullbonezSource/Runtime/Scene/SceneNavigationModel.h"
#include "../SkullbonezSource/Runtime/UI/OperatorUiPhase.h"
#include "../SkullbonezSource/Runtime/UI/RuntimeUiSurface.h"
#include "../SkullbonezSource/UI/UIDrawList.h"
#include "../SkullbonezSource/UI/UIDrawWidgets.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayOverlay;

TEST_CASE( "Graphics stress routes every authored action to one concrete owner" )
{
    const std::array<GraphicsStressActionOwner, 32> expected = {
        GraphicsStressActionOwner::Cinematic,           GraphicsStressActionOwner::Cinematic,
        GraphicsStressActionOwner::Cinematic,           GraphicsStressActionOwner::SceneBrowser,
        GraphicsStressActionOwner::Renderer,            GraphicsStressActionOwner::Renderer,
        GraphicsStressActionOwner::PresentationOverlay, GraphicsStressActionOwner::PresentationOverlay,
        GraphicsStressActionOwner::PresentationOverlay, GraphicsStressActionOwner::PresentationOverlay,
        GraphicsStressActionOwner::PresentationOverlay, GraphicsStressActionOwner::PresentationOverlay,
        GraphicsStressActionOwner::PresentationOverlay, GraphicsStressActionOwner::PresentationOverlay,
        GraphicsStressActionOwner::PresentationOverlay, GraphicsStressActionOwner::TimeScale,
        GraphicsStressActionOwner::World,               GraphicsStressActionOwner::OperatorUi,
        GraphicsStressActionOwner::GeneratedScene,      GraphicsStressActionOwner::Tornado,
        GraphicsStressActionOwner::Tornado,             GraphicsStressActionOwner::OperatorUi,
        GraphicsStressActionOwner::ScenePhysics,        GraphicsStressActionOwner::ScenePhysics,
        GraphicsStressActionOwner::RuntimeOverlay,      GraphicsStressActionOwner::RuntimeOverlay,
        GraphicsStressActionOwner::RuntimeTool,         GraphicsStressActionOwner::Camera,
        GraphicsStressActionOwner::GeneratedScene,      GraphicsStressActionOwner::OperatorUi,
        GraphicsStressActionOwner::OperatorUi,          GraphicsStressActionOwner::RuntimeOverlay,
    };

    for ( int action = 0; action < static_cast<int>( expected.size() ); ++action )
    {
        CAPTURE( action );
        CHECK( GraphicsStressOwnerForAction( action ) == expected[static_cast<std::size_t>( action )] );
    }

    CHECK( GraphicsStressOwnerForAction( -1 ) == GraphicsStressActionOwner::Invalid );
    CHECK( GraphicsStressOwnerForAction( 32 ) == GraphicsStressActionOwner::Invalid );
}

TEST_CASE( "Passive camera floor follows the live fluid surface without inventing out-of-bounds terrain" )
{
    CHECK( InputController::ResolvePassiveCameraMinimumY( 4.0f, 20.0f, 1.5f ) == doctest::Approx( 21.5f ) );
    CHECK( InputController::ResolvePassiveCameraMinimumY( 30.0f, 20.0f, 1.5f ) == doctest::Approx( 31.5f ) );

    const float missingTerrain = -( std::numeric_limits<float>::max )();
    CHECK( InputController::ResolvePassiveCameraMinimumY( missingTerrain, 20.0f, 1.5f ) == missingTerrain );
    CHECK( InputController::ResolvePassiveCameraY( 5.0f, 4.0f, 120.0f, 1.5f, 110.0f ) ==
           doctest::Approx( 110.0f ) );
    CHECK( InputController::ResolvePassiveCameraY( 140.0f, missingTerrain, 20.0f, 1.5f, 110.0f ) ==
           doctest::Approx( 110.0f ) );
    CHECK( InputController::ResolvePassiveCameraY( 50.0f, missingTerrain, 20.0f, 1.5f, 110.0f ) ==
           doctest::Approx( 50.0f ) );
}

TEST_CASE( "Replay scrub camera policy ignores an ordinary historical cursor" )
{
    // Historical pause is deliberately absent from this policy boundary: a
    // timeline drag may pause simulation but cannot select or tween a camera.
    CHECK_FALSE( ReplayScrubNeedsInspectionCamera( false, RunReplayCameraFocusKind::None ) );
    CHECK( ReplayScrubNeedsInspectionCamera( true, RunReplayCameraFocusKind::None ) );
    CHECK( ReplayScrubNeedsInspectionCamera( false, RunReplayCameraFocusKind::Body ) );
}

TEST_CASE( "Replay coordination commands retain only their action payload" )
{
    const ReplayTransportCommand recording = ReplaySetRecordingEnabledCommand { true };
    const ReplayTransportCommand scrub = ReplayScrubCommand { 0.375f };
    const ReplayTransportCommand horizon = ReplaySetPredictionHorizonCommand { 42.0f };
    const ReplayTransportCommand selection = ReplaySelectCauseRowCommand { 7 };

    CHECK( ReplayTransportCommandAction( recording ) == ReplayTransportAction::SetRecordingEnabled );
    CHECK( std::get<ReplaySetRecordingEnabledCommand>( recording ).enabled );
    CHECK( ReplayTransportCommandAction( scrub ) == ReplayTransportAction::Scrub );
    CHECK( std::get<ReplayScrubCommand>( scrub ).normalized == doctest::Approx( 0.375f ) );
    CHECK( ReplayTransportCommandAction( horizon ) == ReplayTransportAction::SetPredictionHorizon );
    CHECK( std::get<ReplaySetPredictionHorizonCommand>( horizon ).seconds == doctest::Approx( 42.0f ) );
    CHECK( ReplayTransportCommandAction( selection ) == ReplayTransportAction::SelectCauseRow );
    CHECK( std::get<ReplaySelectCauseRowCommand>( selection ).rowIndex == 7 );
    CHECK_FALSE( std::holds_alternative<ReplayScrubCommand>( selection ) );

    const ReplayStartupRequest startup {
        ReplayStartupLoadRequest { "capture.sbrv2", true },
#ifdef _DEBUG
        ReplayStartupRestoreFileProbeRequests { "checkpoint.sbrv2", "target.sbrv2", "branch.sbrv2", "failure.sbrv2" },
        ReplayStartupNormalizedProbeRequest { true, 0.25f },
        ReplayStartupNormalizedProbeRequest { true, 0.75f },
        ReplayStartupSaveProbeRequest { true, "saved.sbrv2" }
#endif
    };
    CHECK( std::strcmp( startup.load.path, "capture.sbrv2" ) == 0 );
    CHECK( startup.load.validationProbe );
#ifdef _DEBUG
    CHECK( std::strcmp( startup.restoreFiles.targetPath, "target.sbrv2" ) == 0 );
    CHECK( startup.scrub.enabled );
    CHECK( startup.scrub.normalized == doctest::Approx( 0.25f ) );
    CHECK( startup.restore.normalized == doctest::Approx( 0.75f ) );
    CHECK( std::strcmp( startup.save.path, "saved.sbrv2" ) == 0 );
#endif
}

TEST_CASE( "Scene defaults save snapshot detaches every borrowed owner section" )
{
    ScenePresentationValues presentation;
    presentation.textOnly = true;
    presentation.waterFreeze = true;
    presentation.terrainHidden = true;
    presentation.physicsDebugFlags =
        SkullbonezCore::Physics::PHYSICS_DEBUG_AXES | SkullbonezCore::Physics::PHYSICS_DEBUG_CONTACTS;
    presentation.physicsDebugTransparent = true;
    presentation.physicsDebugAlpha = 0.625f;

    SceneRenderPolicyState renderPolicy;
    renderPolicy.vsyncEnabled = false;
    renderPolicy.pipelineSyncEnabled = true;

    CameraControlState camera;
    camera.trackBallRow.value = 0;
    camera.trackHeight = 123.0f;
    camera.autoCycleInterval = 4.5f;

    SkullbonezCore::UI::RunSceneUIOverrideState uiOverrides;
    uiOverrides.modelCountOverride = 9;
    const SceneDefaultsSaveSnapshot snapshot =
        ProjectSceneDefaultsSaveSnapshot( presentation, renderPolicy, camera, uiOverrides );
    presentation.textOnly = false;
    renderPolicy.vsyncEnabled = true;
    camera.trackHeight = 900.0f;
    uiOverrides.modelCountOverride = 1;

    CHECK( snapshot.presentation.textOnly );
    CHECK( snapshot.presentation.waterFreeze );
    CHECK( snapshot.presentation.terrainHidden );
    CHECK( snapshot.presentation.physicsDebugFlags ==
           ( SkullbonezCore::Physics::PHYSICS_DEBUG_AXES | SkullbonezCore::Physics::PHYSICS_DEBUG_CONTACTS ) );
    CHECK( snapshot.presentation.physicsDebugTransparent );
    CHECK( snapshot.presentation.physicsDebugAlpha == doctest::Approx( 0.625f ) );
    CHECK_FALSE( snapshot.renderPolicy.vsyncEnabled );
    CHECK( snapshot.renderPolicy.pipelineSyncEnabled );
    CHECK( snapshot.camera.writeTrackHeight );
    CHECK( snapshot.camera.trackHeight == doctest::Approx( 123.0f ) );
    CHECK( snapshot.camera.writeAutoCycleInterval );
    CHECK( snapshot.camera.autoCycleInterval == doctest::Approx( 4.5f ) );
    CHECK( snapshot.generatedCounts.modelCount == 9 );
}

namespace
{
RuntimeInteractionFrameInput DefaultFrameInput()
{
    RuntimeInteractionFrameInput input;
    input.scenePhysicsEnabled = true;
    input.sceneTimeScale = 1.5f;
    return input;
}

int RectCenterX( const SkullbonezCore::UI::UIRect& rect )
{
    return static_cast<int>( std::round( rect.x + rect.w * 0.5f ) );
}

int RectCenterY( const SkullbonezCore::UI::UIRect& rect )
{
    return static_cast<int>( std::round( rect.y + rect.h * 0.5f ) );
}
} // namespace

TEST_CASE( "Runtime composition maps camera modes into interaction-owned workspaces" )
{
    RuntimeInteractionController controller;

    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Demo ).workspace == RuntimeWorkspace::Live );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Scene ).workspace == RuntimeWorkspace::Live );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Director ).workspace == RuntimeWorkspace::Live );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Inspect ).workspace == RuntimeWorkspace::Inspect );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Attach ).workspace == RuntimeWorkspace::Inspect );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Launcher ).owner == WorldInteractionOwner::Launcher );
    CHECK( EnterInteractionForCameraMode( controller, RunCameraMode::Manipulator ).owner ==
           WorldInteractionOwner::Manipulator );
}

TEST_CASE( "Operator UI phase: detached facts and process commands cross one ordered frame" )
{
    OperatorUiFrameSnapshot snapshot;
    snapshot.uiText.cameraModeLabel = "Orbit";
    snapshot.uiText.presentationAlpha = 0.25f;
    snapshot.metrics.uiDrawCalls = 9;
    snapshot.viewportWidth = 1920;
    snapshot.viewportHeight = 1080;
    snapshot.secondarySurfaceVisible = true;

    OperatorUiPhaseOwner phase;
    phase.Begin( snapshot );
    snapshot.uiText.presentationAlpha = 0.75f;
    snapshot.metrics.uiDrawCalls = 99;

    CHECK( phase.Snapshot().uiText.presentationAlpha == doctest::Approx( 0.25f ) );
    CHECK( phase.Snapshot().metrics.uiDrawCalls == 9 );
    CHECK( phase.Snapshot().viewportWidth == 1920 );
    CHECK( phase.Snapshot().secondarySurfaceVisible );
    phase.Compose( false, true, true, false );
    CHECK( phase.SubmissionPlan().composeGameUi );
    CHECK_FALSE( phase.SubmissionPlan().submitOverlay );
    CHECK( phase.SubmissionPlan().submitReplay );
    CHECK_FALSE( phase.SubmissionPlan().finalizeOverlay );
    phase.RecordGpuSubmission( 7 );

    phase.EmitCommands( true, true );
    phase.Complete();

    CHECK( phase.CurrentPhase() == OperatorUiPhaseOwner::Phase::Complete );
    CHECK( phase.GameUiDrawCalls() == 7 );
    CHECK( phase.Commands().surface == OperatorUiSurfaceCommand::ShowGameUi );
    CHECK( phase.Commands().requestTracyStandardCapture );
}

TEST_CASE( "Operator UI phase: hidden GameUI and ImGui consume the same immutable facts" )
{
    OperatorUiFrameSnapshot shared;
    shared.uiText.cameraModeEnabledMask = 0x5u;
    shared.uiText.presentationPinned = true;
    shared.metrics.sceneEnergy = 12.5f;

    OperatorUiPhaseOwner hidden;
    OperatorUiPhaseOwner gameUi;
    OperatorUiPhaseOwner imgui;
    hidden.Begin( shared );
    gameUi.Begin( shared );
    imgui.Begin( shared );

    CHECK( hidden.Snapshot().uiText.cameraModeEnabledMask == gameUi.Snapshot().uiText.cameraModeEnabledMask );
    CHECK( gameUi.Snapshot().uiText.cameraModeEnabledMask == imgui.Snapshot().uiText.cameraModeEnabledMask );
    CHECK( hidden.Snapshot().uiText.presentationPinned == imgui.Snapshot().uiText.presentationPinned );
    CHECK( hidden.Snapshot().metrics.sceneEnergy == doctest::Approx( imgui.Snapshot().metrics.sceneEnergy ) );
}

TEST_CASE( "Operator UI phase: only adjacent operations belong to the fatal phase walk" )
{
    using Phase = OperatorUiPhaseOwner::Phase;
    constexpr std::array phases { Phase::Idle, Phase::Snapshot, Phase::Composed,
                                  Phase::Submitted, Phase::CommandsEmitted, Phase::Complete };

    for ( std::size_t fromIndex = 0u; fromIndex < phases.size(); ++fromIndex )
    {
        for ( std::size_t toIndex = 0u; toIndex < phases.size(); ++toIndex )
        {
            const bool adjacent = fromIndex + 1u == toIndex;
            CHECK( OperatorUiPhaseOwner::IsLegalTransition( phases[fromIndex], phases[toIndex] ) == adjacent );
        }
    }
}

TEST_CASE( "Operator UI phase: submission plan preserves text-only hidden and visible routing" )
{
    OperatorUiPhaseOwner textOnlyPhase;
    textOnlyPhase.Begin( {} );
    textOnlyPhase.Compose( true, true, true, false );
    const OperatorUiSubmissionPlan& textOnly = textOnlyPhase.SubmissionPlan();
    CHECK_FALSE( textOnly.composeGameUi );
    CHECK_FALSE( textOnly.submitOverlay );
    CHECK_FALSE( textOnly.submitReplay );
    CHECK_FALSE( textOnly.finalizeOverlay );

    OperatorUiPhaseOwner visiblePhase;
    visiblePhase.Begin( {} );
    visiblePhase.Compose( false, true, true, false );
    const OperatorUiSubmissionPlan& visible = visiblePhase.SubmissionPlan();
    CHECK( visible.composeGameUi );
    CHECK_FALSE( visible.submitOverlay );
    CHECK( visible.submitReplay );
    CHECK_FALSE( visible.finalizeOverlay );

    OperatorUiPhaseOwner hiddenPhase;
    hiddenPhase.Begin( {} );
    hiddenPhase.Compose( false, false, false, false );
    const OperatorUiSubmissionPlan& hidden = hiddenPhase.SubmissionPlan();
    CHECK_FALSE( hidden.composeGameUi );
    CHECK( hidden.submitOverlay );
    CHECK( hidden.submitReplay );
    CHECK( hidden.finalizeOverlay );

    OperatorUiPhaseOwner hiddenNeedsUiPhase;
    hiddenNeedsUiPhase.Begin( {} );
    hiddenNeedsUiPhase.Compose( false, true, false, false );
    const OperatorUiSubmissionPlan& hiddenNeedsUi = hiddenNeedsUiPhase.SubmissionPlan();
    CHECK( hiddenNeedsUi.composeGameUi );
    CHECK( hiddenNeedsUi.submitOverlay );
    CHECK( hiddenNeedsUi.submitReplay );
    CHECK( hiddenNeedsUi.finalizeOverlay );

    OperatorUiPhaseOwner profilerPhase;
    profilerPhase.Begin( {} );
    profilerPhase.Compose( false, false, false, true );
    const OperatorUiSubmissionPlan& profiler = profilerPhase.SubmissionPlan();
    CHECK( profiler.submitOverlay );
    CHECK( profiler.submitReplay );
    CHECK_FALSE( profiler.finalizeOverlay );

    OperatorUiPhaseOwner noCommandPhase;
    noCommandPhase.Begin( {} );
    noCommandPhase.Compose( true, false, false, false );
    noCommandPhase.RecordGpuSubmission( 0 );
    noCommandPhase.EmitCommands( false, false );
    noCommandPhase.Complete();
    CHECK( noCommandPhase.Commands().surface == OperatorUiSurfaceCommand::None );
    CHECK_FALSE( noCommandPhase.Commands().requestTracyStandardCapture );
}

TEST_CASE( "Render policy: unavailable raytracing cannot select DXR reflection" )
{
    RuntimeRenderFramePolicy policy;
    policy.waterRTReflect = true;
    CHECK_FALSE( ShouldUseDxrReflection( false, policy, false, false ) );
    CHECK( ShouldUseDxrReflection( true, policy, false, false ) );

    policy.waterNoReflect = true;
    CHECK_FALSE( ShouldUseDxrReflection( true, policy, false, false ) );
    policy.waterNoReflect = false;
    CHECK_FALSE( ShouldUseDxrReflection( true, policy, true, false ) );
    CHECK_FALSE( ShouldUseDxrReflection( true, policy, false, true ) );
}

TEST_CASE( "Replay render publication keeps time and renderer consumers independent" )
{
    ReplayRenderFrameViews views {};
    views.time.liveAdvanceHeld = true;
    views.render.focusFadeActive = true;

    CHECK( views.time.liveAdvanceHeld );
    CHECK( views.render.focusFadeActive );
    CHECK( views.time.presentationSample == nullptr );
    CHECK( views.render.focusModelMask == nullptr );
    CHECK( views.render.visualPacket == nullptr );
}

TEST_CASE( "Scene controller: one proceed policy governs the complete frame" )
{
    SceneFrameProceedPolicy policy = ResolveSceneFrameProceedPolicy( false, false );
    CHECK_FALSE( policy.stepRequested );
    CHECK_FALSE( policy.crossScenePauseLocked );
    CHECK( policy.proceedAllowed );

    policy = ResolveSceneFrameProceedPolicy( true, false );
    CHECK_FALSE( policy.stepRequested );
    CHECK( policy.crossScenePauseLocked );
    CHECK_FALSE( policy.proceedAllowed );

    policy = ResolveSceneFrameProceedPolicy( true, true );
    CHECK( policy.stepRequested );
    CHECK( policy.crossScenePauseLocked );
    CHECK( policy.proceedAllowed );
}

TEST_CASE( "Scene advance exit policy preserves queued load failure" )
{
    CHECK( ResolveSceneAdvanceExitDisposition( false, true, false ) == SceneAdvanceExitDisposition::None );
    CHECK( ResolveSceneAdvanceExitDisposition( true, true, false ) == SceneAdvanceExitDisposition::Normal );
    CHECK( ResolveSceneAdvanceExitDisposition( false, false, true ) == SceneAdvanceExitDisposition::LoadFailure );
    CHECK( ResolveSceneAdvanceExitDisposition( true, false, true ) == SceneAdvanceExitDisposition::LoadFailure );

    SkullbonezCore::Core::SbDiagnosticStore diagnostics;
    ApplicationExitState exitState( diagnostics );
    const SkullbonezCore::Core::SbResult loadFailure =
        diagnostics.Failure( "Runtime/SceneLoad", "queued scene could not be loaded" );
    const SceneAdvanceExitDisposition failureDisposition = ResolveSceneAdvanceExitDisposition( false, false, true );
    const SceneAdvanceExitAction failureAction =
        ApplySceneAdvanceExitDisposition( failureDisposition, loadFailure, exitState );
    const SkullbonezCore::Core::SbResult processResult = exitState.Resolve( failureAction.messageExitCode );

    CHECK( failureAction.postQuit );
    CHECK( failureAction.messageExitCode == 1 );
    CHECK( exitState.ExitRequested() );
    CHECK_FALSE( processResult.Ok() );
    CHECK( std::strcmp( processResult.ErrorOwner(), "Runtime/SceneLoad" ) == 0 );
    CHECK( std::strcmp( processResult.ErrorMessage(), "queued scene could not be loaded" ) == 0 );
}

TEST_CASE( "Runtime interaction: physics input matrix publishes one frame policy" )
{
    RuntimeInteractionController controller;
    RuntimeInteractionFrameInput input = DefaultFrameInput();

    SUBCASE( "disabled scene and historical replay lock physics" )
    {
        input.scenePhysicsEnabled = false;
        RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Disabled );
        CHECK( policy.physicsTimeScale == 0.0f );

        input.scenePhysicsEnabled = true;
        input.replayScrubbedHistoricalSample = true;
        input.forcePhysicsRunning = true;
        policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Disabled );
        CHECK( policy.physicsTimeScale == 0.0f );
    }

    SUBCASE( "live-edge replay waits for step hold unless forced" )
    {
        input.replayLiveHeldAtCurrentFrame = true;
        RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        CHECK( policy.physicsTimeScale == 0.0f );

        input.stepHeld = true;
        policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        CHECK( policy.physicsTimeScale == doctest::Approx( 1.5f ) );

        input.stepHeld = false;
        input.forcePhysicsRunning = true;
        policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Running );
        CHECK( policy.physicsTimeScale == doctest::Approx( 1.5f ) );
    }

    SUBCASE( "inspection edit and replay workspaces require step hold" )
    {
        controller.EnterInspect();
        CHECK( controller.BuildFramePolicy( input ).physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        controller.EnterEdit();
        CHECK( controller.BuildFramePolicy( input ).physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        controller.EnterReplay();
        CHECK( controller.BuildFramePolicy( input ).physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
    }

    SUBCASE( "launcher and manipulator gestures keep live physics running" )
    {
        controller.EnterLauncher();
        RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
        CHECK( policy.launcherActive );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Running );

        RuntimeInteractionGesture pickup;
        pickup.kind = RuntimeInteractionGestureKind::MousePickupDrag;
        pickup.button = RuntimePointerButton::Left;
        pickup.body = SkullbonezCore::Physics::PhysicsBodyHandle { 3u, 2u };
        REQUIRE( controller.BeginOwnedToolGesture( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, pickup ) );
        policy = controller.BuildFramePolicy( input );
        CHECK( policy.manipulatorActive );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Running );
        CHECK( policy.pointerCapture == RuntimePointerCaptureOwner::ToolGesture );
    }

    SUBCASE( "cross-scene pause lock outranks live tool owners" )
    {
        controller.EnterLauncher();
        input.crossScenePauseLocked = true;
        RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        CHECK( policy.physicsTimeScale == 0.0f );

        input.stepHeld = true;
        policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::RunWhileStepHeld );
        CHECK( policy.physicsTimeScale == doctest::Approx( 1.5f ) );
    }

    SUBCASE( "negative scene scale clamps before policy publication" )
    {
        input.sceneTimeScale = -4.0f;
        const RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
        CHECK( policy.physicsAdvance == PhysicsAdvanceState::Running );
        CHECK( policy.physicsTimeScale == 0.0f );
    }
}

TEST_CASE( "Runtime interaction: camera policy follows owner precedence" )
{
    RuntimeInteractionController controller;
    RuntimeInteractionFrameInput input = DefaultFrameInput();
    input.rightMouseLookHeld = true;
    input.replayInspectionLookActive = true;
    input.editorViewportLookActive = true;

    RuntimeInteractionFramePolicy policy = controller.BuildFramePolicy( input );
    CHECK( policy.cameraLook == CameraLookState::EditorViewportLook );
    CHECK( policy.cameraMouseLookActive );
    CHECK( policy.cameraKeyboardControlsActive );

    input.editorViewportLookActive = false;
    policy = controller.BuildFramePolicy( input );
    CHECK( policy.cameraLook == CameraLookState::ReplayInspectionLook );

    input.replayInspectionLookActive = false;
    policy = controller.BuildFramePolicy( input );
    CHECK( policy.cameraLook == CameraLookState::RightMouseLook );

    RuntimeInteractionGesture pickup;
    pickup.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    pickup.button = RuntimePointerButton::Left;
    pickup.body = SkullbonezCore::Physics::PhysicsBodyHandle { 4u, 1u };
    REQUIRE( controller.BeginOwnedToolGesture( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, pickup ) );
    policy = controller.BuildFramePolicy( input );
    CHECK( policy.cameraLook == CameraLookState::Passive );
    CHECK_FALSE( policy.cameraMouseLookActive );
}

TEST_CASE( "Runtime interaction: every gesture preserves owner and capture consistency" )
{
    struct GestureCase
    {
        RuntimeInteractionGestureKind kind;
        RuntimeWorkspace workspace;
        WorldInteractionOwner owner;
        RuntimeGizmoDragKind gizmoKind = RuntimeGizmoDragKind::None;
        int axis = -1;
        bool requiresBody = false;
    };

    constexpr GestureCase cases[] = {
        { RuntimeInteractionGestureKind::ObjectPick, RuntimeWorkspace::Live, WorldInteractionOwner::Launcher },
        { RuntimeInteractionGestureKind::EditorPlacementScaleDrag, RuntimeWorkspace::Edit,
          WorldInteractionOwner::EditorPlacement },
        { RuntimeInteractionGestureKind::GizmoDrag, RuntimeWorkspace::Edit, WorldInteractionOwner::EditorGizmo,
          RuntimeGizmoDragKind::Translate, 0, true },
        { RuntimeInteractionGestureKind::GizmoDrag, RuntimeWorkspace::Inspect, WorldInteractionOwner::InspectGizmo,
          RuntimeGizmoDragKind::Rotate, 1, true },
        { RuntimeInteractionGestureKind::MousePickupDrag, RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator,
          RuntimeGizmoDragKind::None, -1, true },
        { RuntimeInteractionGestureKind::ReplayScrubDrag, RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub },
        { RuntimeInteractionGestureKind::ReplayVelocityDrag, RuntimeWorkspace::Replay,
          WorldInteractionOwner::ReplayVelocityEdit, RuntimeGizmoDragKind::None, 2, true },
        { RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag, RuntimeWorkspace::Replay,
          WorldInteractionOwner::ReplayPrediction },
        { RuntimeInteractionGestureKind::ReplayCauseTreeDrag, RuntimeWorkspace::Replay,
          WorldInteractionOwner::ReplayCauseTree, RuntimeGizmoDragKind::None, 0 },
    };

    for ( const GestureCase& gestureCase : cases )
    {
        RuntimeInteractionController controller;
        RuntimeInteractionGesture gesture;
        gesture.kind = gestureCase.kind;
        gesture.button = RuntimePointerButton::Left;
        gesture.gizmoKind = gestureCase.gizmoKind;
        gesture.axis = gestureCase.axis;

        if ( gestureCase.requiresBody )
        {
            gesture.body = SkullbonezCore::Physics::PhysicsBodyHandle { 7u, 3u };
        }

        REQUIRE( controller.BeginOwnedToolGesture( gestureCase.workspace, gestureCase.owner, gesture ) );
        CHECK( controller.Workspace() == gestureCase.workspace );
        CHECK( controller.Owner() == gestureCase.owner );
        CHECK( controller.Gesture().kind == gestureCase.kind );
        CHECK( controller.PointerCapture() == RuntimePointerCaptureOwner::ToolGesture );
        controller.EndGestureIfKind( gestureCase.kind );
        CHECK( controller.Gesture().kind == RuntimeInteractionGestureKind::None );
        CHECK( controller.PointerCapture() == RuntimePointerCaptureOwner::None );
    }

    RuntimeInteractionController cameraController;
    RuntimeInteractionFrameInput frameInput = DefaultFrameInput();
    frameInput.rightMouseLookHeld = true;
    const RuntimeInteractionFramePolicy cameraPolicy = cameraController.BuildFramePolicy( frameInput );
    RuntimeInputSnapshot snapshot;
    snapshot.appFocused = true;
    snapshot.pointer.rightDown = true;
    cameraController.SyncCameraLookGesture( snapshot, cameraPolicy, true );
    CHECK( cameraController.Gesture().kind == RuntimeInteractionGestureKind::CameraLook );
    CHECK( cameraController.PointerCapture() == RuntimePointerCaptureOwner::CameraLook );
    cameraController.CancelCameraLookGesture();
    CHECK( cameraController.Gesture().kind == RuntimeInteractionGestureKind::None );
    CHECK( cameraController.PointerCapture() == RuntimePointerCaptureOwner::None );

    const auto expectRejected =
        []( RuntimeWorkspace workspace, WorldInteractionOwner owner, const RuntimeInteractionGesture& gesture )
    {
        RuntimeInteractionController controller;
        controller.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Live, WorldInteractionOwner::Launcher,
                                                        InteractionExitReason::EnterLauncher );
        const RuntimeWorkspace previousWorkspace = controller.Workspace();
        const WorldInteractionOwner previousOwner = controller.Owner();
        CHECK_FALSE( controller.BeginOwnedToolGesture( workspace, owner, gesture ) );
        CHECK( controller.Workspace() == previousWorkspace );
        CHECK( controller.Owner() == previousOwner );
        CHECK( controller.Gesture().kind == RuntimeInteractionGestureKind::None );
        CHECK( controller.PointerCapture() == RuntimePointerCaptureOwner::None );
    };

    RuntimeInteractionGesture invalid;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub, invalid );

    invalid.kind = RuntimeInteractionGestureKind::CameraLook;
    expectRejected( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, invalid );

    invalid = {};
    invalid.kind = RuntimeInteractionGestureKind::ObjectPick;
    expectRejected( RuntimeWorkspace::Live, WorldInteractionOwner::None, invalid );

    invalid = {};
    invalid.kind = RuntimeInteractionGestureKind::EditorPlacementScaleDrag;
    expectRejected( RuntimeWorkspace::Edit, WorldInteractionOwner::Launcher, invalid );

    RuntimeInteractionGesture gizmo;
    gizmo.kind = RuntimeInteractionGestureKind::GizmoDrag;
    gizmo.gizmoKind = RuntimeGizmoDragKind::Translate;
    gizmo.axis = 0;
    gizmo.body = SkullbonezCore::Physics::PhysicsBodyHandle { 2u, 1u };
    expectRejected( RuntimeWorkspace::Edit, WorldInteractionOwner::Launcher, gizmo );
    invalid = gizmo;
    invalid.gizmoKind = RuntimeGizmoDragKind::None;
    expectRejected( RuntimeWorkspace::Edit, WorldInteractionOwner::EditorGizmo, invalid );
    invalid = gizmo;
    invalid.axis = -1;
    expectRejected( RuntimeWorkspace::Edit, WorldInteractionOwner::EditorGizmo, invalid );
    invalid = gizmo;
    invalid.body = {};
    expectRejected( RuntimeWorkspace::Edit, WorldInteractionOwner::EditorGizmo, invalid );

    RuntimeInteractionGesture pickup;
    pickup.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    pickup.body = SkullbonezCore::Physics::PhysicsBodyHandle { 2u, 1u };
    expectRejected( RuntimeWorkspace::Live, WorldInteractionOwner::Launcher, pickup );
    invalid = pickup;
    invalid.body = {};
    expectRejected( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, invalid );

    invalid = {};
    invalid.kind = RuntimeInteractionGestureKind::ReplayScrubDrag;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::Launcher, invalid );

    invalid.kind = RuntimeInteractionGestureKind::ReplayPredictionHorizonDrag;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub, invalid );

    RuntimeInteractionGesture velocity;
    velocity.kind = RuntimeInteractionGestureKind::ReplayVelocityDrag;
    velocity.body = SkullbonezCore::Physics::PhysicsBodyHandle { 2u, 1u };
    velocity.axis = 0;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub, velocity );
    invalid = velocity;
    invalid.body = {};
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayVelocityEdit, invalid );
    invalid = velocity;
    invalid.axis = -1;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayVelocityEdit, invalid );

    RuntimeInteractionGesture causeTree;
    causeTree.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
    causeTree.axis = 0;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayScrub, causeTree );
    invalid = causeTree;
    invalid.axis = 2;
    expectRejected( RuntimeWorkspace::Replay, WorldInteractionOwner::ReplayCauseTree, invalid );

    RuntimeInteractionController commandController;
    commandController.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Live, WorldInteractionOwner::Launcher,
                                                           InteractionExitReason::EnterLauncher );
    RuntimeGestureCommand captureMismatch;
    captureMismatch.gesture.kind = RuntimeInteractionGestureKind::ObjectPick;
    captureMismatch.captureOwner = RuntimePointerCaptureOwner::CameraLook;
    RuntimeGestureEvent ignoredEvent;
    CHECK_FALSE( commandController.ApplyGestureCommand( captureMismatch, ignoredEvent ) );
    CHECK( commandController.Workspace() == RuntimeWorkspace::Live );
    CHECK( commandController.Owner() == WorldInteractionOwner::Launcher );
    CHECK( commandController.Gesture().kind == RuntimeInteractionGestureKind::None );
    CHECK( commandController.PointerCapture() == RuntimePointerCaptureOwner::None );
}

TEST_CASE( "Replay overlay: scrubber geometry clamps compact and wide screens" )
{
    const SkullbonezCore::UI::UIRect panel = ReplayScrubberPanelRect( 1920, 1080 );
    CHECK( panel.x == doctest::Approx( 420.0f ) );
    CHECK( panel.y == doctest::Approx( 1012.0f ) );
    CHECK( panel.w == doctest::Approx( 1080.0f ) );
    CHECK( panel.h == doctest::Approx( 50.0f ) );

    const SkullbonezCore::UI::UIRect track = ReplayScrubberTrackRect( 1920, 1080, RunReplayTrack::Solver );
    CHECK( track.x == doctest::Approx( 580.0f ) );
    CHECK( track.y == doctest::Approx( 1033.0f ) );
    CHECK( track.w == doctest::Approx( 202.0f ) );

    const SkullbonezCore::UI::UIRect compact = ReplayScrubberPanelRect( 200, 100 );
    CHECK( compact.x == doctest::Approx( -30.0f ) );
    CHECK( compact.y == doctest::Approx( 32.0f ) );
    CHECK( compact.w == doctest::Approx( 260.0f ) );

    CHECK( ReplayPredictionHorizonT( -10.0f ) == 0.0f );
    CHECK( REPLAY_FUTURE_DEFAULT_SECONDS == doctest::Approx( 20.0f ) );
    CHECK( REPLAY_PREDICTION_MAX_SECONDS == doctest::Approx( 120.0f ) );
    CHECK( ReplayPredictionHorizonT( REPLAY_PREDICTION_MAX_SECONDS + 10.0f ) == 1.0f );
    const SkullbonezCore::UI::UIRect horizon { 100.0f, 50.0f, 200.0f, 8.0f };
    CHECK( ReplayPredictionHorizonFromMouse( 0, horizon ) == REPLAY_PREDICTION_MIN_SECONDS );
    CHECK( ReplayPredictionHorizonFromMouse( 400, horizon ) == REPLAY_PREDICTION_MAX_SECONDS );
    const SkullbonezCore::UI::UIRect collapsedHorizon { 100.0f, 50.0f, 1.0f, 8.0f };
    CHECK( ReplayPredictionHorizonFromMouse( 100, collapsedHorizon ) == REPLAY_PREDICTION_MAX_SECONDS );

    CHECK( ReplayScrubberPositionFromMouse( -100, 1920, 1080, RunReplayTrack::Solver ) == 0.0f );
    CHECK( ReplayScrubberPositionFromMouse( 4000, 1920, 1080, RunReplayTrack::Solver ) == 1.0f );
}

TEST_CASE( "Runtime UI components preserve pointer ownership and action identity" )
{
    RuntimeUiSurface<3> surface;
    RuntimeUiControl disabled;
    disabled.id = RuntimeUiControlId { 1u };
    disabled.kind = RuntimeUiControlKind::Button;
    disabled.action = RuntimeUiActionId { 41u };
    disabled.drawRect = { 20.0f, 20.0f, 80.0f, 24.0f };
    disabled.hitRect = disabled.drawRect;
    disabled.enabled = false;
    REQUIRE( surface.TryAdd( disabled ) );

    RuntimeUiControl behind = disabled;
    behind.id = RuntimeUiControlId { 2u };
    behind.action = RuntimeUiActionId { 99u };
    behind.enabled = true;
    REQUIRE( surface.TryAdd( behind ) );

    surface.ResolvePointer( 40, 30 );
    CHECK( surface.consumesPointer );
    CHECK( surface.hasPointerControl );
    CHECK( surface.pointerControl == disabled.id );
    CHECK_FALSE( surface.hasHotControl );
    REQUIRE( surface.Find( disabled.id ) != nullptr );
    CHECK( surface.Find( disabled.id )->action.value == 41u );
    CHECK( surface.Find( behind.id )->action.value == 99u );
}

TEST_CASE( "Planning UI components render detached trip controls in owner order" )
{
    ReplayTripPlannerView planner;
    planner.visible = true;
    planner.available = true;
    ReplayTripPlannerSurface surface;
    BuildReplayTripPlannerSurface( planner, 1280, surface );
    REQUIRE( surface.controlCount == 6u );

    const ReplayTripPlannerControlRow* commit = surface.Find( ReplayTripPlannerControl::Commit );
    REQUIRE( commit != nullptr );
    CHECK( commit->action == ReplayTripPlannerCommandKind::Commit );
    CHECK_FALSE( commit->enabled );
    const SkullbonezCore::UI::UIVisualState commitState = ReplayTripPlannerControlVisualState( *commit );
    CHECK( SkullbonezCore::UI::HasVisualState( commitState, SkullbonezCore::UI::UIVisualState::Visible ) );
    CHECK_FALSE( SkullbonezCore::UI::HasVisualState( commitState, SkullbonezCore::UI::UIVisualState::Enabled ) );
    surface.ResolvePointer( RectCenterX( commit->hitRect ), RectCenterY( commit->hitRect ), false );
    CHECK( surface.consumesPointer );
    CHECK_FALSE( surface.hasHotControl );

    constexpr ReplayTripPlannerControl order[] = { ReplayTripPlannerControl::TimeOfFlightDecrease,
                                                   ReplayTripPlannerControl::TimeOfFlightIncrease,
                                                   ReplayTripPlannerControl::Plan, ReplayTripPlannerControl::Commit,
                                                   ReplayTripPlannerControl::Cancel };
    constexpr const char* labels[] = { "-", "+", "PLAN", "COMMIT", "CANCEL" };
    SkullbonezCore::UI::UIDrawList drawList;
    const SkullbonezCore::UI::UIDrawContext draw( 1280, 720, drawList );
    SkullbonezCore::UI::Widgets::DrawPanel( draw, ReplayTripPlannerPanelRect( 1280 ),
                                            SkullbonezCore::UI::UIVisualState::Visible |
                                                SkullbonezCore::UI::UIVisualState::Enabled,
                                            SkullbonezCore::UI::Widgets::ComponentAppearance::Compact );

    for ( std::size_t index = 0; index < std::size( order ); ++index )
    {
        const ReplayTripPlannerControlRow* row = surface.Find( order[index] );
        REQUIRE( row != nullptr );
        SkullbonezCore::UI::Widgets::DrawButton( draw, row->drawRect, labels[index],
                                                 ReplayTripPlannerControlVisualState( *row ),
                                                 SkullbonezCore::UI::Widgets::ComponentAppearance::Compact );
    }

    const std::span<const SkullbonezCore::UI::UIDrawList::Command> commands = drawList.Commands();
    REQUIRE( commands.size() == 12u );

    for ( std::size_t index = 0; index < std::size( labels ); ++index )
    {
        const SkullbonezCore::UI::UIDrawList::Command& text = commands[3u + index * 2u];
        CHECK( text.type == SkullbonezCore::UI::UIDrawList::CommandType::Text );
        CHECK( std::strcmp( drawList.TextAt( text.textOffset ), labels[index] ) == 0 );
    }

    CHECK( drawList.Fingerprint() == 309035145945859501ull );
}

TEST_CASE( "Replay overlay: surface description publishes owner availability as values" )
{
    ReplayScrubberView scrubber;
    scrubber.historicalSamplePaused = true;
    scrubber.activeTrack = RunReplayTrack::Solver;
    ReplayRecorderStats stats;
    stats.enabled = true;
    stats.sampleCount = 2u;

    ReplayScrubberSurfaceInput input =
        DescribeReplayScrubberAvailability( scrubber, stats, { false, true, true, false, true, true } );
    input.screenW = 1920;
    input.screenH = 1080;
    input.gesture = ReplayToolGestureKind::ScrubDrag;
    CHECK( input.track == RunReplayTrack::Solver );
    CHECK( input.solverToolsEnabled );
    CHECK( input.predictionToolsEnabled );
    CHECK( input.pastPathToolsEnabled );
    CHECK( input.scrubTrackDragEnabled );
    CHECK( input.branchTargetAvailable );
    CHECK( input.hotZoneEnabled );

    ReplayScrubberSurface surface;
    BuildReplayScrubberSurface( input, surface );
    CHECK( surface.controlCount == 13u );
    CHECK( surface.hasActiveControl );
    CHECK( surface.activeControl == ReplayScrubberControlId( ReplayScrubberControl::ScrubTrack ) );
    const ReplayOverlayControl* active = surface.Find( surface.activeControl );
    REQUIRE( active != nullptr );
    CHECK( active->active );

    const ReplayOverlayControl* highDetail = surface.Find( ReplayScrubberControlId( ReplayScrubberControl::HighDetail ) );
    REQUIRE( highDetail != nullptr );
    CHECK( highDetail->kind == ReplayOverlayControlKind::Toggle );
    CHECK( highDetail->checked );
    CHECK( highDetail->drawRect.x == doctest::Approx( ReplayScrubberHighDetailToggleRect( 1920, 1080 ).x ) );
    CHECK( highDetail->drawRect.y == doctest::Approx( ReplayScrubberHighDetailToggleRect( 1920, 1080 ).y ) );
    CHECK( highDetail->drawRect.w == doctest::Approx( ReplayScrubberHighDetailToggleRect( 1920, 1080 ).w ) );
    CHECK( highDetail->drawRect.h == doctest::Approx( ReplayScrubberHighDetailToggleRect( 1920, 1080 ).h ) );
    surface.ResolvePointer( RectCenterX( highDetail->hitRect ), RectCenterY( highDetail->hitRect ) );
    CHECK( surface.hasPointerControl );
    CHECK( surface.hasHotControl );
    CHECK( surface.hotControl == highDetail->id );
    CHECK( surface.Find( surface.hotControl )->action ==
           static_cast<uint32_t>( ReplayScrubberAction::SetPredictionDetailMode ) );

    input.predictionHighDetail = false;
    input.predictionEnabled = true;
    BuildReplayScrubberSurface( input, surface );
    highDetail = surface.Find( ReplayScrubberControlId( ReplayScrubberControl::HighDetail ) );
    REQUIRE( highDetail != nullptr );
    CHECK( highDetail->enabled );
    CHECK_FALSE( highDetail->checked );
    surface.ResolvePointer( RectCenterX( highDetail->hitRect ), RectCenterY( highDetail->hitRect ) );
    CHECK( surface.hasPointerControl );
    CHECK( surface.hasHotControl );
    CHECK( surface.consumesPointer );

    input.predictionEnabled = false;
    BuildReplayScrubberSurface( input, surface );
    highDetail = surface.Find( ReplayScrubberControlId( ReplayScrubberControl::HighDetail ) );
    REQUIRE( highDetail != nullptr );
    CHECK( highDetail->enabled );
    surface.ResolvePointer( RectCenterX( highDetail->hitRect ), RectCenterY( highDetail->hitRect ), true );
    CHECK_FALSE( surface.hasPointerControl );
    CHECK_FALSE( surface.hasHotControl );
    CHECK_FALSE( surface.consumesPointer );
}

TEST_CASE( "Replay overlay: loaded and unavailable surfaces block invalid actions" )
{
    ReplayScrubberView scrubber;
    scrubber.historicalSamplePaused = true;
    scrubber.activeTrack = RunReplayTrack::Presentation;
    ReplayRecorderStats stats;
    stats.enabled = true;
    stats.sampleCount = 1u;

    ReplayScrubberSurfaceInput loaded =
        DescribeReplayScrubberAvailability( scrubber, stats, { true, false, false, true, false, false } );
    loaded.hotZoneEnabled = false;
    CHECK( loaded.track == RunReplayTrack::Presentation );
    CHECK_FALSE( loaded.solverToolsEnabled );
    CHECK_FALSE( loaded.predictionToolsEnabled );
    CHECK( loaded.scrubTrackDragEnabled );
    CHECK( loaded.branchTargetAvailable );
    CHECK_FALSE( loaded.hotZoneEnabled );

    ReplayScrubberSurfaceInput livePast =
        DescribeReplayScrubberAvailability( scrubber, stats, { false, false, false, false, false, false } );
    CHECK( livePast.track == RunReplayTrack::Presentation );

    ReplayScrubberSurface loadedSurface;
    BuildReplayScrubberSurface( loaded, loadedSurface );
    const ReplayOverlayControl* highDetail = loadedSurface.Find( ReplayScrubberControlId( ReplayScrubberControl::HighDetail ) );
    REQUIRE( highDetail != nullptr );
    CHECK_FALSE( highDetail->visible );

    ReplayScrubberSurfaceInput unavailable =
        DescribeReplayScrubberAvailability( scrubber, stats, { false, false, false, false, false, false } );
    unavailable.screenW = 1280;
    unavailable.screenH = 720;
    CHECK_FALSE( unavailable.solverToolsEnabled );
    CHECK_FALSE( unavailable.scrubTrackDragEnabled );
    ReplayScrubberSurface unavailableSurface;
    BuildReplayScrubberSurface( unavailable, unavailableSurface );
    highDetail = unavailableSurface.Find( ReplayScrubberControlId( ReplayScrubberControl::HighDetail ) );
    REQUIRE( highDetail != nullptr );
    REQUIRE( highDetail->visible );
    CHECK_FALSE( highDetail->enabled );
    unavailableSurface.ResolvePointer( RectCenterX( highDetail->hitRect ), RectCenterY( highDetail->hitRect ) );
    CHECK( unavailableSurface.hasPointerControl );
    CHECK_FALSE( unavailableSurface.hasHotControl );
    CHECK( unavailableSurface.consumesPointer );
}

TEST_CASE( "Replay event commands: domain values encode bounded deterministic payloads" )
{
    using namespace ReplayEventCommandOperations;
    using SkullbonezCore::Math::Orientation::Quaternion;
    using SkullbonezCore::Math::Vector::Vector3;

    const ReplayEventCommand direct = BuildCommand( ReplayEventKind::OwnerAction, 17u, false, 3u, 1, 2, 3, 4, 99u,
                                                    "owner-action" );
    CHECK( direct.kind == ReplayEventKind::OwnerAction );
    CHECK( direct.frameIndex == 17u );
    CHECK_FALSE( direct.useNextFrame );
    CHECK( direct.flags == 3u );
    CHECK( direct.value3 == 4 );
    CHECK( direct.data0 == 99u );
    CHECK( std::strcmp( direct.text, "owner-action" ) == 0 );
    CHECK( BuildCommand( ReplayEventKind::Unknown, 0u, true, 0u, 0, 0, 0, 0, 0u, nullptr ).text[0] == '\0' );

    const ReplayEventCommand generated = BuildGeneratedSceneConfig( 5u, 100, 40, 60, 0x16u, 128, 2u );
    CHECK( generated.kind == ReplayEventKind::GeneratedSceneConfig );
    CHECK( generated.flags == 5u );
    CHECK( generated.value0 == 100 );
    CHECK( generated.value3 == 0x16 );
    CHECK( generated.data0 != 0u );
    CHECK( std::strcmp( generated.text, "generated_scene_config" ) == 0 );

    CHECK( BuildWorldOverride( -9.8f, 2.0f, 1.0f, -9.8f, 2.0f, 1.0f ).kind == ReplayEventKind::Unknown );
    const ReplayEventCommand world = BuildWorldOverride( -9.8f, 2.0f, 1.0f, -12.0f, 3.0f, 0.5f );
    CHECK( world.kind == ReplayEventKind::WorldOverride );
    CHECK( world.flags == 7u );
    CHECK( world.data0 != 0u );

    CHECK( BuildLauncherConfig( 0u, 10.0f, 20.0f ).kind == ReplayEventKind::Unknown );
    const ReplayEventCommand launcher = BuildLauncherConfig( 3u, 10.0f, 20.0f );
    CHECK( launcher.kind == ReplayEventKind::LauncherConfig );
    CHECK( launcher.flags == 3u );
    CHECK( std::strcmp( launcher.text, "launcher_config" ) == 0 );

    const ReplayEventCommand fire = BuildLauncherFire( Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 0.0f, 0.0f, 1.0f ),
                                                       Vector3( 0.0f, 1.0f, 0.0f ), true, 50.0f, 80.0f, 12 );
    CHECK( fire.kind == ReplayEventKind::LauncherFire );
    CHECK( fire.flags == 1u );
    CHECK( fire.value0 == 1 );
    CHECK( fire.value3 == 12 );
    CHECK( std::strncmp( fire.text, "ray9:", 5u ) == 0 );

    const ReplayEventCommand place = BuildEditorPlace( 4, true, true, 12, Vector3( 10.0f, 20.0f, 30.0f ),
                                                       Vector3( 1.0f, 2.0f, 3.0f ), 0.25f );
    CHECK( place.kind == ReplayEventKind::EditorPlace );
    CHECK( place.flags == 3u );
    CHECK( place.value0 == 4 );
    CHECK( std::strncmp( place.text, "place7:", 7u ) == 0 );

    const Quaternion orientation;
    const SkullbonezCore::Physics::PhysicsSceneObjectId sceneObjectId { 77u };
    CHECK( BuildEditorTransform( 2, 0u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, -1, 1.0f ).kind ==
           ReplayEventKind::Unknown );
    CHECK( BuildEditorTransform( 2, 4u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, 4, 2.0f ).kind ==
           ReplayEventKind::Unknown );
    const ReplayEventCommand translate = BuildEditorTransform( 2, 1u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ),
                                                               orientation, 8, 2, 5.0f );
    CHECK( translate.kind == ReplayEventKind::EditorTransform );
    CHECK( translate.flags == 1u );
    CHECK( translate.value3 == -1 );
    CHECK( std::strncmp( translate.text, "xform7:", 7u ) == 0 );
    const ReplayEventCommand scale = BuildEditorTransform( 2, 7u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8,
                                                           1, 1.25f );
    CHECK( scale.kind == ReplayEventKind::EditorTransform );
    CHECK( scale.flags == 7u );
    CHECK( scale.value3 == 1 );
    CHECK( std::strncmp( scale.text, "xform8:", 7u ) == 0 );
}

TEST_CASE( "Replay event recorder: chronological cursor survives bounded ring wrap" )
{
    ReplayRecorderConfig config;
    ReplayEventRecorder recorder;
    REQUIRE( recorder.Configure( config ) );
    CHECK_FALSE( recorder.IsEnabled() );
    ReplayEventInput ignored;
    recorder.RecordEvent( ignored );
    CHECK( recorder.GetStats().eventCount == 0u );
    recorder.ResetTimeline( "disabled" );

    config.enabled = true;
    config.retentionSeconds = 1;
    REQUIRE( recorder.Configure( config ) );
    CHECK( recorder.IsEnabled() );
    REQUIRE( recorder.GetStats().eventCapacity == 64u );

    for ( ReplayFrameIndex frame = 0u; frame < 66u; ++frame )
    {
        ReplayEventInput input;
        input.frameIndex = frame;
        input.branch.branchId = frame == 0u ? 0u : 7u;
        input.branch.parentBranchId = 2u;
        input.kind = ReplayEventKind::OwnerAction;
        input.flags = static_cast<uint32_t>( frame & 3u );
        input.value0 = static_cast<int32_t>( frame );
        input.value1 = -static_cast<int32_t>( frame );
        input.data0 = 0xA000u + frame;
        input.text = frame == 65u ? "last-event" : nullptr;
        recorder.RecordEvent( input );
    }

    const ReplayEventRecorderStats stats = recorder.GetStats();
    CHECK( stats.totalEventsCaptured == 66u );
    CHECK( stats.totalEventsEvicted == 2u );
    CHECK( stats.nextSequence == 66u );
    CHECK( stats.eventCount == 64u );
    CHECK( recorder.CollectMemoryBytes() > sizeof( recorder ) );

    SkullbonezCore::Core::MainMemoryReplayCategoryBytes categories;
    recorder.CollectMemoryCategoryBytes( categories );
    std::vector<ReplayEventSample> events;
    ReplayArtifactSource::MaterializeEvents( recorder, events );
    REQUIRE( events.size() == 64u );
    CHECK( events.front().frameIndex == 2u );
    CHECK( events.front().sequence == 2u );
    CHECK( events.front().branch.branchId == 7u );
    CHECK( events.back().frameIndex == 65u );
    CHECK( events.back().sequence == 65u );
    CHECK( events.back().value0 == 65 );
    CHECK( events.back().value1 == -65 );
    CHECK( events.back().data0 == 0xA041u );
    CHECK( std::strcmp( events.back().text, "last-event" ) == 0 );

    recorder.ResetTimeline( "new-scene" );
    CHECK( recorder.GetStats().eventCount == 0u );
    CHECK( recorder.GetStats().nextSequence == 0u );
    ReplayArtifactSource::MaterializeEvents( recorder, events );
    CHECK( events.empty() );
}

TEST_CASE( "Replay memory: one Low-detail snapshot reconciles released evidence" )
{
    SkullbonezCore::Core::MainMemoryReplayStats stats;
    SkullbonezCore::Core::
        MainMemoryAddReplayCategoryBytes( stats.categoryBytes,
                                          SkullbonezCore::Core::MainMemoryReplayByteCategory::PredictionOwner, 4096u );
    stats.totalBytes = SkullbonezCore::Core::MainMemoryReplayCategoryTotalBytes( stats.categoryBytes );
    stats.predictionEvidence.releaseCheckpointCount = 1u;
    stats.predictionEvidence.lastReleaseBeforeCapacityBytes = 1536u;
    stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes = stats.totalBytes + 1536u;
    stats.predictionEvidence.lastReleaseAfterReplayTotalBytes = stats.totalBytes;
    stats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes = stats.totalBytes + 1536u;
    stats.predictionEvidence.lastReleaseAfterCategoryTotalBytes = stats.totalBytes;

    CHECK( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );

    // Hazard: a stale total or category row must fail even when the release checkpoint
    // itself says that both banks reached zero.
    ++stats.totalBytes;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
    --stats.totalBytes;

    stats.predictionEvidence.currentCapacityBytes = 64u;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
    stats.predictionEvidence.currentCapacityBytes = 0u;

    ++stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
    --stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes;

    ++stats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
    --stats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes;

    // Hazard: subtraction is guarded before it can underflow, including an extreme
    // fabricated checkpoint that would have overflowed the retired add-back proof.
    stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes = 0u;
    stats.predictionEvidence.lastReleaseAfterReplayTotalBytes = UINT64_MAX;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
    stats.predictionEvidence.lastReleaseBeforeReplayTotalBytes = UINT64_MAX;
    stats.predictionEvidence.lastReleaseAfterReplayTotalBytes = 0u;
    stats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes = UINT64_MAX;
    stats.predictionEvidence.lastReleaseAfterCategoryTotalBytes = 0u;
    CHECK_FALSE( SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( stats ) );
}

TEST_CASE( "Replay overlay: cause-window packets clamp placement and scrolling" )
{
    RunReplayCauseTreeState state;
    state.rows.resize( 20u );
    state.x = -500;
    state.y = -500;
    state.width = 2000;
    state.height = 2000;
    state.scrollY = 1000.0f;

    ClampReplayCauseWindow( state, 800, 600 );
    CHECK( state.x == 8 );
    CHECK( state.y == 84 );
    CHECK( state.width == 784 );
    CHECK( state.height == 584 );
    CHECK( state.scrollY == doctest::Approx( ReplayCauseWindowMaxScroll( state ) ) );
    CHECK( ReplayCauseWindowContentHeight( state ) == doctest::Approx( 760.0f ) );

    const SkullbonezCore::UI::UIRect content = ReplayCauseWindowContentRect( state );
    const SkullbonezCore::UI::UIRect resize = ReplayCauseWindowResizeRect( state );
    CHECK( content.x > static_cast<float>( state.x ) );
    CHECK( resize.x + resize.w == doctest::Approx( static_cast<float>( state.x + state.width ) ) );
    CHECK( resize.y + resize.h == doctest::Approx( static_cast<float>( state.y + state.height ) ) );

    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( state, surface );
    CHECK( surface.controlCount == 9u );
    CHECK( surface.controls[0].id == ReplayCauseWindowControlId( ReplayCauseWindowControl::Resize ) );
    CHECK( surface.controls[1].id == ReplayCauseWindowControlId( ReplayCauseWindowControl::Title ) );
    CHECK( ReplayCauseWindowContainsPoint( state, state.x + 20, state.y + 60 ) );
    CHECK_FALSE( ReplayCauseWindowContainsPoint( state, state.x - 1, state.y + 60 ) );

    RunReplayCauseTreeState fresh;
    EnsureReplayCauseWindowPlacement( fresh, 1024, 768 );
    CHECK( fresh.hasWindowPlacement );
    CHECK( fresh.x == 620 );
    CHECK( fresh.y == 84 );
    CHECK( fresh.width == 380 );
    CHECK( fresh.height == 520 );
}

TEST_CASE( "Replay overlay: cause filtering preserves source ancestry and identity" )
{
    RunReplayCauseTreeState state;
    state.rows.resize( 6u );
    state.rows[0].kind = RunReplayCauseTreeRowKind::Body;
    state.rows[0].depth = 0;
    state.rows[0].prediction = true;
    strcpy_s( state.rows[0].name, "Root" );
    state.rows[1].kind = RunReplayCauseTreeRowKind::Body;
    state.rows[1].depth = 1;
    state.rows[1].prediction = true;
    strcpy_s( state.rows[1].name, "Alpha" );
    state.rows[2].kind = RunReplayCauseTreeRowKind::Manifold;
    state.rows[2].depth = 2;
    state.rows[2].prediction = true;
    strcpy_s( state.rows[2].name, "Crash contact" );
    state.rows[3].kind = RunReplayCauseTreeRowKind::SolverRow;
    state.rows[3].depth = 3;
    state.rows[3].prediction = true;
    strcpy_s( state.rows[3].detail, "friction clamp" );
    state.rows[4].kind = RunReplayCauseTreeRowKind::Body;
    state.rows[4].depth = 1;
    strcpy_s( state.rows[4].name, "Recorded Beta" );
    state.rows[5].kind = RunReplayCauseTreeRowKind::PredictionMotion;
    state.rows[5].depth = 2;
    state.rows[5].prediction = true;
    strcpy_s( state.rows[5].name, "Forecast arc" );

    ReplayCauseWindowProjection projection;
    BuildReplayCauseWindowProjection( state, projection );
    CHECK( projection.count == 6 );
    CHECK( projection.SourceRow( 3 ) == 3 );
    CHECK( projection.VisibleRow( 4 ) == 4 );

    state.filter = RunReplayCauseTreeFilter::Contacts;
    BuildReplayCauseWindowProjection( state, projection );
    REQUIRE( projection.count == 4 );
    CHECK( projection.SourceRow( 0 ) == 0 );
    CHECK( projection.SourceRow( 1 ) == 1 );
    CHECK( projection.SourceRow( 2 ) == 2 );
    CHECK( projection.SourceRow( 3 ) == 3 );

    state.filter = RunReplayCauseTreeFilter::All;
    strcpy_s( state.filterText, "CLAMP" );
    BuildReplayCauseWindowProjection( state, projection );
    REQUIRE( projection.count == 4 );
    CHECK( projection.SourceRow( 3 ) == 3 );

    state.filter = RunReplayCauseTreeFilter::Prediction;
    strcpy_s( state.filterText, "beta" );
    BuildReplayCauseWindowProjection( state, projection );
    CHECK( projection.count == 0 );
    CHECK( projection.SourceRow( 0 ) == -1 );

    state.filter = RunReplayCauseTreeFilter::All;
    BuildReplayCauseWindowProjection( state, projection );
    REQUIRE( projection.count == 2 );
    CHECK( projection.SourceRow( 0 ) == 0 );
    CHECK( projection.SourceRow( 1 ) == 4 );
    CHECK( projection.VisibleRow( 4 ) == 1 );

    RunReplayCauseTreeState textState;
    CHECK_FALSE( AppendReplayCauseFilterCharacter( textState, '\x01' ) );
    CHECK_FALSE( AppendReplayCauseFilterCharacter( textState, static_cast<char>( 0xE9 ) ) );

    for ( std::size_t index = 0; index < REPLAY_CAUSE_FILTER_TEXT_CAPACITY - 1u; ++index )
    {
        CHECK( AppendReplayCauseFilterCharacter( textState, 'x' ) );
    }

    CHECK_FALSE( AppendReplayCauseFilterCharacter( textState, 'y' ) );
    CHECK( strlen( textState.filterText ) == REPLAY_CAUSE_FILTER_TEXT_CAPACITY - 1u );
    CHECK( BackspaceReplayCauseFilter( textState ) );
    CHECK( strlen( textState.filterText ) == REPLAY_CAUSE_FILTER_TEXT_CAPACITY - 2u );
    CHECK( ClearReplayCauseFilterText( textState ) );
    CHECK( textState.filterText[0] == '\0' );
    CHECK_FALSE( ClearReplayCauseFilterText( textState ) );
}
