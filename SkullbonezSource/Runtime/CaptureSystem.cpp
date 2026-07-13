/*
File: SkullbonezSource/Runtime/CaptureSystem.cpp
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Summary:
  Runtime code decides when a screenshot should happen, then hands a narrow
  capture backend to this file. CaptureSystem validates the readback contract,
  writes BMP bytes, and returns automation decisions without owning renderer
  resources.

Glossary:
  Capture backend: Narrow renderer capability that can report capture support
  and read pixels from the active back buffer.
  Back buffer: Swap-chain image that will be presented to the window.
  BMP (Bitmap): Simple image file format used by validation backbuffer captures.

Invariants:
  - Capture only succeeds through a backend that explicitly supports
    back-buffer readback and returns positive dimensions.
  - SaveBackbufferBmp receives only the capture/readback surface, not the full
    render device.
  - Capture automation reports the completion action separately from the side
    effect so Run can decide whether to quit, advance, or hold.

Related:
  - SkullbonezSource/Runtime/CaptureSystem.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "CaptureSystem.h"
#include "CaptureController.h"

#include "../Rendering/IRenderCaptureBackend.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
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

SkullbonezCore::Core::SbResult WriteExact( FILE* file, const void* data, size_t size, const char* path )
{
    // Invariant: validation screenshots are binary artifacts; a short write is
    // a failed capture, not a partial success that downstream comparisons can
    // safely inspect.
    if ( size == 0 )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    const size_t written = fwrite( data, 1, size, file );
    if ( written != size )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            "Runtime/CaptureSystem",
            "Failed to write screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)",
            path );
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

SkullbonezCore::Core::SbResult CaptureSystem::SaveBackbufferBmp( Rendering::IRenderCaptureBackend& backend,
                                                                 const char* path )
{
    // Lane R: capture support, readback dimensions, and file output can fail
    // because of renderer/device/file-system environment state, so callers get
    // an owner/message result instead of an exception unwind.
    if ( !backend.SupportsBackbufferCapture() )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            "Runtime/CaptureSystem",
            "Renderer does not support backbuffer capture for file: %s  (CaptureSystem::SaveBackbufferBmp)",
            path );
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    const SkullbonezCore::Core::SbResult readbackResult = backend.CaptureBackbuffer( pixels, width, height );
    if ( !readbackResult.ok )
    {
        return readbackResult;
    }
    if ( width <= 0 || height <= 0 )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            "Runtime/CaptureSystem",
            "Invalid screenshot dimensions for file: %s  (CaptureSystem::SaveBackbufferBmp)",
            path );
    }

    const int rowStride = ( width * 3 + 3 ) & ~3;
    const int imageSize = rowStride * height;
    if ( pixels.size() < static_cast<size_t>( imageSize ) )
    {
        return SkullbonezCore::Core::SbResult::Failure(
            "Runtime/CaptureSystem",
            "Screenshot readback returned %zu byte(s), expected %d for file: %s  (CaptureSystem::SaveBackbufferBmp)",
            pixels.size(),
            imageSize,
            path );
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
        return SkullbonezCore::Core::SbResult::Failure(
            "Runtime/CaptureSystem",
            "Failed to open screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)",
            path );
    }
    FileHandle file( rawFile );

    SkullbonezCore::Core::SbResult writeResult = WriteExact( file.get(), fileHeader, sizeof( fileHeader ), path );
    if ( !writeResult.ok )
    {
        return writeResult;
    }
    writeResult = WriteExact( file.get(), infoHeader, sizeof( infoHeader ), path );
    if ( !writeResult.ok )
    {
        return writeResult;
    }
    writeResult = WriteExact( file.get(), pixels.data(), static_cast<size_t>( imageSize ), path );
    if ( !writeResult.ok )
    {
        return writeResult;
    }
    return SkullbonezCore::Core::SbResult::Success();
}

bool CaptureSystem::IsScreenshotDue( const RunScreenshotState& screenshot, const RuntimeCaptureSceneContext& context )
{
    if ( !context.isSceneMode )
    {
        return false;
    }
    if ( screenshot.isScreenshotAndExit && context.currentFrame == 0 )
    {
        return true;
    }
    if ( screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {
        if ( screenshot.screenshotFrame > 0 && ( context.currentFrame + 1 ) >= screenshot.screenshotFrame )
        {
            return true;
        }
        if ( screenshot.screenshotMs > 0 && context.elapsedMs >= screenshot.screenshotMs )
        {
            return true;
        }
    }
    return screenshot.screenshotInterval > 0 && screenshot.screenshotDir[0] != '\0' &&
           ( context.currentFrame + 1 ) % screenshot.screenshotInterval == 0;
}


bool CaptureSystem::RequiresDeterministicPresentation( const RunScreenshotState& screenshot,
                                                       const RuntimeCaptureSceneContext& context )
{
    if ( !context.isSceneMode )
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
    return IsScreenshotDue( screenshot, context );
}


RuntimeCaptureResult CaptureSystem::TickScreenshots( RunScreenshotState& screenshot,
                                                     const RuntimeCaptureSceneContext& context,
                                                     CaptureController& capture,
                                                     Rendering::IRenderCaptureBackend& backend )
{
    if ( context.isSceneMode && screenshot.isScreenshotAndExit && context.currentFrame == 0 )
    {
        if ( !context.currentScenePath )
        {
            return {};
        }

        char outPath[256];
        BuildScreenshotAndExitPath( context.currentScenePath, outPath, sizeof( outPath ) );
        const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, outPath );
        if ( !captureResult.ok )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
        }
        return { true,
                 RuntimeCaptureCompletion::ScreenshotAndExit,
                 CompletionAutomation( context.isInteractiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    if ( context.isSceneMode && screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {
        bool shouldCapture = false;

        if ( screenshot.screenshotFrame > 0 && ( context.currentFrame + 1 ) >= screenshot.screenshotFrame )
        {
            shouldCapture = true;
        }
        if ( screenshot.screenshotMs > 0 && context.elapsedMs >= screenshot.screenshotMs )
        {
            shouldCapture = true;
        }

        if ( shouldCapture )
        {
            const SkullbonezCore::Core::SbResult captureResult =
                capture.SaveScreenshot( backend, screenshot.screenshotPath );
            if ( !captureResult.ok )
            {
                return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
            }
            screenshot.isScreenshotSaved = true;
            return { true,
                     RuntimeCaptureCompletion::Screenshot,
                     CompletionAutomation( context.isInteractiveRun, RuntimeCaptureAutomation::AdvanceSceneOrQuit ) };
        }
    }

    if ( context.isSceneMode && screenshot.screenshotInterval > 0 && screenshot.screenshotDir[0] != '\0' )
    {
        if ( ( context.currentFrame + 1 ) % screenshot.screenshotInterval == 0 )
        {
            ++screenshot.intervalCaptureCount;
            char intervalPath[512];
            sprintf_s( intervalPath,
                       sizeof( intervalPath ),
                       "%s/capture_%04d.bmp",
                       screenshot.screenshotDir,
                       screenshot.intervalCaptureCount );
            const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, intervalPath );
            if ( !captureResult.ok )
            {
                return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
            }
        }
    }

    return {};
}

RuntimeCaptureResult CaptureSystem::TickAutoCycle( bool isSceneMode,
                                                   bool isInteractiveRun,
                                                   int ballCount,
                                                   float& autoCycleInterval,
                                                   float& autoCycleAccum,
                                                   int& autoCycleShotsTaken,
                                                   int& trackBallIndex,
                                                   CaptureController& capture,
                                                   Rendering::IRenderCaptureBackend& backend )
{
    if ( !isSceneMode || autoCycleInterval <= 0.0f || autoCycleAccum < autoCycleInterval )
    {
        return {};
    }

    char shotPath[256];
    sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", autoCycleShotsTaken );
    const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, shotPath );
    if ( !captureResult.ok )
    {
        return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
    }
    fprintf( stdout, "Auto-shot %d: ball index %d -> %s\n", autoCycleShotsTaken, trackBallIndex, shotPath );
    fflush( stdout );

    ++autoCycleShotsTaken;
    autoCycleAccum = 0.0f;

    if ( autoCycleShotsTaken >= ballCount )
    {
        return { false,
                 RuntimeCaptureCompletion::AutoCycle,
                 CompletionAutomation( isInteractiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    trackBallIndex = ( trackBallIndex + 1 ) % ballCount;
    return {};
}
} // namespace Runtime
} // namespace SkullbonezCore
