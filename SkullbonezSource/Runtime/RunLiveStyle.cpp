/*
File: SkullbonezSource/Runtime/RunLiveStyle.cpp
Purpose:
  Applies live style-harness updates without restarting physics or scene state.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Lane R result: Recoverable style-load failure reported to the control status
    file and stderr while the run stays alive.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Live style polling is opt-in and style-only; it must not reload scene
    physics or replace runtime-owned bodies.
  - Control file paths are resolved once from the configured directory so
    relative automation behaves the same in validation and interactive runs.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "Scene/SceneRuntimeStyle.h"
#include <cstdio>
#include <cstring>


using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

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
    while ( len > 0 &&
            ( text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t' ) )
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


void LiveStyleController::Tick( SceneRuntimeStyleContext context )
{
    if ( !m_enabled )
    {
        return;
    }

    const uint64_t styleStamp = FileStamp( m_stylePath );
    if ( styleStamp != 0 && styleStamp != m_styleStamp )
    {
        m_styleStamp = styleStamp;
        TestScene styleScene;
        const SbResult loadResult = TestScene::TryLoadStyleFromFile( m_stylePath, context.assets, styleScene );
        if ( loadResult.ok )
        {
            ApplyLiveStyleScene( context, styleScene );
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
                JoinControlPath( m_directory,
                                 requestedPath,
                                 m_pendingScreenshotPath,
                                 sizeof( m_pendingScreenshotPath ) );
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


void Run::TickLiveStyleControl()
{
    m_liveStyle.Tick( SceneRuntimeStyleContext{ m_launchOptions,
                                                SceneState(),
                                                m_sceneController.Browser(),
                                                m_cGameModelCollection,
                                                m_systems.assets,
                                                RuntimeActiveCinematicConfig( SceneState(), m_config ),
                                                m_defaultCinematicRender } );
}


void Run::TickLiveStyleControlCapture()
{
    if ( !m_liveStyle.HasPendingCapture() )
    {
        return;
    }

    SaveScreenshot( m_liveStyle.PendingScreenshotPath() );
    m_liveStyle.MarkCaptureSaved();
}
