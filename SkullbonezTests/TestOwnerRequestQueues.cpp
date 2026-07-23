/*
File: TestOwnerRequestQueues.cpp
Purpose:
  Verifies fixed scene, capture, render-default, and operator-editor request contracts.

Summary:
  Each owner accepts only its domain intent, keeps FIFO order, and exposes a
  fixed capacity. Invalid bounded text or editor payloads are rejected before
  they consume storage, and duplicate frontend intent projects only once.

Glossary:
  FIFO (First In, First Out): Requests drain in submission order.
  Wire code: Explicit serialized replay value independent of C++ enum ordinals.
  Readback result: Recoverable capture owner/message returned by the renderer
    after it attempts to copy the backbuffer into CPU-visible bytes.
  Editor projection: Conversion of an arbitrated common action into the
    established narrow runtime-owner command packet.
  Layout envelope: Responsive pixel allocation that drives stable ImGui dock
    splits without requiring a vendor context in this test executable.
  Preference record: Fixed benign editor state that round-trips independently
    of authored scenes and resets stale layout/panel identity during migration.
  Lifecycle generation: Monotonic identity for one post-preflight scene-load
    attempt, including attempts that fail before activation.

Invariants:
  - Tests stop at the fixed capacity because the next runtime submission is a
    deliberate fatal owner-budget violation.
  - Replay owner codes are compatibility values and must not be renumbered.
  - Render-default saves reject config versions newer than the engine-owned
    schema before rewriting any bytes.
  - Shared editor views fingerprint semantic fields rather than object padding.
  - The versioned editor topology is identical at minimum, 16:9, and ultrawide sizes.
  - Compact causality reads only a bounded neighborhood of replay-owned rows and
    reports empty, stale, truncated, and capacity-limited states separately.
  - Preference migration may retain bounded filters but restores the current
    panel mask and topology fingerprint.
  - Clear and activation observers advance independently and consume each
    lifecycle generation at most once.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureController.h
  - SkullbonezSource/Runtime/Scene/SceneRequestQueue.h
  - SkullbonezSource/Runtime/Render/RenderDefaultsStore.h
  - SkullbonezSource/UI/OperatorEditorExchange.h
  - SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Capture/CaptureController.h"
#include "../SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h"
#include "../SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h"
#include "../SkullbonezSource/Runtime/Render/RenderDefaultsStore.h"
#include "../SkullbonezSource/Runtime/App/RunTimerState.h"
#include "../SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h"
#include "../SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/Runtime/Scene/SceneController.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRequestQueue.h"
#include "../SkullbonezSource/Runtime/Scene/SceneControllerState.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntime.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h"
#include "../SkullbonezSource/Physics/PhysicsDebugData.h"
#include "../SkullbonezSource/UI/UICommands.h"
#include "../SkullbonezSource/UI/UITabPhysics.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

using namespace SkullbonezCore::Runtime;

TEST_CASE( "Physics tab emits one typed request for every toggle row" )
{
    using namespace SkullbonezCore::UI;

    const auto emitToggle = []( int toggleIndex )
    {
        UIPhysicsCommands commands;
        CHECK( PhysicsTab::EmitPhysicsToggleCommand( toggleIndex, commands ) );
        return commands;
    };

    CHECK( emitToggle( 0 ).toggleCollisionVisualizer );
    CHECK( emitToggle( 1 ).physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::Axes );
    CHECK( emitToggle( 2 ).physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::Contacts );
    CHECK( emitToggle( 3 ).physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::Sleep );
    CHECK( emitToggle( 4 ).togglePhysicsDebugTransparent );
    CHECK( emitToggle( 5 ).toggleBroadphaseOverlay );
    CHECK( emitToggle( 6 ).togglePhysicsSleepPolicy );
    CHECK( emitToggle( 7 ).physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::Pipeline );
    CHECK( emitToggle( 8 ).toggleTerrainContactProbe );
    CHECK( emitToggle( 9 ).toggleTornado );
    CHECK( emitToggle( 10 ).toggleTornadoVisualShell );
    CHECK( emitToggle( 11 ).toggleTornadoFieldVectors );
    CHECK( emitToggle( 12 ).toggleRayCastVisualization );

    UIPhysicsCommands invalid;
    CHECK_FALSE( PhysicsTab::EmitPhysicsToggleCommand( -1, invalid ) );
    CHECK_FALSE( PhysicsTab::EmitPhysicsToggleCommand( 13, invalid ) );
}

TEST_CASE( "Runtime applies Physics-tab diagnostics and publishes matching detached status" )
{
    using namespace SkullbonezCore::Physics;
    using namespace SkullbonezCore::UI;

    OverlayDebugState debug;
    UIPhysicsCommands commands;

    commands.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Axes;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).toggledPhysicsDebugFlags );
    UIPhysicsDebugStatus status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.axes );
    CHECK( status.activeFlags == PHYSICS_DEBUG_AXES );

    commands = {};
    commands.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Contacts;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).toggledPhysicsDebugFlags );
    commands.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Sleep;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).toggledPhysicsDebugFlags );
    commands.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Pipeline;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).toggledPhysicsDebugFlags );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.axes );
    CHECK( status.contacts );
    CHECK( status.sleep );
    CHECK( status.pipeline );

    commands = {};
    commands.toggleCollisionVisualizer = true;
    commands.togglePhysicsDebugTransparent = true;
    commands.toggleBroadphaseOverlay = true;
    const DiagnosticsPhysicsOverlayUICommandResult overlayResult =
        ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands );
    CHECK( overlayResult.toggledCollisionVisualizer );
    CHECK( overlayResult.toggledPhysicsDebugTransparent );
    CHECK( overlayResult.toggledBroadphaseOverlay );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.collisionVisualizer );
    CHECK( status.transparent );
    CHECK( status.broadphase );

    commands = {};
    commands.toggleTerrainContactProbe = true;
    CHECK( ApplyDiagnosticsTerrainContactProbeUICommand( debug, commands ) );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.terrainContact );

    commands = {};
    commands.stepPhysicsPipelinePrevious = true;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).steppedPipelinePrevious );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.pipeline );
    CHECK( status.pipelineStageCount == static_cast<int>( PhysicsPipelineStage::Count ) );
    CHECK( status.pipelineStageIndex == status.pipelineStageCount - 1 );
    CHECK( status.pipelineStageName[0] != '\0' );

    commands = {};
    commands.requestedPhysicsDebugAlpha = 2.0f;
    commands.requestedPhysicsDebugContactLinger = 8.0f;
    const DiagnosticsPhysicsDebugValueUICommandResult valueResult =
        ApplyDiagnosticsPhysicsDebugValueUICommands( debug, commands );
    CHECK( valueResult.setAlpha );
    CHECK( valueResult.setContactLinger );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK( status.alpha == doctest::Approx( 1.0f ) );
    CHECK( status.contactLinger == doctest::Approx( 5.0f ) );

    commands = {};
    commands.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Axes;
    CHECK( ApplyDiagnosticsPhysicsOverlayUICommands( debug, commands ).toggledPhysicsDebugFlags );
    status = BuildDiagnosticsPhysicsUIStatus( debug );
    CHECK_FALSE( status.axes );
    CHECK( status.contacts );
}

TEST_CASE( "Scene lifecycle accepts only ordered phases within one generation" )
{
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::None,
                                                 SceneRuntimeLifecycleEvent::BeforeSceneUnload ) );
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeSceneUnload,
                                                 SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterSceneCleared,
                                                 SceneRuntimeLifecycleEvent::BeforeScenePopulate ) );
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeScenePopulate,
                                                 SceneRuntimeLifecycleEvent::AfterScenePopulate ) );
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterScenePopulate,
                                                 SceneRuntimeLifecycleEvent::AfterSceneActivated ) );

    // A retry must open a new generation and reset the previous event to None;
    // it cannot restart or skip inside an existing generation.
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeScenePopulate,
                                                       SceneRuntimeLifecycleEvent::BeforeSceneUnload ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeSceneUnload,
                                                       SceneRuntimeLifecycleEvent::BeforeScenePopulate ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterSceneCleared,
                                                       SceneRuntimeLifecycleEvent::AfterSceneActivated ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterSceneActivated,
                                                       SceneRuntimeLifecycleEvent::None ) );

    const SceneLifecycleConsumerMask beforeUnload = SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics ) |
                                                    SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDrain );
    const SceneLifecycleConsumerMask afterClear = SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeSceneUnload ) == beforeUnload );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneCleared ) == afterClear );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeScenePopulate ) == 0 );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterScenePopulate ) == 0 );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneActivated ) == 0 );
}

TEST_CASE( "Scene lifecycle generations publish failures and repeated scene loads exactly once" )
{
    SceneRuntime scene( std::vector<std::string>{ "alpha.scene.json" } );
    SceneLifecycleGenerationObserver clearObserver;
    SceneLifecycleGenerationObserver activationObserver;

    // A failure before BeginLoad crossed no mutation boundary and publishes no
    // generation for reactive owners to consume.
    CHECK( scene.LifecyclePacket().generation == 0 );
    CHECK_FALSE( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );

    const SceneLifecycleBeginPolicy policy{ true, false, true, true, true };
    scene.BeginLoadAttempt( 0, policy );
    scene.BeginLoad( 0 );
    CHECK( scene.LifecyclePacket().generation == 1 );
    CHECK( scene.LifecyclePacket().event == SceneRuntimeLifecycleEvent::None );
    CHECK( scene.LifecyclePacket().sceneIndex == 0 );
    CHECK( scene.LifecyclePacket().policy.preserveUiState );
    CHECK( scene.LifecyclePacket().policy.suppressExitOnComplete );
    CHECK( scene.LifecyclePacket().policy.enterInteractiveRun );
    CHECK( scene.LifecyclePacket().policy.manualReset );

    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload,
                                SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeSceneUnload ) );
    CHECK_FALSE( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared,
                                SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK_FALSE( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK_FALSE(
        activationObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneActivated ) );

    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeScenePopulate, 0 );
    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterScenePopulate, 0 );
    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneActivated, 0 );
    CHECK_FALSE( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK( activationObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneActivated ) );
    CHECK_FALSE(
        activationObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneActivated ) );

    // Same index and unchanged entity count are still a distinct load attempt.
    scene.BeginLoadAttempt( 0, {} );
    scene.BeginLoad( 0 );
    CHECK( scene.LifecyclePacket().generation == 2 );
    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::BeforeSceneUnload,
                                SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeSceneUnload ) );
    scene.RecordLifecycleEvent( SceneRuntimeLifecycleEvent::AfterSceneCleared,
                                SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK( clearObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneCleared ) );
    CHECK_FALSE(
        activationObserver.ShouldApply( scene.LifecyclePacket(), SceneRuntimeLifecycleEvent::AfterSceneActivated ) );
    CHECK( clearObserver.LastAppliedGeneration() == 2 );
}

TEST_CASE( "Run timers consume reset and activation once per lifecycle generation" )
{
    RunTimerSceneLifecyclePolicy timerPolicy;

    SceneLifecyclePacket packet;
    packet.generation = 1;
    packet.event = SceneRuntimeLifecycleEvent::BeforeSceneUnload;
    RunTimerSceneLifecycleActions actions = timerPolicy.Observe( packet );
    CHECK_FALSE( actions.resetMeasurements );
    CHECK_FALSE( actions.restartClocks );

    packet.event = SceneRuntimeLifecycleEvent::AfterSceneCleared;
    actions = timerPolicy.Observe( packet );
    CHECK( actions.resetMeasurements );
    CHECK_FALSE( actions.restartClocks );
    CHECK( timerPolicy.LastResetGeneration() == 1 );
    CHECK( timerPolicy.LastActivationGeneration() == 0 );

    actions = timerPolicy.Observe( packet );
    CHECK_FALSE( actions.resetMeasurements );
    CHECK_FALSE( actions.restartClocks );

    packet.event = SceneRuntimeLifecycleEvent::AfterSceneActivated;
    actions = timerPolicy.Observe( packet );
    CHECK_FALSE( actions.resetMeasurements );
    CHECK( actions.restartClocks );
    CHECK( timerPolicy.LastActivationGeneration() == 1 );
    packet.generation = 2;
    actions = timerPolicy.Observe( packet );
    CHECK( actions.resetMeasurements );
    CHECK( actions.restartClocks );
    CHECK( timerPolicy.LastResetGeneration() == 2 );
    CHECK( timerPolicy.LastActivationGeneration() == 2 );
}

TEST_CASE( "Scene batch followers prefer presentation values emitted by a completed clear" )
{
    OverlayDebugState submitted;
    submitted.physicsDebugAlpha = 0.25f;
    SceneLoadConsumerOutputs outputs;
    outputs.presentation.physicsDebugAlpha = 0.75f;
    SceneLifecyclePacket lifecycle;

    CHECK( ScenePresentationForFollowingRequest( submitted, outputs, lifecycle ).physicsDebugAlpha ==
           doctest::Approx( 0.25f ) );
    lifecycle.generation = 1;
    lifecycle.event = SceneRuntimeLifecycleEvent::AfterSceneCleared;
    CHECK( ScenePresentationForFollowingRequest( submitted, outputs, lifecycle ).physicsDebugAlpha ==
           doctest::Approx( 0.75f ) );
}

TEST_CASE( "Scene navigation returns value-only accepted load decisions" )
{
    const SceneLoadRequest none = SceneLoadRequest::None();
    CHECK_FALSE( none.accepted );
    CHECK_FALSE( none.HasLoad() );

    const SceneLoadRequest current = SceneLoadRequest::AcceptedWithoutLoad( true );
    CHECK( current.accepted );
    CHECK( current.enterInteractiveSceneRun );
    CHECK_FALSE( current.HasLoad() );

    const SceneLoadRequest load = SceneLoadRequest::Load( 7, true, false, true, true );
    CHECK( load.accepted );
    CHECK( load.HasLoad() );
    CHECK( load.index == 7 );
    CHECK( load.preserveUIState );
    CHECK_FALSE( load.suppressExitOnComplete );
    CHECK( load.preserveRuntimeState );
    CHECK( load.enterInteractiveSceneRun );
    CHECK_FALSE( load.markManualReset );

    CHECK_FALSE( SceneLoadRequest::Load( -1, true, true, true ).accepted );
}

TEST_CASE( "UI scene navigation owns browser queue and demo decisions" )
{
    SkullbonezCore::UI::SceneNavigationModel navigation;
    navigation.browser.paths = { "SkullbonezData\\scenes\\alpha.scene.json", "SkullbonezData/scenes/beta.scene.json" };
    SceneRuntime scene( std::vector<std::string>{ "SkullbonezData/scenes/alpha.scene.json" } );
    scene.BeginLoad( 0 );

    const SceneLoadRequest current = LoadSceneFromBrowserIndex( navigation, 0, scene );
    CHECK( current.accepted );
    CHECK_FALSE( current.HasLoad() );
    CHECK( current.enterInteractiveSceneRun );

    const SceneLoadRequest appended = LoadSceneFromBrowserIndex( navigation, 1, scene );
    CHECK( appended.HasLoad() );
    CHECK( appended.index == 1 );
    CHECK( scene.PathAt( 1 ) == "SkullbonezData/scenes/beta.scene.json" );
    CHECK_FALSE( LoadSceneFromBrowserIndex( navigation, -1, scene ).accepted );

    const SceneLoadRequest demo = LoadDemoScene( scene );
    CHECK( demo.HasLoad() );
    CHECK( demo.index == 2 );
    CHECK( scene.PathAt( 2 ).empty() );
    CHECK( LoadDemoScene( scene ).index == 2 );
}

TEST_CASE( "Scene load navigation snapshot is detached from the UI owner" )
{
    SkullbonezCore::UI::SceneNavigationModel navigation;
    navigation.browser.paths = { "alpha.scene.json", "concept_beta.scene.json" };
    navigation.browser.selectedCineModeSceneIndex = 1;
    navigation.overrides.timeScaleOverride = 0.5f;
    navigation.overrides.modelCountOverride = 24;

    SceneLoadNavigationState loadNavigation = CaptureSceneLoadNavigationState( navigation );
    navigation.browser.paths[1] = "mutated.scene.json";
    navigation.browser.selectedCineModeSceneIndex = -1;
    navigation.overrides.timeScaleOverride = 2.0f;

    CHECK( loadNavigation.browserPaths[1] == "concept_beta.scene.json" );
    CHECK( loadNavigation.selectedCineModeSceneIndex == 1 );
    CHECK( loadNavigation.overrides.timeScaleOverride == doctest::Approx( 0.5f ) );
    CHECK( loadNavigation.overrides.modelCountOverride == 24 );

    SceneRuntime scene( std::vector<std::string>{ "alpha.scene.json" } );
    scene.BeginLoad( 0 );
    const SceneLoadRequest request = loadNavigation.LoadSceneFromBrowserIndex( 1, scene );
    CHECK( request.HasLoad() );
    CHECK( scene.PathAt( request.index ) == "concept_beta.scene.json" );

    loadNavigation.selectedCineModeSceneIndex = 0;
    loadNavigation.overrides.timeScaleOverride = 0.75f;
    ApplySceneLoadNavigationState( navigation, loadNavigation );
    CHECK( navigation.browser.paths[1] == "mutated.scene.json" );
    CHECK( navigation.browser.selectedCineModeSceneIndex == 0 );
    CHECK( navigation.overrides.timeScaleOverride == doctest::Approx( 0.75f ) );
}

TEST_CASE( "UI scene navigation cycles cinematic browser rows" )
{
    SkullbonezCore::UI::SceneNavigationModel navigation;
    navigation.browser.paths = { "ordinary.scene.json",
                                 "concept_one.scene.json",
                                 "ordinary_two.scene.json",
                                 "cinematic_two.scene.json" };
    navigation.browser.selectedCineModeSceneIndex = 1;
    SceneRuntime scene( std::vector<std::string>{ "ordinary.scene.json" } );
    scene.BeginLoad( 0 );

    CHECK( AdjacentCinematicModeBrowserIndex( navigation, 1, 0, false ) == 3 );
    CHECK( AdjacentCinematicModeBrowserIndex( navigation, -1, 0, false ) == 3 );
    CHECK( AdjacentCinematicModeBrowserIndex( navigation, 0, 0, true ) == -1 );

    const SceneLoadRequest adjacent = LoadAdjacentScene( navigation, 1, 1, scene );
    CHECK( adjacent.HasLoad() );
    CHECK( scene.PathAt( adjacent.index ) == "cinematic_two.scene.json" );
}

TEST_CASE( "CaptureController rejects truncating paths before enqueue" )
{
    CaptureController capture;

    CHECK( capture.QueueScreenshot( "Screenshots\\owner_request.bmp" ).ok );
    CHECK( capture.PendingScreenshotCount() == 1 );

    const SkullbonezCore::Core::SbResult empty = capture.QueueScreenshot( "" );
    CHECK_FALSE( empty.ok );
    CHECK( capture.PendingScreenshotCount() == 1 );

    char oversized[CAPTURE_REQUEST_PATH_CAPACITY + 1] = {};
    std::memset( oversized, 'a', sizeof( oversized ) - 1 );
    oversized[sizeof( oversized ) - 1] = '\0';
    const SkullbonezCore::Core::SbResult tooLong = capture.QueueScreenshot( oversized );
    CHECK_FALSE( tooLong.ok );
    CHECK( capture.PendingScreenshotCount() == 1 );
}

TEST_CASE( "CaptureController predicts scene captures before rendering" )
{
    CaptureController capture;
    RunScreenshotState& screenshot = capture.Screenshot();
    strcpy_s( screenshot.screenshotPath, "Profile/capture_pin.bmp" );
    screenshot.screenshotFrame = 10;

    RuntimeCaptureSceneContext context;
    context.isSceneMode = true;
    context.currentFrame = 8;
    CHECK_FALSE( capture.IsScreenshotDue( context ) );
    CHECK( capture.RequiresDeterministicPresentation( context ) );
    context.currentFrame = 9;
    CHECK( capture.IsScreenshotDue( context ) );

    // A millisecond threshold can cross while the frame is rendering. The
    // deterministic decision therefore pins the pending one-shot before due.
    screenshot.screenshotFrame = -1;
    screenshot.screenshotMs = 100;
    context.elapsedMs = 99.0;
    CHECK_FALSE( capture.IsScreenshotDue( context ) );
    CHECK( capture.RequiresDeterministicPresentation( context ) );
    context.elapsedMs = 101.0;
    CHECK( capture.IsScreenshotDue( context ) );

    screenshot.screenshotMs = -1;
    screenshot.screenshotPath[0] = '\0';
    screenshot.screenshotInterval = 3;
    strcpy_s( screenshot.screenshotDir, "TestOutput/capture_pin" );
    context.currentFrame = 1;
    CHECK_FALSE( capture.IsScreenshotDue( context ) );
    context.currentFrame = 2;
    CHECK( capture.IsScreenshotDue( context ) );
}

TEST_CASE( "CaptureController owns a fixed request budget" )
{
    CaptureController capture;
    for ( int index = 0; index < CAPTURE_REQUEST_QUEUE_CAPACITY; ++index )
    {
        REQUIRE( capture.QueueScreenshot( "Screenshots\\fixed_budget.bmp" ).ok );
    }
    CHECK( capture.PendingScreenshotCount() == CAPTURE_REQUEST_QUEUE_CAPACITY );
}

TEST_CASE( "Capture request batches return only successful requests as accepted events" )
{
    CaptureRequest request;
    strcpy_s( request.path, "Screenshots\\unsupported.bmp" );
    CaptureRequestBatchResult result;
    AccumulateCaptureRequestResult(
        result,
        request,
        SkullbonezCore::Core::SbResult::Failure( "Runtime/CaptureSystem", "capture unsupported" ) );
    CHECK_FALSE( result.status.ok );
    CHECK( result.savedCount == 0 );
    CHECK( result.failedCount == 1 );
}

TEST_CASE( "Capture request batches preserve concrete readback failure ownership" )
{
    CaptureRequest request;
    strcpy_s( request.path, "Screenshots\\readback_failure.bmp" );
    CaptureRequestBatchResult result;
    AccumulateCaptureRequestResult( result,
                                    request,
                                    SkullbonezCore::Core::SbResult::Failure( "Test/Readback", "fence wait failed" ) );
    CHECK_FALSE( result.status.ok );
    CHECK_EQ( std::strcmp( result.status.error.owner, "Test/Readback" ), 0 );
    CHECK_EQ( std::strcmp( result.status.error.message, "fence wait failed" ), 0 );
    CHECK( result.savedCount == 0 );
    CHECK( result.failedCount == 1 );
}

TEST_CASE( "SceneRequestQueue preserves domain order and rejects unbounded create text" )
{
    SceneRequestQueue queue;
    SceneRequest reset;
    reset.type = SceneRequestType::ResetCurrentScene;
    reset.preserveUIState = false;
    reset.preserveRuntimeState = false;
    REQUIRE( queue.Submit( reset ).ok );

    SceneRequest save;
    save.type = SceneRequestType::SaveCurrentDefaults;
    REQUIRE( queue.Submit( save ).ok );

    SceneRequest invalidCreate;
    invalidCreate.type = SceneRequestType::CreateScene;
    std::memset( invalidCreate.text, 'x', sizeof( invalidCreate.text ) );
    CHECK_FALSE( queue.Submit( invalidCreate ).ok );
    CHECK( queue.Size() == 2 );

    const SceneRequestBatch batch = queue.TakePending();
    REQUIRE( batch.count == 2 );
    CHECK( batch.requests[0].type == SceneRequestType::ResetCurrentScene );
    CHECK_FALSE( batch.requests[0].preserveUIState );
    CHECK_FALSE( batch.requests[0].preserveRuntimeState );
    CHECK( batch.requests[1].type == SceneRequestType::SaveCurrentDefaults );
    CHECK( queue.Size() == 0 );
}

TEST_CASE( "SceneRequestQueue accepts at most one transition per checkpoint" )
{
    SceneRequestQueue queue;
    SceneRequest load;
    load.type = SceneRequestType::LoadBrowserIndex;
    load.index = 3;
    REQUIRE( queue.Submit( load ).ok );

    SceneRequest save;
    save.type = SceneRequestType::SaveCurrentDefaults;
    REQUIRE( queue.Submit( save ).ok );

    SceneRequest reset;
    reset.type = SceneRequestType::ResetCurrentScene;
    REQUIRE( queue.Submit( reset ).ok );

    const SceneRequestBatch batch = queue.TakePending();
    REQUIRE( batch.count == 2 );
    CHECK( batch.requests[0].type == SceneRequestType::LoadBrowserIndex );
    CHECK( batch.requests[1].type == SceneRequestType::SaveCurrentDefaults );
    CHECK( batch.rejectedTransitionCount == 1 );
}

TEST_CASE( "Scene request batches stop after a failed transition" )
{
    CHECK( SceneRequestBatchContinuesAfter( SceneRequestType::LoadBrowserIndex, true ) );
    CHECK_FALSE( SceneRequestBatchContinuesAfter( SceneRequestType::LoadBrowserIndex, false ) );
    CHECK_FALSE( SceneRequestBatchContinuesAfter( SceneRequestType::CreateScene, false ) );
    CHECK( SceneRequestBatchContinuesAfter( SceneRequestType::SaveCurrentDefaults, false ) );
}

TEST_CASE( "Scene request execution saves navigation committed by an earlier load" )
{
    SceneLoadNavigationState submitted;
    submitted.overrides.timeScaleOverride = 2.0f;
    submitted.overrides.modelCountOverride = 80;

    SceneLoadConsumerOutputs outputs;
    outputs.navigation.overrides.timeScaleOverride = 0.5f;
    outputs.navigation.overrides.modelCountOverride = 24;
    CHECK( &SceneNavigationForFollowingRequest( submitted, outputs ) == &submitted );

    outputs.applyNavigation = true;
    const SceneLoadNavigationState& committed = SceneNavigationForFollowingRequest( submitted, outputs );
    CHECK( &committed == &outputs.navigation );
    CHECK( committed.overrides.timeScaleOverride == doctest::Approx( 0.5f ) );
    CHECK( committed.overrides.modelCountOverride == 24 );
}

TEST_CASE( "RenderDefaultsStore preserves interleaved save intent without value snapshots" )
{
    RenderDefaultsStore store;
    store.SubmitCinematicSave();
    store.SubmitOrdinarySave();
    store.SubmitCinematicSave();

    REQUIRE( store.PendingCount() == 3 );
    CHECK( store.PendingTypeAt( 0 ) == RenderDefaultsRequestType::Cinematic );
    CHECK( store.PendingTypeAt( 1 ) == RenderDefaultsRequestType::Ordinary );
    CHECK( store.PendingTypeAt( 2 ) == RenderDefaultsRequestType::Cinematic );
}

TEST_CASE( "RenderDefaultsStore excludes failed writes from accepted events" )
{
    namespace fs = std::filesystem;
    std::error_code filesystemError;
    const fs::path originalPath = fs::current_path( filesystemError );
    REQUIRE_FALSE( filesystemError );
    const fs::path emptyRoot = originalPath / "TestOutput" / "owner_request_missing_config";
    fs::create_directories( emptyRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    fs::current_path( emptyRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );

    RenderDefaultsStore store;
    store.SubmitOrdinarySave();
    const RenderDefaultsSaveBatchResult result =
        store.DrainAtFrameCheckpoint( SkullbonezCore::Core::OrdinaryRenderConfig{},
                                      SkullbonezCore::Core::CinematicRenderConfig{} );

    fs::current_path( originalPath, filesystemError );
    CHECK_FALSE( filesystemError );
    fs::remove_all( emptyRoot, filesystemError );
    CHECK_FALSE( filesystemError );
    CHECK_FALSE( result.status.ok );
    CHECK( result.savedCount == 0 );
    CHECK( result.failedCount == 1 );
}

TEST_CASE( "RenderDefaultsStore samples values at the drain checkpoint" )
{
    namespace fs = std::filesystem;
    std::error_code filesystemError;
    const fs::path originalPath = fs::current_path( filesystemError );
    REQUIRE_FALSE( filesystemError );
    const fs::path testRoot = originalPath / "TestOutput" / "owner_request_final_values";
    const fs::path dataRoot = testRoot / "SkullbonezData";
    fs::create_directories( dataRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    {
        std::ofstream configFile( dataRoot / "engine.cfg", std::ios::trunc );
        REQUIRE( configFile.is_open() );
        configFile << "# Ordinary rendering\nordinary_sun_intensity = 1.00\n";
        REQUIRE( configFile.good() );
    }

    RenderDefaultsStore store;
    SkullbonezCore::Core::OrdinaryRenderConfig ordinary;
    SkullbonezCore::Core::CinematicRenderConfig cinematic;
    store.SubmitOrdinarySave();
    ordinary.sunIntensity = 9.25f; // Final UI mutation after submission, before checkpoint.

    fs::current_path( testRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    const RenderDefaultsSaveBatchResult result = store.DrainAtFrameCheckpoint( ordinary, cinematic );
    fs::current_path( originalPath, filesystemError );
    CHECK_FALSE( filesystemError );

    std::string configText;
    {
        std::ifstream configFile( dataRoot / "engine.cfg" );
        configText.assign( std::istreambuf_iterator<char>( configFile ), std::istreambuf_iterator<char>() );
    }
    CHECK( result.status.ok );
    CHECK( result.savedCount == 1 );
    CHECK( configText.find( "ordinary_sun_intensity = 9.25" ) != std::string::npos );
    CHECK( configText.find( "format_version = 6" ) != std::string::npos );

    store.SubmitOrdinarySave();
    fs::current_path( testRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    const RenderDefaultsSaveBatchResult repeated = store.DrainAtFrameCheckpoint( ordinary, cinematic );
    fs::current_path( originalPath, filesystemError );
    REQUIRE_FALSE( filesystemError );
    std::string repeatedText;
    {
        std::ifstream repeatedFile( dataRoot / "engine.cfg" );
        repeatedText.assign( std::istreambuf_iterator<char>( repeatedFile ), std::istreambuf_iterator<char>() );
    }
    CHECK( repeated.status.ok );
    CHECK( repeatedText == configText );

    fs::remove_all( testRoot, filesystemError );
    CHECK_FALSE( filesystemError );
}

TEST_CASE( "RenderDefaultsStore legacy writers remove retired config rows" )
{
    namespace fs = std::filesystem;
    std::error_code filesystemError;
    const fs::path originalPath = fs::current_path( filesystemError );
    REQUIRE_FALSE( filesystemError );

    const auto verifyWriter = [&]( bool cinematicSave, const char* fixtureName )
    {
        const fs::path testRoot = originalPath / "TestOutput" / fixtureName;
        const fs::path dataRoot = testRoot / "SkullbonezData";
        fs::create_directories( dataRoot, filesystemError );
        REQUIRE_FALSE( filesystemError );
        {
            std::ofstream configFile( dataRoot / "engine.cfg", std::ios::trunc );
            REQUIRE( configFile.is_open() );
            configFile << "format_version = 3\n"
                          "physics_simd_kernels = 1\n"
                          "contact_audio_master_gain = 0.5\n"
                          "terrain_render_step_size = 1\n"
                          "owner_unknown_key = retain-me\n";
            REQUIRE( configFile.good() );
        }

        RenderDefaultsStore store;
        if ( cinematicSave )
        {
            store.SubmitCinematicSave();
        }
        else
        {
            store.SubmitOrdinarySave();
        }

        fs::current_path( testRoot, filesystemError );
        REQUIRE_FALSE( filesystemError );
        const RenderDefaultsSaveBatchResult result =
            store.DrainAtFrameCheckpoint( SkullbonezCore::Core::OrdinaryRenderConfig{},
                                          SkullbonezCore::Core::CinematicRenderConfig{} );
        fs::current_path( originalPath, filesystemError );
        REQUIRE_FALSE( filesystemError );

        std::string configText;
        {
            std::ifstream configFile( dataRoot / "engine.cfg" );
            REQUIRE( configFile.is_open() );
            configText.assign( std::istreambuf_iterator<char>( configFile ), std::istreambuf_iterator<char>() );
        }
        CHECK( result.status.ok );
        CHECK( result.savedCount == 1 );
        CHECK( configText.find( "format_version = 6" ) != std::string::npos );
        CHECK( configText.find( "physics_simd_kernels" ) == std::string::npos );
        CHECK( configText.find( "contact_audio_" ) == std::string::npos );
        CHECK( configText.find( "terrain_render_step_size" ) == std::string::npos );
        CHECK( configText.find( "owner_unknown_key = retain-me" ) != std::string::npos );

        fs::remove_all( testRoot, filesystemError );
        REQUIRE_FALSE( filesystemError );
    };

    verifyWriter( false, "owner_request_v3_ordinary_writer" );
    verifyWriter( true, "owner_request_v3_cinematic_writer" );
}

TEST_CASE( "RenderDefaultsStore rejects future config without rewriting bytes" )
{
    namespace fs = std::filesystem;
    std::error_code filesystemError;
    const fs::path originalPath = fs::current_path( filesystemError );
    REQUIRE_FALSE( filesystemError );
    const fs::path testRoot = originalPath / "TestOutput" / "owner_request_future_config";
    const fs::path dataRoot = testRoot / "SkullbonezData";
    fs::create_directories( dataRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    const std::string originalText = "format_version = 7\nordinary_sun_intensity = 1.00\n";
    {
        std::ofstream configFile( dataRoot / "engine.cfg", std::ios::trunc );
        REQUIRE( configFile.is_open() );
        configFile << originalText;
        REQUIRE( configFile.good() );
    }

    RenderDefaultsStore store;
    store.SubmitOrdinarySave();
    fs::current_path( testRoot, filesystemError );
    REQUIRE_FALSE( filesystemError );
    const RenderDefaultsSaveBatchResult result =
        store.DrainAtFrameCheckpoint( SkullbonezCore::Core::OrdinaryRenderConfig{},
                                      SkullbonezCore::Core::CinematicRenderConfig{} );
    fs::current_path( originalPath, filesystemError );
    REQUIRE_FALSE( filesystemError );

    std::string finalText;
    {
        std::ifstream configFile( dataRoot / "engine.cfg" );
        finalText.assign( std::istreambuf_iterator<char>( configFile ), std::istreambuf_iterator<char>() );
    }
    CHECK_FALSE( result.status.ok );
    CHECK( result.savedCount == 0 );
    CHECK( result.failedCount == 1 );
    CHECK( finalText == originalText );

    fs::remove_all( testRoot, filesystemError );
    CHECK_FALSE( filesystemError );
}

TEST_CASE( "Replay owner event codes are explicit compatibility values" )
{
    CHECK( static_cast<uint16_t>( ReplayEventKind::OwnerAction ) == 10 );
    CHECK( static_cast<int32_t>( ReplayOwnerEventCode::SceneLoadBrowserIndex ) == 1001 );
    CHECK( static_cast<int32_t>( ReplayOwnerEventCode::SceneSaveDefaults ) == 1005 );
    CHECK( static_cast<int32_t>( ReplayOwnerEventCode::CaptureScreenshot ) == 2001 );
    CHECK( static_cast<int32_t>( ReplayOwnerEventCode::RenderSaveOrdinaryDefaults ) == 3001 );
    CHECK( static_cast<int32_t>( ReplayOwnerEventCode::RenderSaveCinematicDefaults ) == 3002 );
}

TEST_CASE( "Operator editor queues coalesce identical frontend intent before projection" )
{
    using namespace SkullbonezCore::UI;
    static_assert( std::is_trivially_copyable_v<OperatorEditorCommandQueues> );
    static_assert( OperatorEditorSceneCommandQueue::capacity == 8u );
    static_assert( OperatorEditorPropertyCommandQueue::capacity == 24u );
    static_assert( OperatorEditorRenderingCommandQueue::capacity == 8u );
    static_assert( OperatorEditorDiagnosticsCommandQueue::capacity == 8u );
    static_assert( OperatorEditorReplayCommandQueue::capacity == 8u );
    static_assert( OperatorEditorToolCommandQueue::capacity == 16u );

    InGameUICommands legacy;
    legacy.scene.resetScene = true;
    legacy.sceneOptions.requestedTimeScale = 0.5f;
    legacy.renderer.toggleVsync = true;
    legacy.replayMemory.requestPolicy = true;
    legacy.replayMemory.requestedPresetIndex = 2;
    legacy.replayMemory.requestedRetentionSeconds = 45;
    legacy.replayMemory.requestedBudgetMiB = 96;
    REQUIRE( NormalizeLegacyOperatorEditorCommands( legacy ).ok );
    CHECK_FALSE( legacy.scene.resetScene );
    CHECK( legacy.sceneOptions.requestedTimeScale < 0.0f );
    CHECK_FALSE( legacy.renderer.toggleVsync );
    CHECK_FALSE( legacy.replayMemory.requestPolicy );

    OperatorEditorCommandQueues secondary;
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.scene, { OperatorEditorSceneCommandType::ResetCurrentScene, -1 } ).ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.property, { OperatorEditorPropertyCommandType::SetTimeScale, 0.5f } )
            .ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.rendering, { OperatorEditorRenderingCommandType::ToggleVsync } ).ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.replay, { OperatorEditorReplayCommandType::SetMemoryPolicy, 2, 45, 96 } )
            .ok );

    const OperatorEditorArbitrationResult merged = ArbitrateOperatorEditorCommands( legacy.operatorEditor, secondary );
    REQUIRE( merged.status.ok );
    CHECK( merged.acceptedLegacyCommands == 4u );
    CHECK( merged.acceptedSecondaryCommands == 0u );
    CHECK( merged.coalescedDuplicateCommands == 4u );

    REQUIRE( ProjectOperatorEditorCommands( merged.commands, legacy ).ok );
    CHECK( legacy.scene.resetScene );
    CHECK( legacy.sceneOptions.requestedTimeScale == doctest::Approx( 0.5f ) );
    CHECK( legacy.renderer.toggleVsync );
    CHECK( legacy.replayMemory.requestPolicy );
    CHECK( legacy.replayMemory.requestedPresetIndex == 2 );
    CHECK( legacy.replayMemory.requestedRetentionSeconds == 45 );
    CHECK( legacy.replayMemory.requestedBudgetMiB == 96 );
}

TEST_CASE( "Operator editor queue rejects conflict and malformed surface values" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorCommandQueues legacy;
    OperatorEditorCommandQueues secondary;
    REQUIRE(
        SubmitOperatorEditorCommand( legacy.property, { OperatorEditorPropertyCommandType::SetTimeScale, 1.0f } ).ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.property, { OperatorEditorPropertyCommandType::SetTimeScale, 0.5f } )
            .ok );
    CHECK_FALSE( ArbitrateOperatorEditorCommands( legacy, secondary ).status.ok );

    OperatorEditorPropertyCommandQueue invalidProperty;
    CHECK_FALSE( SubmitOperatorEditorCommand(
                     invalidProperty,
                     { OperatorEditorPropertyCommandType::SetTimeScale, std::numeric_limits<float>::quiet_NaN() } )
                     .ok );
    CHECK( invalidProperty.count == 0u );

    OperatorEditorReplayCommandQueue invalidReplay;
    CHECK_FALSE(
        SubmitOperatorEditorCommand( invalidReplay, { OperatorEditorReplayCommandType::SetMemoryPolicy, 0, 0, 64 } )
            .ok );
    CHECK( invalidReplay.count == 0u );

    OperatorEditorCommandQueues corruptCount;
    corruptCount.scene.count = OperatorEditorSceneCommandQueue::capacity + 1u;
    InGameUICommands projected;
    CHECK_FALSE( ProjectOperatorEditorCommands( corruptCount, projected ).ok );
    CHECK_FALSE( projected.scene.resetScene );
}

TEST_CASE( "Operator editor replay transport validates values and arbitrates one owner action" )
{
    using namespace SkullbonezCore::UI;
    const auto replayCommand =
        []( OperatorEditorReplayCommandType type, float value = 0.0f, int rowIndex = -1, bool enabled = false )
    {
        OperatorEditorReplayCommand command;
        command.type = type;
        command.value = value;
        command.rowIndex = rowIndex;
        command.enabled = enabled;
        return command;
    };

    OperatorEditorReplayCommandQueue valid;
    CHECK( SubmitOperatorEditorCommand(
               valid,
               replayCommand( OperatorEditorReplayCommandType::SetRecordingEnabled, 0.0f, -1, true ) )
               .ok );
    CHECK( SubmitOperatorEditorCommand( valid, replayCommand( OperatorEditorReplayCommandType::Scrub, 0.5f ) ).ok );
    CHECK( SubmitOperatorEditorCommand( valid, replayCommand( OperatorEditorReplayCommandType::SetRevealSpeed, 2.0f ) )
               .ok );
    CHECK( SubmitOperatorEditorCommand( valid,
                                        replayCommand( OperatorEditorReplayCommandType::SetPredictionHorizon, 3.0f ) )
               .ok );
    CHECK(
        SubmitOperatorEditorCommand( valid, replayCommand( OperatorEditorReplayCommandType::SelectCauseRow, 0.0f, 4 ) )
            .ok );
    CHECK( valid.count == 5u );

    OperatorEditorReplayCommandQueue invalid;
    CHECK_FALSE( SubmitOperatorEditorCommand(
                     invalid,
                     replayCommand( OperatorEditorReplayCommandType::Scrub, std::numeric_limits<float>::quiet_NaN() ) )
                     .ok );
    CHECK_FALSE(
        SubmitOperatorEditorCommand( invalid, replayCommand( OperatorEditorReplayCommandType::SetRevealSpeed, 10.0f ) )
            .ok );
    CHECK_FALSE(
        SubmitOperatorEditorCommand( invalid,
                                     replayCommand( OperatorEditorReplayCommandType::SetPredictionHorizon, 0.0f ) )
            .ok );
    CHECK_FALSE(
        SubmitOperatorEditorCommand( invalid,
                                     replayCommand( OperatorEditorReplayCommandType::SelectCauseRow, 0.0f, -1 ) )
            .ok );
    CHECK( invalid.count == 0u );

    OperatorEditorCommandQueues legacy;
    OperatorEditorCommandQueues secondary;
    REQUIRE(
        SubmitOperatorEditorCommand( legacy.replay, replayCommand( OperatorEditorReplayCommandType::Scrub, 0.25f ) )
            .ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.replay, replayCommand( OperatorEditorReplayCommandType::Scrub, 0.25f ) )
            .ok );
    const OperatorEditorArbitrationResult duplicate = ArbitrateOperatorEditorCommands( legacy, secondary );
    REQUIRE( duplicate.status.ok );
    CHECK( duplicate.coalescedDuplicateCommands == 1u );

    secondary = {};
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.replay, replayCommand( OperatorEditorReplayCommandType::Scrub, 0.75f ) )
            .ok );
    CHECK_FALSE( ArbitrateOperatorEditorCommands( legacy, secondary ).status.ok );
}

TEST_CASE( "Operator editor world previews stay local and commits project to established owners" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorCommandQueues preview;
    REQUIRE( SubmitOperatorEditorCommand(
                 preview.property,
                 { OperatorEditorPropertyCommandType::SetWorldGravity, -4.5f, 0, OperatorEditorEditPhase::Preview } )
                 .ok );
    InGameUICommands projectedPreview;
    REQUIRE( ProjectOperatorEditorCommands( preview, projectedPreview ).ok );
    CHECK_FALSE( projectedPreview.water.requestWorldGravity );

    OperatorEditorCommandQueues commits;
    for ( const OperatorEditorPropertyCommand& command :
          { OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTimeScale, 0.75f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::ToggleFixedStep },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetModelCount, 0.0f, 120 },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetSeed, 0.0f, 42 },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetSolverBallCount, 0.0f, 70 },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetSolverBoxCount, 0.0f, 50 },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetWorldGravity, -12.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetWorldFluidHeight, 8.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetWorldFluidDensity, 1.2f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::TogglePhysicsSleepPolicy },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTerrainFriction, 0.8f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetObjectFriction, 0.6f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetRollingFriction, 0.04f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::ToggleTornado },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTornadoRadius, 140.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTornadoHeight, 180.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTornadoInward, 90.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTornadoSwirl, 130.0f },
            OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetTornadoLift, 65.0f } } )
    {
        REQUIRE( SubmitOperatorEditorCommand( commits.property, command ).ok );
    }
    REQUIRE( commits.property.count == 19u );
    REQUIRE(
        SubmitOperatorEditorCommand( commits.scene,
                                     OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::RequestDemoScene } )
            .ok );

    InGameUICommands projected;
    REQUIRE( ProjectOperatorEditorCommands( commits, projected ).ok );
    CHECK( projected.scene.requestDemoScene );
    CHECK( projected.sceneOptions.requestedTimeScale == doctest::Approx( 0.75f ) );
    CHECK( projected.sceneOptions.toggleFixedStep );
    CHECK( projected.sceneOptions.requestedModelCount == 120 );
    CHECK( projected.run.requestedSeed == 42 );
    CHECK( projected.run.requestedSolverBallCount == 70 );
    CHECK( projected.run.requestedSolverBoxCount == 50 );
    CHECK( projected.water.requestWorldGravity );
    CHECK( projected.water.requestedWorldGravity == doctest::Approx( -12.0f ) );
    CHECK( projected.water.requestWorldFluidHeight );
    CHECK( projected.water.requestedWorldFluidHeight == doctest::Approx( 8.0f ) );
    CHECK( projected.water.requestWorldFluidDensity );
    CHECK( projected.water.requestedWorldFluidDensity == doctest::Approx( 1.2f ) );
    CHECK( projected.physics.togglePhysicsSleepPolicy );
    CHECK( projected.physics.requestTerrainFrictionCoeff );
    CHECK( projected.physics.requestedTerrainFrictionCoeff == doctest::Approx( 0.8f ) );
    CHECK( projected.physics.requestObjectFrictionCoeff );
    CHECK( projected.physics.requestedObjectFrictionCoeff == doctest::Approx( 0.6f ) );
    CHECK( projected.physics.requestRollingFrictionCoeff );
    CHECK( projected.physics.requestedRollingFrictionCoeff == doctest::Approx( 0.04f ) );
    CHECK( projected.physics.toggleTornado );
    CHECK( projected.physics.requestTornadoRadius );
    CHECK( projected.physics.requestTornadoHeight );
    CHECK( projected.physics.requestTornadoInward );
    CHECK( projected.physics.requestTornadoSwirl );
    CHECK( projected.physics.requestTornadoLift );

    InGameUICommands legacy;
    legacy.scene.requestDemoScene = true;
    legacy.sceneOptions.requestedTimeScale = 0.75f;
    legacy.sceneOptions.toggleFixedStep = true;
    legacy.sceneOptions.requestedModelCount = 120;
    legacy.run.requestedSeed = 42;
    legacy.run.requestedSolverBallCount = 70;
    legacy.run.requestedSolverBoxCount = 50;
    legacy.water.requestWorldGravity = true;
    legacy.water.requestedWorldGravity = -12.0f;
    legacy.water.requestWorldFluidHeight = true;
    legacy.water.requestedWorldFluidHeight = 8.0f;
    legacy.water.requestWorldFluidDensity = true;
    legacy.water.requestedWorldFluidDensity = 1.2f;
    legacy.physics.togglePhysicsSleepPolicy = true;
    legacy.physics.requestTerrainFrictionCoeff = true;
    legacy.physics.requestedTerrainFrictionCoeff = 0.8f;
    legacy.physics.requestObjectFrictionCoeff = true;
    legacy.physics.requestedObjectFrictionCoeff = 0.6f;
    legacy.physics.requestRollingFrictionCoeff = true;
    legacy.physics.requestedRollingFrictionCoeff = 0.04f;
    legacy.physics.toggleTornado = true;
    legacy.physics.requestTornadoRadius = true;
    legacy.physics.requestedTornadoRadius = 140.0f;
    legacy.physics.requestTornadoHeight = true;
    legacy.physics.requestedTornadoHeight = 180.0f;
    legacy.physics.requestTornadoInward = true;
    legacy.physics.requestedTornadoInward = 90.0f;
    legacy.physics.requestTornadoSwirl = true;
    legacy.physics.requestedTornadoSwirl = 130.0f;
    legacy.physics.requestTornadoLift = true;
    legacy.physics.requestedTornadoLift = 65.0f;
    REQUIRE( NormalizeLegacyOperatorEditorCommands( legacy ).ok );
    CHECK_FALSE( legacy.scene.requestDemoScene );
    CHECK_FALSE( legacy.sceneOptions.toggleFixedStep );
    CHECK( legacy.sceneOptions.requestedModelCount == -1 );
    CHECK( legacy.run.requestedSeed == -1 );
    CHECK_FALSE( legacy.water.requestWorldFluidHeight );
    CHECK_FALSE( legacy.physics.toggleTornado );
    CHECK( legacy.operatorEditor.property.count == 19u );
    const OperatorEditorArbitrationResult arbitration =
        ArbitrateOperatorEditorCommands( legacy.operatorEditor, commits );
    REQUIRE( arbitration.status.ok );
    CHECK( arbitration.acceptedLegacyCommands == 20u );
    CHECK( arbitration.acceptedSecondaryCommands == 0u );
    CHECK( arbitration.coalescedDuplicateCommands == 20u );

    OperatorEditorPropertyCommandQueue invalid;
    CHECK_FALSE( SubmitOperatorEditorCommand(
                     invalid,
                     OperatorEditorPropertyCommand{ OperatorEditorPropertyCommandType::SetSeed, 0.0f, 0 } )
                     .ok );
}

TEST_CASE( "Operator editor frame fingerprint follows semantic values only" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorFrameView first;
    const char* sceneOptions[] = { "varied", "terrain" };
    first.scene.sceneName = "SkullbonezData/scenes/varied.scene.json";
    first.scene.sceneOptions = sceneOptions;
    first.scene.currentSceneIndex = 0;
    first.scene.sceneCount = 2;
    first.scene.currentFrame = 42;
    first.scene.modelCount = 200;
    first.scene.timeScale = 0.75f;
    first.scene.canSaveCurrentScene = true;
    first.scene.dirty = true;
    first.property = { -9.81f, 4.0f, 998.0f };
    first.rendering.vsyncEnabled = true;
    first.rendering.shadowsEnabled = true;
    first.rendering.presentationInterpolation = true;
    first.rendering.presentationAlpha = 0.5f;
    first.viewport = { "Inspect", "translate", false };
    first.replay = { 1, 30, 64, 30, 20, false, true };
    first.surfaces = { true, true };
    first.tools = { true, true, false, true, false, true, 3, 2 };
    first.hierarchy.rowCount = 1u;
    first.hierarchy.totalRowCount = 1u;
    first.hierarchy.selectedSceneObjectId = 91u;
    first.hierarchy.rows[0] = { "Crate", 91u, 91u, 0, true, true, false, true };
    first.assets = { 2, 37, true };
    const OperatorEditorFrameView same = first;
    CHECK( FingerprintOperatorEditorFrameView( first ) == FingerprintOperatorEditorFrameView( same ) );

    OperatorEditorFrameView changed = first;
    changed.replay.solverRetentionSeconds = 21;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );

    changed = first;
    changed.hierarchy.rows[0].locked = true;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );

    changed = first;
    changed.viewport.gizmoModeLabel = "rotate";
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );

    changed = first;
    changed.world.tornadoRadius = 151.0f;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );

    changed = first;
    changed.inspector.selectionState = OperatorEditorInspectorSelectionState::Single;
    changed.inspector.sceneObjectId = 91u;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );

    changed = first;
    changed.rendering.cinematicParameters[static_cast<int>( UICinematicParam::Exposure )] = 1.25f;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );
}

TEST_CASE( "Operator editor rendering and diagnostics retain canonical owner projection" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorCommandQueues preview;
    REQUIRE( SubmitOperatorEditorCommand( preview.rendering,
                                          { OperatorEditorRenderingCommandType::SetCinematicParameter,
                                            static_cast<int>( UICinematicParam::Exposure ),
                                            1.4f,
                                            OperatorEditorEditPhase::Preview } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( preview.diagnostics,
                                          { OperatorEditorDiagnosticsCommandType::SetPhysicsDebugAlpha,
                                            0u,
                                            0,
                                            0.5f,
                                            OperatorEditorEditPhase::Preview } )
                 .ok );
    InGameUICommands previewPacket;
    REQUIRE( ProjectOperatorEditorCommands( preview, previewPacket ).ok );
    CHECK( previewPacket.cinematic.requestedParam == UICinematicParam::None );
    CHECK( previewPacket.physics.requestedPhysicsDebugAlpha < 0.0f );

    OperatorEditorCommandQueues commits;
    REQUIRE( SubmitOperatorEditorCommand( commits.rendering,
                                          { OperatorEditorRenderingCommandType::SetOrdinaryParameter,
                                            static_cast<int>( UIRenderParam::WaterFresnel ),
                                            0.04f } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( commits.rendering,
                                          { OperatorEditorRenderingCommandType::SetCinematicParameter,
                                            static_cast<int>( UICinematicParam::BloomStrength ),
                                            0.6f } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( commits.rendering,
                                          { OperatorEditorRenderingCommandType::ToggleCinematicFeature,
                                            static_cast<int>( UICinematicFeature::Bloom ) } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( commits.rendering, { OperatorEditorRenderingCommandType::ToggleWaterFreeze } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( commits.diagnostics,
                                          { OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag,
                                            static_cast<uint32_t>( UIPhysicsDebugOverlay::Contacts ) } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand( commits.diagnostics,
                                          { OperatorEditorDiagnosticsCommandType::SetWorkerThreads, 0u, 4 } )
                 .ok );
    REQUIRE( SubmitOperatorEditorCommand(
                 commits.diagnostics,
                 { OperatorEditorDiagnosticsCommandType::SetRayCastImpulseStrength, 0u, 0, 125.0f } )
                 .ok );

    InGameUICommands projected;
    REQUIRE( ProjectOperatorEditorCommands( commits, projected ).ok );
    CHECK( projected.renderTuning.requestedParam == UIRenderParam::WaterFresnel );
    CHECK( projected.renderTuning.requestedValue == doctest::Approx( 0.04f ) );
    CHECK( projected.cinematic.requestedParam == UICinematicParam::BloomStrength );
    CHECK( projected.cinematic.requestedValue == doctest::Approx( 0.6f ) );
    CHECK( projected.cinematic.requestedFeature == UICinematicFeature::Bloom );
    CHECK( projected.sceneOptions.toggleWaterFreeze );
    CHECK( projected.physics.physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::Contacts );
    CHECK( projected.profiler.requestedWorkerThreads == 4 );
    CHECK( projected.physics.requestRayCastImpulseStrength );
    CHECK( projected.physics.requestedRayCastImpulseStrength == doctest::Approx( 125.0f ) );

    InGameUICommands legacy;
    legacy.renderTuning.requestedParam = UIRenderParam::WaterFresnel;
    legacy.renderTuning.requestedValue = 0.04f;
    legacy.cinematic.requestedParam = UICinematicParam::BloomStrength;
    legacy.cinematic.requestedValue = 0.6f;
    legacy.cinematic.requestedFeature = UICinematicFeature::Bloom;
    legacy.sceneOptions.toggleWaterFreeze = true;
    legacy.physics.physicsDebugOverlayToToggle = UIPhysicsDebugOverlay::Contacts;
    legacy.profiler.requestedWorkerThreads = 4;
    legacy.physics.requestRayCastImpulseStrength = true;
    legacy.physics.requestedRayCastImpulseStrength = 125.0f;
    REQUIRE( NormalizeLegacyOperatorEditorCommands( legacy ).ok );
    CHECK( legacy.renderTuning.requestedParam == UIRenderParam::None );
    CHECK( legacy.cinematic.requestedParam == UICinematicParam::None );
    CHECK( legacy.physics.physicsDebugOverlayToToggle == UIPhysicsDebugOverlay::None );
    CHECK( legacy.profiler.requestedWorkerThreads == -2 );
    const OperatorEditorArbitrationResult merged = ArbitrateOperatorEditorCommands( legacy.operatorEditor, commits );
    REQUIRE( merged.status.ok );
    CHECK( merged.acceptedSecondaryCommands == 0u );
    CHECK( merged.coalescedDuplicateCommands == 7u );
    OperatorEditorDiagnosticsCommandQueue malformedDiagnostics;
    CHECK_FALSE( SubmitOperatorEditorCommand( malformedDiagnostics,
                                              { OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag, 0u } )
                     .ok );
    CHECK_FALSE(
        SubmitOperatorEditorCommand( malformedDiagnostics,
                                     { OperatorEditorDiagnosticsCommandType::TogglePhysicsDebugFlag, 1u << 7 } )
            .ok );
}

TEST_CASE( "Operator editor scene hierarchy and asset intents project through typed owner packets" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorCommandQueues secondary;

    OperatorEditorSceneCommand create;
    create.type = OperatorEditorSceneCommandType::CreateScene;
    strcpy_s( create.sceneName, "typed-editor-scene" );
    REQUIRE( SubmitOperatorEditorCommand( secondary.scene, create ).ok );
    REQUIRE( SubmitOperatorEditorCommand(
                 secondary.scene,
                 OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::SetCurrentSceneIndex, 4 } )
                 .ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.scene,
                                     OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::SaveCurrentScene } )
            .ok );
    REQUIRE(
        SubmitOperatorEditorCommand( secondary.scene,
                                     OperatorEditorSceneCommand{ OperatorEditorSceneCommandType::ResetSceneDefaults } )
            .ok );

    for ( const OperatorEditorToolCommand& command :
          { OperatorEditorToolCommand{ OperatorEditorToolCommandType::SelectSceneObject, 91u },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetEntityVisible, 91u, 0, false },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetEntityLocked, 91u, 0, true },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetPlacementObjectType, 0u, 30 },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetPlaceStatic, 0u, 0, true },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::ToggleTerrainAlign },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::DuplicateSelection },
            OperatorEditorToolCommand{ OperatorEditorToolCommandType::DeleteSelection } } )
    {
        REQUIRE( SubmitOperatorEditorCommand( secondary.tools, command ).ok );
    }

    InGameUICommands projected;
    REQUIRE( ProjectOperatorEditorCommands( secondary, projected ).ok );
    CHECK( projected.scene.createScene );
    CHECK( std::strcmp( projected.scene.requestedSceneName, "typed-editor-scene" ) == 0 );
    CHECK( projected.scene.requestedSceneIndex == 4 );
    CHECK( projected.scene.saveSceneDefaults );
    CHECK( projected.scene.resetSceneDefaults );
    CHECK( projected.editor.requestSelectSceneObject );
    CHECK( projected.editor.requestedSceneObjectId == 91u );
    CHECK( projected.editor.requestSetEntityVisible );
    CHECK_FALSE( projected.editor.requestedEntityVisible );
    CHECK( projected.editor.visibilitySceneObjectId == 91u );
    CHECK( projected.editor.requestSetEntityLocked );
    CHECK( projected.editor.requestedEntityLocked );
    CHECK( projected.editor.lockSceneObjectId == 91u );
    CHECK( projected.editor.requestedObjectType == 30 );
    CHECK( projected.editor.enterPlacementMode );
    CHECK( projected.editor.requestPlaceStatic );
    CHECK( projected.editor.requestedPlaceStatic );
    CHECK( projected.editor.toggleTerrainAlign );
    CHECK( projected.editor.requestDuplicateSelection );
    CHECK( projected.editor.requestDeleteSelection );

    OperatorEditorToolCommandQueue malformed;
    CHECK_FALSE(
        SubmitOperatorEditorCommand( malformed,
                                     OperatorEditorToolCommand{ OperatorEditorToolCommandType::SelectSceneObject, 0u } )
            .ok );
    CHECK_FALSE( SubmitOperatorEditorCommand(
                     malformed,
                     OperatorEditorToolCommand{ OperatorEditorToolCommandType::SetPlacementObjectType, 0u, 37 } )
                     .ok );
}

TEST_CASE( "Operator editor tool commands coalesce and project into established owner packets" )
{
    using namespace SkullbonezCore::UI;
    InGameUICommands legacy;
    legacy.editor.toggleEditorMode = true;
    legacy.editor.togglePlacementMode = true;
    legacy.editor.requestUndo = true;
    legacy.editor.requestRedo = true;
    legacy.scene.toggleCrossScenePause = true;
    legacy.scene.requestSingleStep = true;
    REQUIRE( NormalizeLegacyOperatorEditorCommands( legacy ).ok );
    CHECK( legacy.operatorEditor.tools.count == 6u );

    OperatorEditorCommandQueues secondary;
    for ( const OperatorEditorToolCommandType type : { OperatorEditorToolCommandType::ToggleEditorMode,
                                                       OperatorEditorToolCommandType::TogglePlacementMode,
                                                       OperatorEditorToolCommandType::Undo,
                                                       OperatorEditorToolCommandType::Redo,
                                                       OperatorEditorToolCommandType::ToggleCrossScenePause,
                                                       OperatorEditorToolCommandType::StepPausedScene } )
    {
        REQUIRE( SubmitOperatorEditorCommand( secondary.tools, OperatorEditorToolCommand{ type } ).ok );
    }

    const OperatorEditorArbitrationResult merged = ArbitrateOperatorEditorCommands( legacy.operatorEditor, secondary );
    REQUIRE( merged.status.ok );
    CHECK( merged.acceptedLegacyCommands == 6u );
    CHECK( merged.acceptedSecondaryCommands == 0u );
    CHECK( merged.coalescedDuplicateCommands == 6u );
    REQUIRE( ProjectOperatorEditorCommands( merged.commands, legacy ).ok );
    CHECK( legacy.editor.toggleEditorMode );
    CHECK( legacy.editor.togglePlacementMode );
    CHECK( legacy.editor.requestUndo );
    CHECK( legacy.editor.requestRedo );
    CHECK( legacy.scene.toggleCrossScenePause );
    CHECK( legacy.scene.requestSingleStep );

    OperatorEditorToolCommandQueue malformed;
    CHECK_FALSE(
        SubmitOperatorEditorCommand( malformed,
                                     OperatorEditorToolCommand{ static_cast<OperatorEditorToolCommandType>( 255 ) } )
            .ok );
    CHECK( malformed.count == 0u );
}

TEST_CASE( "Editor dock envelope preserves viewport across supported aspect ratios" )
{
    using namespace SkullbonezCore::Runtime::DevelopmentTools;
    const ImGuiEditorLayoutEnvelope minimum = ResolveImGuiEditorLayoutEnvelope( 1280, 640 );
    const ImGuiEditorLayoutEnvelope widescreen = ResolveImGuiEditorLayoutEnvelope( 1920, 1000 );
    const ImGuiEditorLayoutEnvelope ultrawide = ResolveImGuiEditorLayoutEnvelope( 3440, 1360 );

    for ( const ImGuiEditorLayoutEnvelope& envelope : { minimum, widescreen, ultrawide } )
    {
        CHECK( envelope.editorLeftWidth + envelope.viewportWidth + envelope.utilityRightWidth ==
               envelope.contentWidth );
        CHECK( envelope.upperHeight + envelope.replayHeight + envelope.statusHeight == envelope.contentHeight );
        CHECK( envelope.preservesCentralViewport );
        CHECK( envelope.statusHeight >= 48 );
        CHECK( envelope.replayHeight >= 112 );
    }
    CHECK( minimum.compactToolbarLabels );
    CHECK_FALSE( widescreen.compactToolbarLabels );
    CHECK( ultrawide.viewportWidth > widescreen.viewportWidth );
    CHECK( ultrawide.editorLeftWidth == 420 );
    CHECK( ultrawide.utilityRightWidth == 460 );
    CHECK( FingerprintImGuiEditorDefaultTopology() == 12435707336326486392ull );
    CHECK( std::strcmp( IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR,
                        "v2|status:bottommost|replay:bottom|left:scene,hierarchy,assets|center:game-viewport|"
                        "right:inspector,world,rendering,diagnostics,causality" ) == 0 );
}

TEST_CASE( "Editor preferences round trip and recover stale layout identity" )
{
    using namespace SkullbonezCore::Runtime::DevelopmentTools;

    ImGuiEditorPreferences preferences;
    preferences.topologyFingerprint = FingerprintImGuiEditorDefaultTopology();
    preferences.panelVisibilityMask =
        IMGUI_EDITOR_DEFAULT_PANEL_MASK & ~ImGuiEditorPanelBit( ImGuiEditorPanelId::Diagnostics );
    strcpy_s( preferences.sceneFilter, "stack" );
    strcpy_s( preferences.hierarchyFilter, "crate" );
    strcpy_s( preferences.assetFilter, "terrain" );

    char serialized[IMGUI_EDITOR_PREFERENCES_TEXT_CAPACITY] = {};
    const std::size_t bytes = SerializeImGuiEditorPreferences( preferences, serialized, sizeof( serialized ) );
    REQUIRE( bytes > 0u );
    const ImGuiEditorPreferenceParseResult roundTrip = ParseImGuiEditorPreferences( serialized, bytes );
    REQUIRE( roundTrip.valid );
    CHECK_FALSE( roundTrip.layoutResetRequired );
    CHECK_FALSE( roundTrip.recoveredDefaults );
    CHECK( roundTrip.preferences.panelVisibilityMask == preferences.panelVisibilityMask );
    CHECK( std::strcmp( roundTrip.preferences.sceneFilter, "stack" ) == 0 );
    CHECK( std::strcmp( roundTrip.preferences.hierarchyFilter, "crate" ) == 0 );
    CHECK( std::strcmp( roundTrip.preferences.assetFilter, "terrain" ) == 0 );

    constexpr const char* stale =
        "schema=1\nlayout=1\ntopology=17\npanels=0\nscene_filter=keep\nhierarchy_filter=bounded\nasset_filter=text\n";
    const ImGuiEditorPreferenceParseResult migrated = ParseImGuiEditorPreferences( stale, std::strlen( stale ) );
    REQUIRE( migrated.valid );
    CHECK( migrated.layoutResetRequired );
    CHECK( migrated.recoveredDefaults );
    CHECK( migrated.preferences.layoutVersion == IMGUI_EDITOR_LAYOUT_VERSION );
    CHECK( migrated.preferences.topologyFingerprint == FingerprintImGuiEditorDefaultTopology() );
    CHECK( migrated.preferences.panelVisibilityMask == IMGUI_EDITOR_DEFAULT_PANEL_MASK );
    CHECK( std::strcmp( migrated.preferences.sceneFilter, "keep" ) == 0 );

    constexpr const char* malformed = "schema=1\nlayout=2\ntopology=not-a-number\npanels=4294967295\n";
    const ImGuiEditorPreferenceParseResult recovered =
        ParseImGuiEditorPreferences( malformed, std::strlen( malformed ) );
    CHECK_FALSE( recovered.valid );
    CHECK( recovered.layoutResetRequired );
    CHECK( recovered.recoveredDefaults );
    CHECK( recovered.preferences.panelVisibilityMask == IMGUI_EDITOR_DEFAULT_PANEL_MASK );

    ImGuiEditorPanelId panel = ImGuiEditorPanelId::Count;
    CHECK( TryParseImGuiEditorPanel( "Replay", panel ) );
    CHECK( panel == ImGuiEditorPanelId::Replay );
    CHECK( TryParseImGuiEditorPanel( "Diag###SkoreDiagnostics", panel ) );
    CHECK( panel == ImGuiEditorPanelId::Diagnostics );
    CHECK_FALSE( TryParseImGuiEditorPanel( "Stale Panel", panel ) );
}

TEST_CASE( "Compact causality projection is bounded and exposes explicit edge states" )
{
    using namespace SkullbonezCore::Runtime::DevelopmentTools;
    using namespace SkullbonezCore::Runtime::ReplayOverlay;

    ReplayScrubberView scrubber;
    ReplayPredictionPresentationView prediction;
    ReplayInterceptView intercept;
    ReplayPorkchopPanelView porkchop;
    ReplayTripPlannerView planner;
    RunReplayPathVisualizerState path;
    RunReplayVelocityEditState velocity;
    RunReplayCauseTreeState tree;
    ReplayRecorderStats solverStats;
    ReplayOverlayStateView
        replay{ scrubber, prediction, intercept, porkchop, planner, path, velocity, tree, solverStats };

    ImGuiEditorCausalityContext context = BuildImGuiEditorCausalityContext( replay );
    CHECK( context.state == ImGuiEditorCausalityState::Empty );
    CHECK( context.relevantLinkCount == 0u );

    path.hasTarget = true;
    path.targetId.value = 17u;
    context = BuildImGuiEditorCausalityContext( replay );
    CHECK( context.state == ImGuiEditorCausalityState::CapacityLimited );

    tree.rows.reserve( 32u );
    tree.focusedId.value = 17u;
    context = BuildImGuiEditorCausalityContext( replay );
    CHECK( context.state == ImGuiEditorCausalityState::Stale );

    RunReplayCauseTreeRow root;
    root.id.value = 7u;
    strcpy_s( root.name, "Root crate" );
    strcpy_s( root.detail, "retained root state" );
    tree.rows.push_back( root );

    RunReplayCauseTreeRow child;
    child.id.value = 17u;
    child.parentId.value = 7u;
    child.firstFrame = 41u;
    child.depth = 1;
    strcpy_s( child.name, "Affected sphere" );
    strcpy_s( child.detail, "first affected frame 41" );
    tree.rows.push_back( child );
    tree.selectedRow = 1;

    ReplaySolverFrameSample selectedSolver;
    selectedSolver.frameIndex = 42u;
    replay.selection.selectedSolver = &selectedSolver;
    replay.prediction.enabled = true;
    replay.prediction.building = true;
    replay.prediction.targetId.value = 17u;

    context = BuildImGuiEditorCausalityContext( replay );
    REQUIRE( context.selectedObjectRow != nullptr );
    REQUIRE( context.immediateCauseRow != nullptr );
    CHECK( context.state == ImGuiEditorCausalityState::Ready );
    CHECK( context.selectedObjectRow->id.value == 17u );
    CHECK( context.immediateCauseRow->id.value == 7u );
    CHECK( context.hasReplayTick );
    CHECK( context.replayTick == 42u );
    CHECK( context.predictionState == ImGuiEditorPredictionState::Building );

    for ( int index = 0; index < 12; ++index )
    {
        RunReplayCauseTreeRow detail;
        detail.kind = RunReplayCauseTreeRowKind::SolverRow;
        detail.id.value = 17u;
        detail.parentId.value = 7u;
        detail.depth = 2;
        sprintf_s( detail.name, "Solver row %d", index );
        tree.rows.push_back( detail );
    }
    context = BuildImGuiEditorCausalityContext( replay );
    CHECK( context.state == ImGuiEditorCausalityState::Truncated );
    CHECK( context.relevantLinkCount == IMGUI_CAUSALITY_RELEVANT_LINK_CAPACITY );
    CHECK( context.compactScanTruncated );
    CHECK( context.totalRowCount == 14u );
}

TEST_CASE( "Game viewport policy letterboxes and maps physical client pixels" )
{
    using namespace SkullbonezCore::Runtime::DevelopmentTools;
    const ImGuiGameViewportRect widePane =
        ResolveImGuiGameViewportRect( 100.0f, 50.0f, 1000.0f, 500.0f, 800, 600, 1.5f );
    REQUIRE( widePane.valid );
    CHECK( widePane.letterboxed );
    CHECK( widePane.imageMinX == doctest::Approx( 266.6667f ) );
    CHECK( widePane.imageMinY == doctest::Approx( 50.0f ) );
    CHECK( widePane.imageWidth == doctest::Approx( 666.6667f ) );
    CHECK( widePane.imageHeight == doctest::Approx( 500.0f ) );
    CHECK( widePane.dpiScale == doctest::Approx( 1.5f ) );

    int sourceX = -1;
    int sourceY = -1;
    CHECK( MapImGuiGameViewportPoint( widePane, 600.0f, 300.0f, sourceX, sourceY ) );
    CHECK( sourceX == 400 );
    CHECK( sourceY == 300 );
    CHECK_FALSE( MapImGuiGameViewportPoint( widePane, 120.0f, 300.0f, sourceX, sourceY ) );
    CHECK( sourceX == 0 );
    CHECK( sourceY == 0 );

    const ImGuiGameViewportRect tallPane =
        ResolveImGuiGameViewportRect( 10.0f, 20.0f, 500.0f, 900.0f, 1920, 1080, 2.0f );
    REQUIRE( tallPane.valid );
    CHECK( tallPane.letterboxed );
    CHECK( tallPane.imageMinX == doctest::Approx( 10.0f ) );
    CHECK( tallPane.imageMinY == doctest::Approx( 329.375f ) );
    CHECK( tallPane.imageWidth == doctest::Approx( 500.0f ) );
    CHECK( tallPane.imageHeight == doctest::Approx( 281.25f ) );
    CHECK( MapImGuiGameViewportPoint( tallPane, 509.9f, 610.5f, sourceX, sourceY ) );
    CHECK( sourceX == 1919 );
    CHECK( sourceY == 1079 );

    const ImGuiGameViewportRect invalid = ResolveImGuiGameViewportRect( 0.0f, 0.0f, 0.0f, 100.0f, 800, 600, 0.0f );
    CHECK_FALSE( invalid.valid );
    CHECK( invalid.dpiScale == doctest::Approx( 1.0f ) );
}
