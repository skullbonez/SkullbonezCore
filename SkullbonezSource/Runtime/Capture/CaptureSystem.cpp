/*
File: SkullbonezSource/Runtime/Capture/CaptureSystem.cpp
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Summary:
  Runtime code decides when a screenshot should happen, then borrows the
  concrete DX12 capture owner. CaptureSystem validates the readback contract,
  writes BMP bytes, and returns automation decisions without owning renderer
  resources.

Glossary:
  Capture owner: DX12 component that reads pixels from the active back buffer.
  Back buffer: Swap-chain image that will be presented to the window.
  BMP (Bitmap): Simple image file format used by validation backbuffer captures.

Invariants:
  - Capture succeeds only when the concrete owner reports valid dimensions.
  - SaveBackbufferBmp receives only the capture/readback surface, not the full
    render device.
  - Capture automation reports the completion action separately from the side
    effect so Run can decide whether to quit, advance, or hold.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "CaptureSystem.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/ByteView.h"
#include "CaptureController.h"

#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
#include "../../Rendering/DX12/Dx12BackbufferCapture.h"
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
namespace
{
struct FileCloser
{
    void operator()( FILE* file ) const
    {

        if ( file )
        {
            fclose( file );
        }
    }
};

using FileHandle = std::unique_ptr<FILE, FileCloser>;

SkullbonezCore::Core::SbResult WriteExact( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, FILE* file,
                                           SkullbonezCore::Core::ByteView bytes, const char* path )
{

    // Invariant: validation screenshots are binary artifacts; a short write is
    // a failed capture, not a partial success that downstream comparisons can
    // safely inspect.

    if ( bytes.empty() )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    const size_t written = fwrite( bytes.data(), 1, bytes.size(), file );

    if ( written != bytes.size() )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem",
                                    "Failed to write screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
    }

    return SkullbonezCore::Core::SbResult::Success();
}

RuntimeCaptureAutomation CompletionAutomation( bool isInteractiveRun, RuntimeCaptureAutomation automationWhenHeadless )
{

    // Why: interactive captures should keep the window available for inspection,
    // while validation launches need an explicit automation policy to finish.
    return isInteractiveRun ? RuntimeCaptureAutomation::HoldInteractive : automationWhenHeadless;
}

void BuildScreenshotAndExitPath( const char* scenePath, char* outPath, size_t outPathSize )
{
    const char* slash = strrchr( scenePath, '/' );
    const char* backslash = strrchr( scenePath, '\\' );
    const char* name = slash ? slash + 1 : ( backslash ? backslash + 1 : scenePath );

    char stem[256];
    strcpy_s( stem, sizeof( stem ), name );
    char* dot = strrchr( stem, '.' );

    if ( dot )
    {
        *dot = '\0';
    }

    sprintf_s( outPath, outPathSize, "%s.bmp", stem );
}
} // namespace

SkullbonezCore::Core::SbResult CaptureSystem::SaveBackbufferBmp( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                 Rendering::Dx12BackbufferCapture& backend,
                                                                 const char* path )
{

    // Lane R: capture support, readback dimensions, and file output can fail
    // because of renderer/device/file-system environment state, so callers get
    // an owner/message result instead of an exception unwind.

    if ( !backend.SupportsBackbufferCapture() )
    {
        return diagnostics
            .Failure( "Runtime/CaptureSystem",
                      "Renderer does not support backbuffer capture for file: %s  (CaptureSystem::SaveBackbufferBmp)",
                      path );
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    const SkullbonezCore::Core::SbResult readbackResult = backend.CaptureBackbuffer( pixels, width, height );

    if ( !readbackResult.Ok() )
    {
        return readbackResult;
    }

    if ( width <= 0 || height <= 0 )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem",
                                    "Invalid screenshot dimensions for file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
    }

    const int rowStride = ( width * 3 + 3 ) & ~3;
    const int imageSize = rowStride * height;

    if ( pixels.size() < static_cast<size_t>( imageSize ) )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem",
                                    "Screenshot readback returned %zu byte(s), expected %d for "
                                    "file: %s  (CaptureSystem::SaveBackbufferBmp)",
                                    pixels.size(), imageSize, path );
    }

    unsigned char fileHeader[14] = {};
    const int fileSize = 14 + 40 + imageSize;
    fileHeader[0] = 'B';
    fileHeader[1] = 'M';
    fileHeader[2] = static_cast<unsigned char>( fileSize );
    fileHeader[3] = static_cast<unsigned char>( fileSize >> 8 );
    fileHeader[4] = static_cast<unsigned char>( fileSize >> 16 );
    fileHeader[5] = static_cast<unsigned char>( fileSize >> 24 );
    fileHeader[10] = 54; // Pixel data offset.

    unsigned char infoHeader[40] = {};
    infoHeader[0] = 40; // Header size.
    infoHeader[4] = static_cast<unsigned char>( width );

    infoHeader[5] = static_cast<unsigned char>( width >> 8 );
    infoHeader[6] = static_cast<unsigned char>( width >> 16 );
    infoHeader[7] = static_cast<unsigned char>( width >> 24 );
    infoHeader[8] = static_cast<unsigned char>( height );
    infoHeader[9] = static_cast<unsigned char>( height >> 8 );
    infoHeader[10] = static_cast<unsigned char>( height >> 16 );
    infoHeader[11] = static_cast<unsigned char>( height >> 24 );
    infoHeader[12] = 1;  // Color planes.
    infoHeader[14] = 24; // Bits per pixel.

    infoHeader[20] = static_cast<unsigned char>( imageSize );

    infoHeader[21] = static_cast<unsigned char>( imageSize >> 8 );
    infoHeader[22] = static_cast<unsigned char>( imageSize >> 16 );
    infoHeader[23] = static_cast<unsigned char>( imageSize >> 24 );

    FILE* rawFile = nullptr;
    const errno_t err = fopen_s( &rawFile, path, "wb" );

    if ( err != 0 || !rawFile )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem",
                                    "Failed to open screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
    }

    FileHandle file( rawFile );

    SkullbonezCore::Core::SbResult writeResult = WriteExact( diagnostics, file.get(), fileHeader, path );

    if ( !writeResult.Ok() )
    {
        return writeResult;
    }

    writeResult = WriteExact( diagnostics, file.get(), infoHeader, path );

    if ( !writeResult.Ok() )
    {
        return writeResult;
    }

    writeResult = WriteExact( diagnostics, file.get(), { pixels.data(), static_cast<size_t>( imageSize ) }, path );

    if ( !writeResult.Ok() )
    {
        return writeResult;
    }

    return SkullbonezCore::Core::SbResult::Success();
}
#endif

bool CaptureSystem::IsScreenshotDue( const RunScreenshotState& screenshot, bool isSceneMode, int currentFrame,
                                     double elapsedMs )
{

    if ( !isSceneMode )
    {
        return false;
    }

    if ( screenshot.isScreenshotAndExit && currentFrame == 0 )
    {
        return true;
    }

    if ( screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {

        if ( screenshot.screenshotFrame > 0 && ( currentFrame + 1 ) >= screenshot.screenshotFrame )
        {
            return true;
        }

        if ( screenshot.screenshotMs > 0 && elapsedMs >= screenshot.screenshotMs )
        {
            return true;
        }
    }

    return screenshot.screenshotInterval > 0 && screenshot.screenshotDir[0] != '\0' &&
           ( currentFrame + 1 ) % screenshot.screenshotInterval == 0;
}


bool CaptureSystem::RequiresDeterministicPresentation( const RunScreenshotState& screenshot, bool isSceneMode,
                                                       int currentFrame, double elapsedMs )
{

    if ( !isSceneMode )
    {
        return false;
    }

    // Hazard: a millisecond trigger is checked again after rendering and can
    // cross its threshold during the frame. Pin every pending one-shot scene
    // capture so trigger timing can never select an interpolated backbuffer.

    if ( screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {
        return true;
    }

    return IsScreenshotDue( screenshot, isSceneMode, currentFrame, elapsedMs );
}


#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
RuntimeCaptureResult CaptureSystem::TickScreenshots( RunScreenshotState& screenshot, bool isSceneMode, bool isInteractiveRun,
                                                     int currentFrame, double elapsedMs, const char* currentScenePath,
                                                     CaptureController& capture, Rendering::Dx12BackbufferCapture& backend )
{

    if ( isSceneMode && screenshot.isScreenshotAndExit && currentFrame == 0 )
    {

        if ( !currentScenePath )
        {
            return {};
        }

        char outPath[256];
        BuildScreenshotAndExitPath( currentScenePath, outPath, sizeof( outPath ) );
        const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, outPath );

        if ( !captureResult.Ok() )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
        }

        return { true, RuntimeCaptureCompletion::ScreenshotAndExit,
                 CompletionAutomation( isInteractiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    if ( isSceneMode && screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {
        bool shouldCapture = false;

        if ( screenshot.screenshotFrame > 0 && ( currentFrame + 1 ) >= screenshot.screenshotFrame )
        {
            shouldCapture = true;
        }

        if ( screenshot.screenshotMs > 0 && elapsedMs >= screenshot.screenshotMs )
        {
            shouldCapture = true;
        }

        if ( shouldCapture )
        {
            const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend,
                                                                                         screenshot.screenshotPath );

            if ( !captureResult.Ok() )
            {
                return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
            }

            screenshot.isScreenshotSaved = true;
            return { true, RuntimeCaptureCompletion::Screenshot,
                     CompletionAutomation( isInteractiveRun, RuntimeCaptureAutomation::AdvanceSceneOrQuit ) };
        }
    }

    if ( isSceneMode && screenshot.screenshotInterval > 0 && screenshot.screenshotDir[0] != '\0' )
    {

        if ( ( currentFrame + 1 ) % screenshot.screenshotInterval == 0 )
        {
            ++screenshot.intervalCaptureCount;
            char intervalPath[512];
            sprintf_s( intervalPath, sizeof( intervalPath ), "%s/capture_%04d.bmp", screenshot.screenshotDir,
                       screenshot.intervalCaptureCount );

            const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, intervalPath );

            if ( !captureResult.Ok() )
            {
                return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
            }
        }
    }

    return {};
}

RuntimeCaptureResult CaptureSystem::TickAutoCycle( bool isSceneMode, bool isInteractiveRun, int ballCount,
                                                   float& autoCycleInterval, float& autoCycleAccum, int& autoCycleShotsTaken,
                                                   int& trackBallIndex, CaptureController& capture,
                                                   Rendering::Dx12BackbufferCapture& backend )
{

    if ( !isSceneMode || autoCycleInterval <= 0.0f || autoCycleAccum < autoCycleInterval )
    {
        return {};
    }

    char shotPath[256];
    sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", autoCycleShotsTaken );
    const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, shotPath );

    if ( !captureResult.Ok() )
    {
        return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
    }

    fprintf( stdout, "Auto-shot %d: ball index %d -> %s\n", autoCycleShotsTaken, trackBallIndex, shotPath );
    fflush( stdout );

    ++autoCycleShotsTaken;
    autoCycleAccum = 0.0f;

    if ( autoCycleShotsTaken >= ballCount )
    {
        return { false, RuntimeCaptureCompletion::AutoCycle,
                 CompletionAutomation( isInteractiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    trackBallIndex = ( trackBallIndex + 1 ) % ballCount;
    return {};
}
#endif
} // namespace Runtime
} // namespace SkullbonezCore
