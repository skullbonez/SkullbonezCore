/*
File: SkullbonezSource/Runtime/Direction/LiveStyleController.cpp
Purpose:
  Applies live style-harness updates without restarting physics or scene state.

Summary:
  LiveStyleController parses live style-harness updates and publishes bounded
  values without borrowing the Scene or Capture owners that apply them.

Invariants:
  - Live style polling is opt-in and style-only; it must not reload scene
    physics or replace runtime-owned bodies.
  - Control file paths are resolved once from the configured directory so
    relative automation behaves the same in validation and interactive runs.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "LiveStyleController.h"
#include "../../Core/PlatformWin32.h"
#include "../../Scene/AuthoredScene.h"
#include <cstdio>
#include <cstring>


using namespace SkullbonezCore::Runtime;

namespace
{
bool IsPathSeparator( char c )
{
    return c == '\\' || c == '/';
}


bool IsAbsolutePath( const char* path )
{
    return path && ( IsPathSeparator( path[0] ) ||
                     ( path[0] != '\0' && path[1] == ':' && IsPathSeparator( path[2] ) ) );
}


uint64_t FileStamp( const char* path )
{
    // Why: live style files are tiny, so a combined timestamp/size stamp avoids
    // rereading every frame while still catching rapid save updates.
    WIN32_FILE_ATTRIBUTE_DATA data = {};

    if ( !path || !GetFileAttributesExA( path, GetFileExInfoStandard, &data ) )
    {
        return 0;
    }

    ULARGE_INTEGER writeTime = {};
    writeTime.LowPart = data.ftLastWriteTime.dwLowDateTime;
    writeTime.HighPart = data.ftLastWriteTime.dwHighDateTime;

    ULARGE_INTEGER fileSize = {};
    fileSize.LowPart = data.nFileSizeLow;
    fileSize.HighPart = data.nFileSizeHigh;

    return writeTime.QuadPart ^ ( fileSize.QuadPart * 1099511628211ull );
}


char* TrimLeft( char* text )
{
    while ( *text == ' ' || *text == '\t' )
    {
        ++text;
    }

    return text;
}


void TrimRight( char* text )
{
    size_t len = strlen( text );

    while ( len > 0 && ( text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t' ) )
    {
        text[--len] = '\0';
    }
}


bool TokenMatches( const char* text, const char* token )
{
    const size_t len = strlen( token );
    return strncmp( text, token, len ) == 0 && ( text[len] == '\0' || text[len] == ' ' || text[len] == '\t' );
}


bool ExtractCapturePath( const char* source, char* out, size_t outSize )
{
    out[0] = '\0';

    if ( !source )
    {
        return false;
    }

    char line[512] = {};
    strcpy_s( line, sizeof( line ), source );
    TrimRight( line );

    char* cursor = TrimLeft( line );

    if ( cursor[0] == '\0' || cursor[0] == '#' )
    {
        return false;
    }

    if ( TokenMatches( cursor, "capture" ) )
    {
        cursor += strlen( "capture" );
    }
    else if ( TokenMatches( cursor, "screenshot" ) )
    {
        cursor += strlen( "screenshot" );
    }

    cursor = TrimLeft( cursor );

    if ( cursor[0] == '"' )
    {
        ++cursor;
        char* end = strchr( cursor, '"' );

        if ( end )
        {
            *end = '\0';
        }
    }

    if ( cursor[0] == '\0' )
    {
        return false;
    }

    strcpy_s( out, outSize, cursor );
    return true;
}


enum class CaptureRequestReadResult
{
    Unavailable,
    Invalid,
    NoRequest,
    Found
};

CaptureRequestReadResult ReadCaptureRequest( const char* path, char* out, size_t outSize )
{
    FILE* file = nullptr;
    const errno_t err = fopen_s( &file, path, "rb" );

    if ( err != 0 || !file )
    {
        return CaptureRequestReadResult::Unavailable;
    }

    char line[LIVE_STYLE_SCREENSHOT_PATH_CAPACITY] = {};
    bool found = false;
    bool invalid = false;
    bool reachedEnd = false;

    // Hazard: fgets cannot distinguish an exact final line from the prefix of
    // an overlong command. Read physical bytes so no truncated path can become
    // a different valid screenshot destination.
    while ( !found && !invalid && !reachedEnd )
    {
        std::size_t length = 0u;

        for ( ;; )
        {
            const int next = fgetc( file );

            if ( next == EOF )
            {
                reachedEnd = true;
                break;
            }

            if ( next == '\n' )
            {
                break;
            }

            if ( next == '\0' || length + 1u >= sizeof( line ) )
            {
                invalid = true;

                // Consume the rejected logical line so a suffix can never be
                // reconsidered as a second capture command.
                int remainder = next;

                while ( remainder != '\n' && remainder != EOF )
                {
                    remainder = fgetc( file );
                }

                reachedEnd = remainder == EOF;
                break;
            }

            line[length++] = static_cast<char>( next );
        }

        line[length] = '\0';

        if ( invalid || ferror( file ) != 0 )
        {
            break;
        }

        if ( ExtractCapturePath( line, out, outSize ) )
        {
            found = true;
        }
    }

    const bool readFailed = ferror( file ) != 0;
    const bool closeFailed = fclose( file ) != 0;

    if ( readFailed || closeFailed )
    {
        return CaptureRequestReadResult::Unavailable;
    }

    if ( invalid )
    {
        return CaptureRequestReadResult::Invalid;
    }

    return found ? CaptureRequestReadResult::Found : CaptureRequestReadResult::NoRequest;
}


} // namespace


void LiveStyleController::WriteStatus( const char* status, const char* detail ) const
{
    if ( !m_enabled || m_statusPath[0] == '\0' )
    {
        return;
    }

    FILE* file = nullptr;
    const errno_t err = fopen_s( &file, m_statusPath, "w" );

    if ( err != 0 || !file )
    {
        return;
    }

    fprintf( file, "status %s\n", status ? status : "unknown" );
    fprintf( file, "detail %s\n", detail ? detail : "" );
    fprintf( file, "style_applies %d\n", m_styleApplyCount );
    fprintf( file, "captures %d\n", m_captureCount );
    fprintf( file, "live_style %s\n", m_stylePath );
    fprintf( file, "capture_control %s\n", m_capturePath );
    fclose( file );
}


bool LiveStyleController::ConfigureDirectory( const char* path )
{
    const LiveStyleControlPaths resolved = ResolveLiveStyleControlPaths( path );

    if ( !resolved.valid )
    {
        return false;
    }

    std::memcpy( m_directory, resolved.directory.data(), sizeof( m_directory ) );
    std::memcpy( m_stylePath, resolved.style.data(), sizeof( m_stylePath ) );
    std::memcpy( m_capturePath, resolved.capture.data(), sizeof( m_capturePath ) );
    std::memcpy( m_statusPath, resolved.status.data(), sizeof( m_statusPath ) );
    m_styleStamp = 0;
    m_captureStamp = FileStamp( m_capturePath );
    m_pendingScreenshotPath[0] = '\0';
    m_hasPendingScreenshot = false;
    m_styleApplyCount = 0;
    m_captureCount = 0;
    m_enabled = true;
    return true;
}


void LiveStyleController::MarkReady()
{
    WriteStatus( "ready", "watching live.style.json" );
    printf( "[style-harness] Watching %s\n", m_directory );
}


bool LiveStyleController::Poll( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                const Assets::AssetSystem& assets, AuthoredScene& outStyle )
{
    if ( !m_enabled )
    {
        return false;
    }

    bool styleReady = false;
    const uint64_t styleStamp = FileStamp( m_stylePath );

    if ( styleStamp != 0 && styleStamp != m_styleStamp )
    {
        const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadStyleFromFile( resultDiagnostics,
                                                                                               m_stylePath, assets,
                                                                                               outStyle );
        m_styleStamp = ResolveLiveStyleRetainedStamp( m_styleStamp, styleStamp, loadResult.Ok() );

        if ( loadResult.Ok() )
        {
            styleReady = true;
        }
        else
        {
            const char* message = loadResult.ErrorMessage()[0] != '\0' ? loadResult.ErrorMessage() : "style load failed";
            WriteStatus( "style_error", message );
            fprintf( stderr, "[style-harness] Style error: %s\n", message );
        }
    }

    const uint64_t captureStamp = FileStamp( m_capturePath );

    if ( captureStamp != 0 && captureStamp != m_captureStamp )
    {
        char requestedPath[512] = {};
        const CaptureRequestReadResult readResult = ReadCaptureRequest( m_capturePath, requestedPath,
                                                                        sizeof( requestedPath ) );
        m_captureStamp = ResolveLiveStyleRetainedStamp( m_captureStamp, captureStamp,
                                                        readResult != CaptureRequestReadResult::Unavailable );

        if ( readResult == CaptureRequestReadResult::Found )
        {
            char resolvedPath[sizeof( m_pendingScreenshotPath )] = {};
            const bool pathFits = IsAbsolutePath( requestedPath )
                                      ? TryBuildLiveStylePath( "", requestedPath, resolvedPath, sizeof( resolvedPath ) )
                                      : TryBuildLiveStylePath( m_directory, requestedPath, resolvedPath,
                                                               sizeof( resolvedPath ) );

            if ( pathFits )
            {
                std::memcpy( m_pendingScreenshotPath, resolvedPath, sizeof( m_pendingScreenshotPath ) );
                m_hasPendingScreenshot = true;
                WriteStatus( "capture_pending", m_pendingScreenshotPath );
            }
            else
            {
                WriteStatus( "capture_ignored", "requested screenshot path exceeds bounded capacity" );
            }
        }
        else if ( readResult == CaptureRequestReadResult::NoRequest )
        {
            WriteStatus( "capture_ignored", "capture.txt contains no screenshot path" );
        }
        else if ( readResult == CaptureRequestReadResult::Invalid )
        {
            WriteStatus( "capture_ignored", "capture.txt contains an invalid or overlong request" );
        }
    }

    return styleReady;
}


void LiveStyleController::MarkStyleApplied()
{
    ++m_styleApplyCount;
    WriteStatus( "style_applied", m_stylePath );
    printf( "[style-harness] Applied %s\n", m_stylePath );
}


bool LiveStyleController::HasPendingCapture() const
{
    return m_enabled && m_hasPendingScreenshot;
}


const char* LiveStyleController::PendingScreenshotPath() const
{
    return m_pendingScreenshotPath;
}


void LiveStyleController::MarkCaptureSaved()
{
    ++m_captureCount;
    WriteStatus( "capture_saved", m_pendingScreenshotPath );
    printf( "[style-harness] Captured %s\n", m_pendingScreenshotPath );
    m_pendingScreenshotPath[0] = '\0';
    m_hasPendingScreenshot = false;
}


void LiveStyleController::MarkCaptureFailed( const char* message )
{
    const char* detail = message && message[0] != '\0' ? message : "capture failed";
    WriteStatus( "capture_error", detail );
    fprintf( stderr, "[style-harness] Capture error: %s\n", detail );
    m_pendingScreenshotPath[0] = '\0';
    m_hasPendingScreenshot = false;
}
