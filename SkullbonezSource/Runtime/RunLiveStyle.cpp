/*
File: SkullbonezSource/Runtime/RunLiveStyle.cpp
Purpose:
  Applies live style-harness updates without restarting physics or scene state.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
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
#include <stdexcept>


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


void WriteStatus( const RunLiveStyleControlState& state, const char* status, const char* detail )
{
    if ( !state.enabled || state.statusPath[0] == '\0' )
    {
        return;
    }

    FILE* file = nullptr;
    const errno_t err = fopen_s( &file, state.statusPath, "w" );
    if ( err != 0 || !file )
    {
        return;
    }

    fprintf( file, "status %s\n", status ? status : "unknown" );
    fprintf( file, "detail %s\n", detail ? detail : "" );
    fprintf( file, "style_applies %d\n", state.styleApplyCount );
    fprintf( file, "captures %d\n", state.captureCount );
    fprintf( file, "live_style %s\n", state.stylePath );
    fprintf( file, "capture_control %s\n", state.capturePath );
    fclose( file );
}
} // namespace


void Run::SetLiveStyleControlDirectory( const char* path )
{
    if ( !path || path[0] == '\0' )
    {
        return;
    }

    strcpy_s( m_liveStyle.directory, sizeof( m_liveStyle.directory ), path );
    JoinControlPath( m_liveStyle.directory, "live.style.json", m_liveStyle.stylePath, sizeof( m_liveStyle.stylePath ) );
    JoinControlPath( m_liveStyle.directory, "capture.txt", m_liveStyle.capturePath, sizeof( m_liveStyle.capturePath ) );
    JoinControlPath( m_liveStyle.directory, "status.txt", m_liveStyle.statusPath, sizeof( m_liveStyle.statusPath ) );
    m_liveStyle.styleStamp = 0;
    m_liveStyle.captureStamp = FileStamp( m_liveStyle.capturePath );
    m_liveStyle.pendingScreenshotPath[0] = '\0';
    m_liveStyle.hasPendingScreenshot = false;
    m_liveStyle.styleApplyCount = 0;
    m_liveStyle.captureCount = 0;
    m_liveStyle.enabled = true;

    m_launchOptions.interactiveSceneRun = true;
    EnterInteractiveSceneRun();
    WriteStatus( m_liveStyle, "ready", "watching live.style.json" );
    printf( "[style-harness] Watching %s\n", m_liveStyle.directory );
}


void Run::TickLiveStyleControl()
{
    if ( !m_liveStyle.enabled )
    {
        return;
    }

    const uint64_t styleStamp = FileStamp( m_liveStyle.stylePath );
    if ( styleStamp != 0 && styleStamp != m_liveStyle.styleStamp )
    {
        m_liveStyle.styleStamp = styleStamp;
        try
        {
            const TestScene styleScene = TestScene::LoadStyleFromFile( m_liveStyle.stylePath );
            ApplyLiveStyleScene( SceneRuntimeStyleContext{ m_launchOptions,
                                                           SceneState(),
                                                           m_sceneBrowser,
                                                           m_cGameModelCollection,
                                                           RuntimeActiveCinematicConfig( SceneState(), Cfg() ),
                                                           m_defaultCinematicRender },
                                 styleScene );
            ++m_liveStyle.styleApplyCount;
            WriteStatus( m_liveStyle, "style_applied", m_liveStyle.stylePath );
            printf( "[style-harness] Applied %s\n", m_liveStyle.stylePath );
        }
        catch ( const std::exception& e )
        {
            WriteStatus( m_liveStyle, "style_error", e.what() );
            fprintf( stderr, "[style-harness] Style error: %s\n", e.what() );
        }
    }

    const uint64_t captureStamp = FileStamp( m_liveStyle.capturePath );
    if ( captureStamp != 0 && captureStamp != m_liveStyle.captureStamp )
    {
        m_liveStyle.captureStamp = captureStamp;

        char requestedPath[512] = {};
        if ( ReadCaptureRequest( m_liveStyle.capturePath, requestedPath, sizeof( requestedPath ) ) )
        {
            if ( IsAbsolutePath( requestedPath ) )
            {
                strcpy_s( m_liveStyle.pendingScreenshotPath,
                          sizeof( m_liveStyle.pendingScreenshotPath ),
                          requestedPath );
            }
            else
            {
                JoinControlPath( m_liveStyle.directory,
                                 requestedPath,
                                 m_liveStyle.pendingScreenshotPath,
                                 sizeof( m_liveStyle.pendingScreenshotPath ) );
            }
            m_liveStyle.hasPendingScreenshot = true;
            WriteStatus( m_liveStyle, "capture_pending", m_liveStyle.pendingScreenshotPath );
        }
        else
        {
            WriteStatus( m_liveStyle, "capture_ignored", "capture.txt contains no screenshot path" );
        }
    }
}


void Run::TickLiveStyleControlCapture()
{
    if ( !m_liveStyle.enabled || !m_liveStyle.hasPendingScreenshot )
    {
        return;
    }

    SaveScreenshot( m_liveStyle.pendingScreenshotPath );
    ++m_liveStyle.captureCount;
    WriteStatus( m_liveStyle, "capture_saved", m_liveStyle.pendingScreenshotPath );
    printf( "[style-harness] Captured %s\n", m_liveStyle.pendingScreenshotPath );
    m_liveStyle.pendingScreenshotPath[0] = '\0';
    m_liveStyle.hasPendingScreenshot = false;
}
