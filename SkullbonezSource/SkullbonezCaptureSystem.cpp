/*
File: SkullbonezSource/SkullbonezCaptureSystem.cpp
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezCaptureSystem.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezCaptureSystem.h"

#include "SkullbonezIRenderBackend.h"
#include "SkullbonezRun.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
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

void WriteExact( FILE* file, const void* data, size_t size, const char* path )
{
    if ( size == 0 )
    {
        return;
    }

    const size_t written = fwrite( data, 1, size, file );
    if ( written != size )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to write screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }
}

RuntimeCaptureAutomation CompletionAutomation( bool isInteractiveRun, RuntimeCaptureAutomation automationWhenHeadless )
{
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

void CaptureSystem::SaveBackbufferBmp( Rendering::IRenderBackend& backend, const char* path )
{
    if ( !backend.GetCapabilities().supportsBackbufferCapture )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Renderer does not support backbuffer capture for file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels = backend.CaptureBackbuffer( width, height );
    if ( width <= 0 || height <= 0 )
    {
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Invalid screenshot dimensions for file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }

    const int rowStride = ( width * 3 + 3 ) & ~3;
    const int imageSize = rowStride * height;

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
        char msg[512];
        sprintf_s( msg, sizeof( msg ), "Failed to open screenshot file: %s  (CaptureSystem::SaveBackbufferBmp)", path );
        throw std::runtime_error( msg );
    }
    FileHandle file( rawFile );

    WriteExact( file.get(), fileHeader, sizeof( fileHeader ), path );
    WriteExact( file.get(), infoHeader, sizeof( infoHeader ), path );
    WriteExact( file.get(), pixels.data(), static_cast<size_t>( imageSize ), path );
}

RuntimeCaptureResult CaptureSystem::TickScreenshots( RunScreenshotState& screenshot,
                                                     const RuntimeCaptureSceneContext& context,
                                                     RuntimeCaptureSink& sink )
{
    if ( context.isSceneMode && screenshot.isScreenshotAndExit && context.currentFrame == 0 )
    {
        if ( !context.currentScenePath )
        {
            return {};
        }

        char outPath[256];
        BuildScreenshotAndExitPath( context.currentScenePath, outPath, sizeof( outPath ) );
        sink.SaveScreenshot( outPath );
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
            sink.SaveScreenshot( screenshot.screenshotPath );
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
            sprintf_s( intervalPath, sizeof( intervalPath ), "%s/capture_%04d.bmp", screenshot.screenshotDir, screenshot.intervalCaptureCount );
            sink.SaveScreenshot( intervalPath );
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
                                                   RuntimeCaptureSink& sink )
{
    if ( !isSceneMode || autoCycleInterval <= 0.0f || autoCycleAccum < autoCycleInterval )
    {
        return {};
    }

    char shotPath[256];
    sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", autoCycleShotsTaken );
    sink.SaveScreenshot( shotPath );
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
} // namespace Basics
} // namespace SkullbonezCore
