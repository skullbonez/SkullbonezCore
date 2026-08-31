/*
File: SkullbonezSource/Runtime/Capture/CaptureSystem.cpp
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Summary:
  Runtime code decides when a screenshot should happen, then borrows the
  concrete DX12 capture owner. CaptureSystem validates the readback contract,
  writes BMP or PNG bytes, and returns automation decisions without owning
  renderer resources.

Glossary:
  BMP (Bitmap): Simple image file format used by validation backbuffer captures.
  PNG (Portable Network Graphics): Bundle image format encoded from the same
    padded bottom-up BGR readback without changing renderer ownership.

Invariants:
  - Capture succeeds only when the concrete owner reports valid dimensions.
  - SaveBackbufferBmp receives only the capture/readback surface, not the full
    render device.
  - PNG conversion flips rows and swaps BGR to RGB before building valid zlib
    and chunk checksums; CPU tests pin the exact orientation and channel order.
  - Capture automation reports the completion action separately from the side
    effect so Run can decide whether to quit, advance, or hold.

Related:
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
  - Agentic/Reference/engine-glossary.md
*/
#include "CaptureSystem.h"
#include "../../Core/AtomicTextFileWriter.h"
#include "../../Core/SbDiagnosticStore.h"
#include "CaptureController.h"

#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
#include "../../Rendering/DX12/Dx12BackbufferCapture.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
void AppendBigEndian32( std::vector<uint8_t>& output, uint32_t value )
{
    output.push_back( static_cast<uint8_t>( value >> 24 ) );
    output.push_back( static_cast<uint8_t>( value >> 16 ) );
    output.push_back( static_cast<uint8_t>( value >> 8 ) );
    output.push_back( static_cast<uint8_t>( value ) );
}

uint32_t PngCrc32( std::span<const uint8_t> bytes )
{
    uint32_t crc = 0xffffffffu;

    for ( uint8_t byte : bytes )
    {
        crc ^= byte;

        for ( int bit = 0; bit < 8; ++bit )
        {
            crc = ( crc >> 1 ) ^ ( 0xedb88320u & ( 0u - ( crc & 1u ) ) );
        }
    }

    return crc ^ 0xffffffffu;
}

uint32_t ZlibAdler32( std::span<const uint8_t> bytes )
{
    constexpr uint32_t MODULUS = 65521u;
    uint32_t a = 1u;
    uint32_t b = 0u;

    for ( uint8_t byte : bytes )
    {
        a = ( a + byte ) % MODULUS;
        b = ( b + a ) % MODULUS;
    }

    return ( b << 16 ) | a;
}

void AppendPngChunk( std::vector<uint8_t>& output, const char type[4], std::span<const uint8_t> payload )
{
    AppendBigEndian32( output, static_cast<uint32_t>( payload.size() ) );
    const size_t crcStart = output.size();
    output.insert( output.end(), type, type + 4 );
    output.insert( output.end(), payload.begin(), payload.end() );
    AppendBigEndian32( output, PngCrc32( std::span<const uint8_t>( output ).subspan( crcStart ) ) );
}
} // namespace

SkullbonezCore::Core::SbResult CaptureSystem::BuildPngBytes( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                             std::span<const uint8_t> bottomUpBgr, int width, int height,
                                                             std::vector<uint8_t>& output )
{
    output.clear();

    if ( width <= 0 || height <= 0 || static_cast<size_t>( width ) > ( std::numeric_limits<size_t>::max )() / 3u )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem", "PNG dimensions are invalid: %dx%d", width, height );
    }

    const size_t sourceRowStride = ( static_cast<size_t>( width ) * 3u + 3u ) & ~size_t { 3u };
    const size_t scanlineStride = static_cast<size_t>( width ) * 3u + 1u;

    if ( static_cast<size_t>( height ) > ( std::numeric_limits<size_t>::max )() / sourceRowStride ||
         static_cast<size_t>( height ) > ( std::numeric_limits<size_t>::max )() / scanlineStride ||
         bottomUpBgr.size() < sourceRowStride * static_cast<size_t>( height ) )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem", "PNG source pixels do not cover %dx%d BGR rows.", width,
                                    height );
    }

    std::vector<uint8_t> scanlines( scanlineStride * static_cast<size_t>( height ) );

    for ( int y = 0; y < height; ++y )
    {
        const uint8_t* source = bottomUpBgr.data() + static_cast<size_t>( height - 1 - y ) * sourceRowStride;
        uint8_t* destination = scanlines.data() + static_cast<size_t>( y ) * scanlineStride;
        destination[0] = 0u;

        for ( int x = 0; x < width; ++x )
        {
            destination[1 + x * 3 + 0] = source[x * 3 + 2];
            destination[1 + x * 3 + 1] = source[x * 3 + 1];
            destination[1 + x * 3 + 2] = source[x * 3 + 0];
        }
    }

    // Concept: PNG permits a zlib stream made from uncompressed DEFLATE blocks.
    // Capture is a cold path, so a tiny self-contained encoder avoids adding a
    // process-wide image dependency while retaining exact RGB bytes.
    std::vector<uint8_t> zlib;
    zlib.reserve( scanlines.size() + scanlines.size() / 65535u * 5u + 11u );
    zlib.push_back( 0x78u );
    zlib.push_back( 0x01u );
    size_t cursor = 0;

    while ( cursor < scanlines.size() )
    {
        const size_t blockSize = (std::min)( size_t { 65535u }, scanlines.size() - cursor );
        const bool finalBlock = cursor + blockSize == scanlines.size();
        const uint16_t length = static_cast<uint16_t>( blockSize );
        const uint16_t inverseLength = static_cast<uint16_t>( ~length );
        zlib.push_back( finalBlock ? 0x01u : 0x00u );
        zlib.push_back( static_cast<uint8_t>( length ) );
        zlib.push_back( static_cast<uint8_t>( length >> 8 ) );
        zlib.push_back( static_cast<uint8_t>( inverseLength ) );
        zlib.push_back( static_cast<uint8_t>( inverseLength >> 8 ) );
        zlib.insert( zlib.end(), scanlines.begin() + static_cast<std::ptrdiff_t>( cursor ),
                     scanlines.begin() + static_cast<std::ptrdiff_t>( cursor + blockSize ) );

        cursor += blockSize;
    }

    AppendBigEndian32( zlib, ZlibAdler32( scanlines ) );

    if ( zlib.size() > ( std::numeric_limits<uint32_t>::max )() )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem", "PNG IDAT payload exceeds the format limit." );
    }

    constexpr uint8_t signature[] = { 0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };
    output.insert( output.end(), std::begin( signature ), std::end( signature ) );
    std::array<uint8_t, 13> header = {};
    header[0] = static_cast<uint8_t>( static_cast<uint32_t>( width ) >> 24 );
    header[1] = static_cast<uint8_t>( static_cast<uint32_t>( width ) >> 16 );
    header[2] = static_cast<uint8_t>( static_cast<uint32_t>( width ) >> 8 );
    header[3] = static_cast<uint8_t>( width );
    header[4] = static_cast<uint8_t>( static_cast<uint32_t>( height ) >> 24 );
    header[5] = static_cast<uint8_t>( static_cast<uint32_t>( height ) >> 16 );
    header[6] = static_cast<uint8_t>( static_cast<uint32_t>( height ) >> 8 );
    header[7] = static_cast<uint8_t>( height );
    header[8] = 8u;
    header[9] = 2u;
    AppendPngChunk( output, "IHDR", header );
    AppendPngChunk( output, "IDAT", zlib );
    AppendPngChunk( output, "IEND", {} );
    return SkullbonezCore::Core::SbResult::Success();
}


bool CaptureSystem::TryBuildScreenshotAndExitPath( const char* scenePath, char* outPath, size_t outPathSize ) noexcept
{
    if ( !scenePath || !outPath || outPathSize == 0u )
    {
        return false;
    }

    outPath[0] = '\0';
    const char* name = scenePath;

    for ( const char* cursor = scenePath; *cursor != '\0'; ++cursor )
    {
        if ( *cursor == '/' || *cursor == '\\' )
        {
            name = cursor + 1;
        }
    }

    const char* dot = strrchr( name, '.' );
    const size_t stemLength = dot ? static_cast<size_t>( dot - name ) : strlen( name );
    constexpr char extension[] = ".bmp";

    if ( stemLength == 0u || stemLength + sizeof( extension ) > outPathSize )
    {
        return false;
    }

    memcpy( outPath, name, stemLength );
    memcpy( outPath + stemLength, extension, sizeof( extension ) );
    return true;
}


SkullbonezCore::Core::SbResult
CaptureSystem::SaveScreenshotBytesAtomic( SkullbonezCore::Core::SbDiagnosticStore& diagnostics, const char* path,
                                          std::span<const uint8_t> bytes )
{
    const std::string_view byteView = bytes.empty()
                                          ? std::string_view {}
                                          : std::string_view( reinterpret_cast<const char*>( bytes.data() ), bytes.size() );
    return SkullbonezCore::Core::WriteFileAtomic( diagnostics, "Runtime/CaptureSystem", path, byteView );
}

#if defined( SKULLBONEZ_CAPTURE_EXECUTION )
namespace
{
RuntimeCaptureAutomation CompletionAutomation( bool isInteractiveRun, RuntimeCaptureAutomation automationWhenHeadless )
{
    // Why: interactive captures should keep the window available for inspection,
    // while validation launches need an explicit automation policy to finish.
    return isInteractiveRun ? RuntimeCaptureAutomation::HoldInteractive : automationWhenHeadless;
}

} // namespace

SkullbonezCore::Core::SbResult CaptureSystem::SaveBackbufferBmp( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                 Rendering::Dx12BackbufferCapture& backend,
                                                                 const char* path )
{
    // Recoverable error: capture support, readback dimensions, and file output can fail
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

    // Hazard: the final artifact may be validation evidence from an earlier
    // run. Assemble the complete replacement in memory, then publish through
    // the shared temporary-sibling transaction so short writes and close
    // failures cannot truncate that retained file.
    std::vector<uint8_t> bmp;
    bmp.reserve( static_cast<size_t>( fileSize ) );
    bmp.insert( bmp.end(), std::begin( fileHeader ), std::end( fileHeader ) );
    bmp.insert( bmp.end(), std::begin( infoHeader ), std::end( infoHeader ) );
    bmp.insert( bmp.end(), pixels.begin(), pixels.begin() + imageSize );
    return SaveScreenshotBytesAtomic( diagnostics, path, bmp );
}


SkullbonezCore::Core::SbResult CaptureSystem::SaveBackbufferPng( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                 Rendering::Dx12BackbufferCapture& backend,
                                                                 const char* path )
{
    if ( !backend.SupportsBackbufferCapture() )
    {
        return diagnostics.Failure( "Runtime/CaptureSystem", "Renderer does not support PNG backbuffer capture: %s", path );
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    SkullbonezCore::Core::SbResult result = backend.CaptureBackbuffer( pixels, width, height );

    if ( !result.Ok() )
    {
        return result;
    }

    std::vector<uint8_t> png;
    result = BuildPngBytes( diagnostics, pixels, width, height, png );

    if ( !result.Ok() )
    {
        return result;
    }

    return SaveScreenshotBytesAtomic( diagnostics, path, png );
}
#endif

ScreenshotCapturePlan CaptureSystem::BuildScreenshotCapturePlan( const RunScreenshotState& screenshot, bool isSceneMode,
                                                                 int currentFrame, double elapsedMs )
{
    ScreenshotCapturePlan plan;

    if ( !isSceneMode )
    {
        return plan;
    }

    if ( screenshot.screenshotPath[0] != '\0' && !screenshot.isScreenshotSaved )
    {
        plan.oneShotDue = ( screenshot.screenshotFrame > 0 && ( currentFrame + 1 ) >= screenshot.screenshotFrame ) ||
                          ( screenshot.screenshotMs > 0 && elapsedMs >= screenshot.screenshotMs );
    }

    plan.intervalDue = screenshot.screenshotInterval > 0 && screenshot.screenshotDir[0] != '\0' &&
                       ( currentFrame + 1 ) % screenshot.screenshotInterval == 0;
    return plan;
}


bool CaptureSystem::IsScreenshotDue( const RunScreenshotState& screenshot, bool isSceneMode, int currentFrame,
                                     double elapsedMs )
{
    if ( isSceneMode && screenshot.isScreenshotAndExit && currentFrame == 0 )
    {
        return true;
    }

    const ScreenshotCapturePlan plan = BuildScreenshotCapturePlan( screenshot, isSceneMode, currentFrame, elapsedMs );
    return plan.oneShotDue || plan.intervalDue;
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
RuntimeCaptureResult CaptureSystem::TickScreenshots( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                     RunScreenshotState& screenshot, const ScreenshotFrameInput& input,
                                                     CaptureController& capture, Rendering::Dx12BackbufferCapture& backend )
{
    if ( input.sceneMode && screenshot.isScreenshotAndExit && input.frame == 0 )
    {
        if ( !input.scenePath )
        {
            return {};
        }

        char outPath[256];

        if ( !TryBuildScreenshotAndExitPath( input.scenePath, outPath, sizeof( outPath ) ) )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None,
                     diagnostics.Failure( "Runtime/CaptureSystem",
                                          "Could not derive screenshot-and-exit path from scene: %s", input.scenePath ) };
        }

        const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, outPath );

        if ( !captureResult.Ok() )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
        }

        return { true, RuntimeCaptureCompletion::ScreenshotAndExit,
                 CompletionAutomation( input.interactiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    const ScreenshotCapturePlan plan = BuildScreenshotCapturePlan( screenshot, input.sceneMode, input.frame,
                                                                   input.elapsedMs );
    RuntimeCaptureResult result;

    if ( plan.oneShotDue )
    {
        const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, screenshot.screenshotPath );

        if ( !captureResult.Ok() )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
        }

        screenshot.isScreenshotSaved = true;
        result = { true, RuntimeCaptureCompletion::Screenshot,
                   CompletionAutomation( input.interactiveRun, RuntimeCaptureAutomation::AdvanceSceneOrQuit ) };
    }

    if ( plan.intervalDue )
    {
        const int nextIntervalCapture = screenshot.intervalCaptureCount + 1;
        char intervalPath[512];
        const int formatted = sprintf_s( intervalPath, sizeof( intervalPath ), "%s/capture_%04d.bmp",
                                         screenshot.screenshotDir, nextIntervalCapture );

        if ( formatted < 0 )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None,
                     diagnostics.Failure( "Runtime/CaptureSystem", "Interval screenshot path is too long: %s",
                                          screenshot.screenshotDir ) };
        }

        const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, intervalPath );

        if ( !captureResult.Ok() )
        {
            return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
        }

        screenshot.intervalCaptureCount = nextIntervalCapture;
    }

    return result;
}

RuntimeCaptureResult CaptureSystem::TickAutoCycle( const AutoCycleCaptureInput& input, AutoCycleCaptureUpdate& update,
                                                   CaptureController& capture, Rendering::Dx12BackbufferCapture& backend )
{
    update = {};
    if ( !input.Due() )
    {
        return {};
    }

    char shotPath[256];
    sprintf_s( shotPath, sizeof( shotPath ), "Profile/cardinal_ball%d.bmp", input.shotsTaken );
    const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, shotPath );

    if ( !captureResult.Ok() )
    {
        return { false, RuntimeCaptureCompletion::None, RuntimeCaptureAutomation::None, captureResult };
    }

    fprintf( stdout, "Auto-shot %d: ball index %d -> %s\n", input.shotsTaken, input.trackedBallIndex, shotPath );
    fflush( stdout );

    update.apply = true;
    update.accumulatedSeconds = 0.0f;
    update.shotsTaken = input.shotsTaken + 1;
    update.trackedBallIndex = input.trackedBallIndex;

    if ( update.shotsTaken >= input.ballCount )
    {
        return { false, RuntimeCaptureCompletion::AutoCycle,
                 CompletionAutomation( input.interactiveRun, RuntimeCaptureAutomation::Quit ) };
    }

    update.trackedBallIndex = ( input.trackedBallIndex + 1 ) % input.ballCount;
    return {};
}
#endif
} // namespace Runtime
} // namespace SkullbonezCore
