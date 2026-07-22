//
// File: SkullbonezTests/TestRuntimeValueSeams.cpp
// Purpose:
//   Lock CPU-only interaction-policy and replay-overlay value seams.
//
// Summary:
//   These tests exercise the frame-policy matrix and the screen-space control
//   packets shared by replay input and drawing. They deliberately avoid the
//   engine loop and renderer so failures identify owner logic directly.
//
// Glossary:
//   Frame policy: Value packet deciding physics advance, camera-look state, or
//     whether a cross-scene-locked frame may proceed.
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
//
// Related:
//   - SkullbonezSource/Runtime/RuntimeInteractionController.cpp
//   - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp
//   - SkullbonezSource/Runtime/UI/RuntimeUiSurface.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h"
#include "../SkullbonezSource/Runtime/RuntimeInteractionController.h"
#include "../SkullbonezSource/Runtime/Scene/SceneController.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayOverlay;

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
        pickup.body = SkullbonezCore::Physics::PhysicsBodyHandle{ 3u, 2u };
        REQUIRE(
            controller.BeginOwnedToolGesture( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, pickup ) );
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
    pickup.body = SkullbonezCore::Physics::PhysicsBodyHandle{ 4u, 1u };
    REQUIRE( controller.BeginOwnedToolGesture( RuntimeWorkspace::Live, WorldInteractionOwner::Manipulator, pickup ) );
    policy = controller.BuildFramePolicy( input );
    CHECK( policy.cameraLook == CameraLookState::Passive );
    CHECK_FALSE( policy.cameraMouseLookActive );
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
    CHECK( ReplayPredictionHorizonT( REPLAY_PREDICTION_MAX_SECONDS + 10.0f ) == 1.0f );
    const SkullbonezCore::UI::UIRect horizon{ 100.0f, 50.0f, 200.0f, 8.0f };
    CHECK( ReplayPredictionHorizonFromMouse( 0, horizon ) == REPLAY_PREDICTION_MIN_SECONDS );
    CHECK( ReplayPredictionHorizonFromMouse( 400, horizon ) == REPLAY_PREDICTION_MAX_SECONDS );
    const SkullbonezCore::UI::UIRect collapsedHorizon{ 100.0f, 50.0f, 1.0f, 8.0f };
    CHECK( ReplayPredictionHorizonFromMouse( 100, collapsedHorizon ) == REPLAY_PREDICTION_MAX_SECONDS );

    CHECK( ReplayScrubberPositionFromMouse( -100, 1920, 1080, RunReplayTrack::Solver ) == 0.0f );
    CHECK( ReplayScrubberPositionFromMouse( 4000, 1920, 1080, RunReplayTrack::Solver ) == 1.0f );
}

TEST_CASE( "Replay overlay: surface description publishes owner availability as values" )
{
    ReplayScrubberView scrubber;
    scrubber.historicalSamplePaused = true;
    scrubber.activeTrack = RunReplayTrack::Solver;
    ReplayRecorderStats stats;
    stats.enabled = true;
    stats.sampleCount = 2u;

    ReplayScrubberSurfaceDesc desc{ scrubber, stats };
    desc.pathTargetAvailable = true;
    desc.predictionTimelineAvailable = true;
    desc.currentSolverAvailable = true;
    desc.scenePhysicsEnabled = true;
    desc.screenW = 1920;
    desc.screenH = 1080;
    desc.gesture = RuntimeInteractionGestureKind::ReplayScrubDrag;

    const ReplayScrubberSurfaceInput input = DescribeReplayScrubberSurface( desc );
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
    const RuntimeUiControl* active = surface.Find( surface.activeControl );
    REQUIRE( active != nullptr );
    CHECK( active->active );

    const RuntimeUiControl* pause = surface.Find( ReplayScrubberControlId( ReplayScrubberControl::Pause ) );
    REQUIRE( pause != nullptr );
    surface.ResolvePointer( RectCenterX( pause->hitRect ), RectCenterY( pause->hitRect ) );
    CHECK( surface.hasPointerControl );
    CHECK( surface.hasHotControl );
    CHECK( surface.hotControl == pause->id );

    surface.ResolvePointer( RectCenterX( pause->hitRect ), RectCenterY( pause->hitRect ), true );
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

    ReplayScrubberSurfaceDesc loadedDesc{ scrubber, stats };
    loadedDesc.loadedPresentation = true;
    loadedDesc.currentPresentationAvailable = true;
    loadedDesc.uiBlocksMouse = true;
    const ReplayScrubberSurfaceInput loaded = DescribeReplayScrubberSurface( loadedDesc );
    CHECK( loaded.track == RunReplayTrack::Presentation );
    CHECK_FALSE( loaded.solverToolsEnabled );
    CHECK_FALSE( loaded.predictionToolsEnabled );
    CHECK( loaded.scrubTrackDragEnabled );
    CHECK( loaded.branchTargetAvailable );
    CHECK_FALSE( loaded.hotZoneEnabled );

    ReplayScrubberSurface loadedSurface;
    BuildReplayScrubberSurface( loaded, loadedSurface );
    const RuntimeUiControl* pause = loadedSurface.Find( ReplayScrubberControlId( ReplayScrubberControl::Pause ) );
    REQUIRE( pause != nullptr );
    CHECK_FALSE( pause->visible );

    ReplayScrubberSurfaceDesc unavailableDesc{ scrubber, stats };
    unavailableDesc.screenW = 1280;
    unavailableDesc.screenH = 720;
    const ReplayScrubberSurfaceInput unavailable = DescribeReplayScrubberSurface( unavailableDesc );
    CHECK_FALSE( unavailable.solverToolsEnabled );
    CHECK_FALSE( unavailable.scrubTrackDragEnabled );
    ReplayScrubberSurface unavailableSurface;
    BuildReplayScrubberSurface( unavailable, unavailableSurface );
    pause = unavailableSurface.Find( ReplayScrubberControlId( ReplayScrubberControl::Pause ) );
    REQUIRE( pause != nullptr );
    REQUIRE( pause->visible );
    CHECK_FALSE( pause->enabled );
    unavailableSurface.ResolvePointer( RectCenterX( pause->hitRect ), RectCenterY( pause->hitRect ) );
    CHECK( unavailableSurface.hasPointerControl );
    CHECK_FALSE( unavailableSurface.hasHotControl );
    CHECK( unavailableSurface.consumesPointer );
}

TEST_CASE( "Replay event commands: domain values encode bounded deterministic payloads" )
{
    using namespace ReplayEventCommandOperations;
    using SkullbonezCore::Math::Orientation::Quaternion;
    using SkullbonezCore::Math::Vector::Vector3;

    const ReplayEventCommand direct =
        BuildCommand( ReplayEventKind::OwnerAction, 17u, false, 3u, 1, 2, 3, 4, 99u, "owner-action" );
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

    const ReplayEventCommand fire = BuildLauncherFire( Vector3( 1.0f, 2.0f, 3.0f ),
                                                       Vector3( 0.0f, 0.0f, 1.0f ),
                                                       Vector3( 0.0f, 1.0f, 0.0f ),
                                                       true,
                                                       50.0f,
                                                       80.0f,
                                                       12 );
    CHECK( fire.kind == ReplayEventKind::LauncherFire );
    CHECK( fire.flags == 1u );
    CHECK( fire.value0 == 1 );
    CHECK( fire.value3 == 12 );
    CHECK( std::strncmp( fire.text, "ray9:", 5u ) == 0 );

    const ReplayEventCommand place =
        BuildEditorPlace( 4, true, true, 12, Vector3( 10.0f, 20.0f, 30.0f ), Vector3( 1.0f, 2.0f, 3.0f ), 0.25f );
    CHECK( place.kind == ReplayEventKind::EditorPlace );
    CHECK( place.flags == 3u );
    CHECK( place.value0 == 4 );
    CHECK( std::strncmp( place.text, "place7:", 7u ) == 0 );

    const Quaternion orientation;
    const SkullbonezCore::Physics::PhysicsSceneObjectId sceneObjectId{ 77u };
    CHECK( BuildEditorTransform( 2, 0u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, -1, 1.0f ).kind ==
           ReplayEventKind::Unknown );
    CHECK( BuildEditorTransform( 2, 4u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, 4, 2.0f ).kind ==
           ReplayEventKind::Unknown );
    const ReplayEventCommand translate =
        BuildEditorTransform( 2, 1u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, 2, 5.0f );
    CHECK( translate.kind == ReplayEventKind::EditorTransform );
    CHECK( translate.flags == 1u );
    CHECK( translate.value3 == -1 );
    CHECK( std::strncmp( translate.text, "xform7:", 7u ) == 0 );
    const ReplayEventCommand scale =
        BuildEditorTransform( 2, 7u, sceneObjectId, Vector3( 1.0f, 2.0f, 3.0f ), orientation, 8, 1, 1.25f );
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
    CHECK( state.y == 124 );
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
    CHECK( surface.controlCount == 4u );
    CHECK( surface.controls[0].id == ReplayCauseWindowControlId( ReplayCauseWindowControl::Resize ) );
    CHECK( surface.controls[1].id == ReplayCauseWindowControlId( ReplayCauseWindowControl::Title ) );

    RunReplayCauseTreeState fresh;
    EnsureReplayCauseWindowPlacement( fresh, 1024, 768 );
    CHECK( fresh.hasWindowPlacement );
    CHECK( fresh.x == 620 );
    CHECK( fresh.y == 124 );
    CHECK( fresh.width == 380 );
    CHECK( fresh.height == 520 );

    const SkullbonezCore::UI::UIRect tree = ReplayCauseTreePanelRect( 1024, 768 );
    CHECK( ReplayCauseTreeVisibleRowCapacity( tree ) > 0 );
    const SkullbonezCore::UI::UIRect firstRow = ReplayCauseTreeRowRect( tree, 0 );
    const SkullbonezCore::UI::UIRect secondRow = ReplayCauseTreeRowRect( tree, 1 );
    CHECK( secondRow.y - firstRow.y == doctest::Approx( REPLAY_CAUSE_TREE_ROW_HEIGHT ) );
}
