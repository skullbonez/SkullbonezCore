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

Invariants:
  - Tests stop at the fixed capacity because the next runtime submission is a
    deliberate fatal owner-budget violation.
  - Replay owner codes are compatibility values and must not be renumbered.
  - Render-default saves reject config versions newer than the engine-owned
    schema before rewriting any bytes.
  - Shared editor views fingerprint semantic fields rather than object padding.
  - The versioned editor topology is identical at minimum, 16:9, and ultrawide sizes.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/Scene/SceneRequestQueue.h
  - SkullbonezSource/Runtime/RenderDefaultsStore.h
  - SkullbonezSource/UI/OperatorEditorExchange.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/CaptureController.h"
#include "../SkullbonezSource/Runtime/RenderDefaultsStore.h"
#include "../SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/Runtime/Scene/SceneController.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRequestQueue.h"
#include "../SkullbonezSource/Runtime/Scene/SceneControllerState.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntime.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h"
#include "../SkullbonezSource/Rendering/IRenderCaptureBackend.h"
#include "../SkullbonezSource/UI/UICommands.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>

using namespace SkullbonezCore::Runtime;

namespace
{
class UnsupportedCaptureBackend final : public SkullbonezCore::Rendering::IRenderCaptureBackend
{
  public:
    bool SupportsBackbufferCapture() const override
    {
        return false;
    }

    SkullbonezCore::Core::SbResult CaptureBackbuffer( std::vector<uint8_t>&, int&, int& ) override
    {
        return SkullbonezCore::Core::SbResult::Failure( "Test/UnsupportedCaptureBackend", "unexpected readback" );
    }
};

class FailingCaptureBackend final : public SkullbonezCore::Rendering::IRenderCaptureBackend
{
  public:
    bool SupportsBackbufferCapture() const override
    {
        return true;
    }

    SkullbonezCore::Core::SbResult
    CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight ) override
    {
        outPixels.assign( 4, 0xff );
        outWidth = 1;
        outHeight = 1;
        return SkullbonezCore::Core::SbResult::Failure( "Test/Readback", "fence wait failed" );
    }
};
} // namespace

TEST_CASE( "Scene lifecycle accepts ordered phases and explicit restart" )
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

    // A recoverable failure may restart the transaction, but it cannot skip a
    // commit phase and publish plausible lifecycle evidence out of order.
    CHECK( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeScenePopulate,
                                                 SceneRuntimeLifecycleEvent::BeforeSceneUnload ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::BeforeSceneUnload,
                                                       SceneRuntimeLifecycleEvent::BeforeScenePopulate ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterSceneCleared,
                                                       SceneRuntimeLifecycleEvent::AfterSceneActivated ) );
    CHECK_FALSE( SceneRuntimeLifecycleTransitionValid( SceneRuntimeLifecycleEvent::AfterSceneActivated,
                                                       SceneRuntimeLifecycleEvent::None ) );

    const SceneLifecycleConsumerMask beforeUnload = SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics ) |
                                                    SceneLifecycleConsumerBit( SceneLifecycleConsumer::RenderDevice );
    const SceneLifecycleConsumerMask afterClear = SceneLifecycleConsumerBit( SceneLifecycleConsumer::Diagnostics ) |
                                                  SceneLifecycleConsumerBit( SceneLifecycleConsumer::Simulation ) |
                                                  SceneLifecycleConsumerBit( SceneLifecycleConsumer::Tools ) |
                                                  SceneLifecycleConsumerBit( SceneLifecycleConsumer::Interaction ) |
                                                  SceneLifecycleConsumerBit( SceneLifecycleConsumer::Replay );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeSceneUnload ) == beforeUnload );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneCleared ) == afterClear );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::BeforeScenePopulate ) == 0 );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterScenePopulate ) == 0 );
    CHECK( SceneLifecycleRequiredConsumers( SceneRuntimeLifecycleEvent::AfterSceneActivated ) ==
           SceneLifecycleConsumerBit( SceneLifecycleConsumer::Replay ) );
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

    const SceneLoadRequest current = navigation.LoadSceneFromBrowserIndex( 0, scene );
    CHECK( current.accepted );
    CHECK_FALSE( current.HasLoad() );
    CHECK( current.enterInteractiveSceneRun );

    const SceneLoadRequest appended = navigation.LoadSceneFromBrowserIndex( 1, scene );
    CHECK( appended.HasLoad() );
    CHECK( appended.index == 1 );
    CHECK( scene.PathAt( 1 ) == "SkullbonezData/scenes/beta.scene.json" );
    CHECK_FALSE( navigation.LoadSceneFromBrowserIndex( -1, scene ).accepted );

    const SceneLoadRequest demo = navigation.LoadDemoScene( scene );
    CHECK( demo.HasLoad() );
    CHECK( demo.index == 2 );
    CHECK( scene.PathAt( 2 ).empty() );
    CHECK( navigation.LoadDemoScene( scene ).index == 2 );
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

    CHECK( navigation.AdjacentCinematicModeBrowserIndex( 1, 0, false ) == 3 );
    CHECK( navigation.AdjacentCinematicModeBrowserIndex( -1, 0, false ) == 3 );
    CHECK( navigation.AdjacentCinematicModeBrowserIndex( 0, 0, true ) == -1 );

    const SceneLoadRequest adjacent = navigation.LoadAdjacentScene( 1, 1, scene );
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

TEST_CASE( "CaptureController returns only successful requests as accepted events" )
{
    CaptureController capture;
    UnsupportedCaptureBackend backend;
    REQUIRE( capture.QueueScreenshot( "Screenshots\\unsupported.bmp" ).ok );

    const CaptureRequestBatchResult result = capture.DrainScreenshotRequests( backend );
    CHECK_FALSE( result.status.ok );
    CHECK( result.savedCount == 0 );
    CHECK( result.failedCount == 1 );
    CHECK( capture.PendingScreenshotCount() == 0 );
}

TEST_CASE( "CaptureController preserves backend readback failure ownership" )
{
    CaptureController capture;
    FailingCaptureBackend backend;
    REQUIRE( capture.QueueScreenshot( "Screenshots\\readback_failure.bmp" ).ok );

    const CaptureRequestBatchResult result = capture.DrainScreenshotRequests( backend );
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
    CHECK( configText.find( "format_version = 4" ) != std::string::npos );

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

TEST_CASE( "RenderDefaultsStore v3 writers remove only the rejected SIMD row" )
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
        CHECK( configText.find( "format_version = 4" ) != std::string::npos );
        CHECK( configText.find( "physics_simd_kernels" ) == std::string::npos );
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
    const std::string originalText = "format_version = 5\nordinary_sun_intensity = 1.00\n";
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
    static_assert( OperatorEditorSceneCommandQueue::capacity == 4u );
    static_assert( OperatorEditorPropertyCommandQueue::capacity == 4u );

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

TEST_CASE( "Operator editor frame fingerprint follows semantic values only" )
{
    using namespace SkullbonezCore::UI;
    OperatorEditorFrameView first;
    first.scene = { "SkullbonezData/scenes/varied.scene.json", 3, 8, 42, 200, 0.75f };
    first.property = { -9.81f, 4.0f, 998.0f };
    first.rendering = { true, true, false, true, 0.5f };
    first.replay = { 1, 30, 64, 30, 20, false, true };
    first.surfaces = { true, true };
    first.tools = { true, true, false, true, false, 3, 2 };
    const OperatorEditorFrameView same = first;
    CHECK( FingerprintOperatorEditorFrameView( first ) == FingerprintOperatorEditorFrameView( same ) );

    OperatorEditorFrameView changed = first;
    changed.replay.solverRetentionSeconds = 21;
    CHECK( FingerprintOperatorEditorFrameView( first ) != FingerprintOperatorEditorFrameView( changed ) );
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
    CHECK( FingerprintImGuiEditorDefaultTopology() == 9482475421528666861ull );
    CHECK( std::strcmp( IMGUI_EDITOR_TOPOLOGY_DESCRIPTOR,
                        "v2|status:bottommost|replay:bottom|left:scene,hierarchy,assets|center:game-viewport|"
                        "right:inspector,world,render-audio,diagnostics,causality" ) == 0 );
}
