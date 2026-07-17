/*
File: TestOwnerRequestQueues.cpp
Purpose:
  Verifies fixed scene, capture, and render-default owner request contracts.

Summary:
  Each owner accepts only its domain intent, keeps FIFO order, and exposes a
  fixed capacity. Invalid bounded text is rejected before it consumes storage.

Glossary:
  FIFO (First In, First Out): Requests drain in submission order.
  Wire code: Explicit serialized replay value independent of C++ enum ordinals.
  Readback result: Recoverable capture owner/message returned by the renderer
    after it attempts to copy the backbuffer into CPU-visible bytes.

Invariants:
  - Tests stop at the fixed capacity because the next runtime submission is a
    deliberate fatal owner-budget violation.
  - Replay owner codes are compatibility values and must not be renumbered.
  - Render-default saves reject config versions newer than the engine-owned
    schema before rewriting any bytes.

Related:
  - SkullbonezSource/Runtime/CaptureController.h
  - SkullbonezSource/Runtime/Scene/SceneRequestQueue.h
  - SkullbonezSource/Runtime/RenderDefaultsStore.h
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/CaptureController.h"
#include "../SkullbonezSource/Runtime/RenderDefaultsStore.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRequestQueue.h"
#include "../SkullbonezSource/Runtime/Scene/SceneControllerState.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntime.h"
#include "../SkullbonezSource/Runtime/Scene/SceneRuntimeCoordinator.h"
#include "../SkullbonezSource/Rendering/IRenderCaptureBackend.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
                                                  SceneLifecycleConsumerBit( SceneLifecycleConsumer::Audio ) |
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
