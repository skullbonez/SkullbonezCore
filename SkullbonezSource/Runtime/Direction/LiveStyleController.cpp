/*
File: SkullbonezSource/Runtime/Direction/LiveStyleController.cpp
Purpose:
  Applies live style-harness updates without restarting physics or scene state.

Summary:
  LiveStyleController applies live style-harness updates without restarting
  physics or scene state. As an implementation unit, keep edits anchored on
  local owner boundaries and call direction and on the glossary/invariants
  below.

Glossary:
  Lane R result: Recoverable style-load or capture failure reported to the
    control status file and stderr while the run stays alive.

Invariants:
  - Live style polling is opt-in and style-only; it must not reload scene
    physics or replace runtime-owned bodies.
  - Control file paths are resolved once from the configured directory so
    relative automation behaves the same in validation and interactive runs.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "LiveStyleController.h"
#include "../Scene/SceneController.h"
#include "../../Core/PlatformWin32.h"
#include "../Capture/CaptureController.h"
#include "../../Rendering/DX12/Dx12BackbufferCapture.h"
#include "../Scene/SceneRuntimeStyle.h"
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
    return path && ( IsPathSeparator( path[0] ) || ( path[0] != '\0' && path[1] == ':' ) );
}


void JoinControlPath( const char* directory, const char* fileName, char* out, size_t outSize )
{

    if ( !directory || directory[0] == '\0' )
    {
        strcpy_s( out, outSize, fileName );
        return;
    }

    const size_t len = strlen( directory );

    if ( len > 0 && IsPathSeparator( directory[len - 1] ) )
    {
        sprintf_s( out, outSize, "%s%s", directory, fileName );
    }
    else
    {
        sprintf_s( out, outSize, "%s\\%s", directory, fileName );
    }
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


bool ReadCaptureRequest( const char* path, char* out, size_t outSize )
{
    FILE* file = nullptr;
    const errno_t err = fopen_s( &file, path, "r" );

    if ( err != 0 || !file )
    {
        return false;
    }

    char line[512] = {};
    bool found = false;

    while ( fgets( line, sizeof( line ), file ) )
    {

        if ( ExtractCapturePath( line, out, outSize ) )
        {
            found = true;
            break;
        }
    }

    fclose( file );
    return found;
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

    if ( !path || path[0] == '\0' )
    {
        return false;
    }

    strcpy_s( m_directory, sizeof( m_directory ), path );
    JoinControlPath( m_directory, "live.style.json", m_stylePath, sizeof( m_stylePath ) );
    JoinControlPath( m_directory, "capture.txt", m_capturePath, sizeof( m_capturePath ) );
    JoinControlPath( m_directory, "status.txt", m_statusPath, sizeof( m_statusPath ) );
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


void LiveStyleController::Tick( RunLaunchOptions& launchOptions, SceneController& sceneController,
                                SkullbonezCore::UI::RunSceneBrowserState& sceneBrowser, const Assets::AssetSystem& assets,
                                SkullbonezCore::Core::CinematicRenderConfig& activeCinematic,
                                const SkullbonezCore::Core::CinematicRenderConfig& defaultCinematic )
{

    if ( !m_enabled )
    {
        return;
    }

    const uint64_t styleStamp = FileStamp( m_stylePath );

    if ( styleStamp != 0 && styleStamp != m_styleStamp )
    {
        m_styleStamp = styleStamp;
        AuthoredScene styleScene;
        const SkullbonezCore::Core::SbResult loadResult = AuthoredScene::TryLoadStyleFromFile( m_stylePath, assets,
                                                                                               styleScene );

        if ( loadResult.ok )
        {
            sceneController.ApplyLiveStyle( launchOptions, sceneBrowser, activeCinematic, defaultCinematic, styleScene );
            ++m_styleApplyCount;
            WriteStatus( "style_applied", m_stylePath );
            printf( "[style-harness] Applied %s\n", m_stylePath );
        }
        else
        {
            const char* message = loadResult.error.message[0] != '\0' ? loadResult.error.message : "style load failed";
            WriteStatus( "style_error", message );
            fprintf( stderr, "[style-harness] Style error: %s\n", message );
        }
    }

    const uint64_t captureStamp = FileStamp( m_capturePath );

    if ( captureStamp != 0 && captureStamp != m_captureStamp )
    {
        m_captureStamp = captureStamp;

        char requestedPath[512] = {};

        if ( ReadCaptureRequest( m_capturePath, requestedPath, sizeof( requestedPath ) ) )
        {

            if ( IsAbsolutePath( requestedPath ) )
            {
                strcpy_s( m_pendingScreenshotPath, sizeof( m_pendingScreenshotPath ), requestedPath );
            }
            else
            {
                JoinControlPath( m_directory, requestedPath, m_pendingScreenshotPath, sizeof( m_pendingScreenshotPath ) );
            }

            m_hasPendingScreenshot = true;
            WriteStatus( "capture_pending", m_pendingScreenshotPath );
        }
        else
        {
            WriteStatus( "capture_ignored", "capture.txt contains no screenshot path" );
        }
    }
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


void LiveStyleController::SavePendingCapture( CaptureController& capture, Rendering::Dx12BackbufferCapture& backend )
{

    if ( !HasPendingCapture() )
    {
        return;
    }

    const SkullbonezCore::Core::SbResult captureResult = capture.SaveScreenshot( backend, PendingScreenshotPath() );

    if ( !captureResult.ok )
    {
        MarkCaptureFailed( captureResult.error.message );
        return;
    }

    MarkCaptureSaved();
}
