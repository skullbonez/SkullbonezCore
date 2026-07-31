/*
File: SkullbonezSource/Core/Log.cpp
Purpose:
  Writes debug-only runtime, crash, and diagnostics logs.

Summary:
  Writes debug-only runtime, crash, and diagnostics logs.

Invariants:
  - Debug logs are opened in binary mode so newline bytes stay byte-exact for
    validation artifacts.
  - Release builds keep the logging API callable but compile the side effects
    away.

Related:
  - SkullbonezSource/Core/Log.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG )
#include "PlatformWin32.h"
#endif

using namespace SkullbonezCore::Core;


EngineLog& EngineLog::Get()
{
    static EngineLog s_instance;
    return s_instance;
}


#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG )

namespace
{
constexpr size_t DEBUG_LOG_BUFFER_BYTES = 8u * 1024u * 1024u;

void EnsureParentDirectory( const char* fileName )
{

    if ( !fileName )
    {
        return;
    }

    char directory[MAX_PATH] = {};
    strcpy_s( directory, sizeof( directory ), fileName );

    char* slash = strrchr( directory, '/' );
    char* backslash = strrchr( directory, '\\' );
    char* separator = slash;

    if ( backslash && ( !separator || backslash > separator ) )
    {
        separator = backslash;
    }

    if ( separator )
    {
        *separator = '\0';

        if ( directory[0] != '\0' )
        {
            CreateDirectoryA( directory, nullptr );
        }
    }
}
} // namespace


const char* EngineLog::EventLogPath()
{
    return "Debug/runtime_events.log";
}


FILE* EngineLog::OpenLog( const char* fileName )
{
    FILE* f = nullptr;
    auto it = m_logs.find( fileName );

    if ( it == m_logs.end() )
    {

        // Open debug logs in binary mode so '\n' is written exactly as LF on
        // Windows. Physics regression CSVs are intended to be byte-exact
        // validation artifacts; text mode silently expands '\n' to CRLF and can
        // make data-identical files differ at the byte level.
        EnsureParentDirectory( fileName );
        fopen_s( &f, fileName, "wb" );

        if ( f )
        {

            // SkullScope and physics CSV logging can emit thousands of small
            // rows per run. Give the CRT a large user-space buffer so those rows
            // batch in memory instead of forcing tiny disk writes from the hot
            // physics loop. Event logs still flush explicitly in WriteEventf().
            setvbuf( f, nullptr, _IOFBF, DEBUG_LOG_BUFFER_BYTES );
            m_logs[fileName] = f;
        }
    }
    else
    {
        f = it->second;
    }

    return f;
}


void EngineLog::Writef( const char* fileName, const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    WriteVf( fileName, fmt, args );
    va_end( args );
}


void EngineLog::WriteVf( const char* fileName, const char* fmt, va_list args )
{

    // Why: the lock covers both lazy handle lookup and the CRT write. FILE
    // streams do not become safe merely because their owning map is guarded.
    std::lock_guard<std::mutex> lock( m_logMutex );
    FILE* f = OpenLog( fileName );

    if ( f )
    {

        // Intentionally no fflush here. Hot diagnostic paths call Writef many
        // times per frame; flushing each row makes SkullScope trace generation
        // dominated by I/O. Callers that need durable output at a boundary use
        // FlushAll(), and the logger destructor flushes/closes every file.
        vfprintf( f, fmt, args );
    }
}


void EngineLog::WriteEventf( const char* fmt, ... )
{
    char message[2048] = {};
    va_list args;
    va_start( args, fmt );
    vsnprintf_s( message, sizeof( message ), _TRUNCATE, fmt, args );
    va_end( args );

    SYSTEMTIME now = {};
    GetLocalTime( &now );

    char line[2304] = {};
    snprintf( line, sizeof( line ), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n", now.wYear, now.wMonth, now.wDay, now.wHour,
              now.wMinute, now.wSecond, now.wMilliseconds, message );

    OutputDebugStringA( line );

    std::lock_guard<std::mutex> lock( m_logMutex );
    FILE* f = OpenLog( EventLogPath() );

    if ( f )
    {
        fputs( line, f );
        fflush( f );
    }
}


void EngineLog::FlushAll()
{
    std::lock_guard<std::mutex> lock( m_logMutex );

    for ( auto& [name, file] : m_logs )
    {

        if ( file )
        {
            fflush( file );
        }
    }
}

#if defined( SKULLBONEZ_TEST_ENGINE_LOG )
void EngineLog::CloseAllForTests()
{
    std::lock_guard<std::mutex> lock( m_logMutex );

    for ( auto& [name, file] : m_logs )
    {

        if ( file )
        {
            fclose( file );
        }
    }

    m_logs.clear();
}
#endif


EngineLog::~EngineLog()
{
    std::lock_guard<std::mutex> lock( m_logMutex );

    for ( auto& [name, file] : m_logs )
    {

        if ( file )
        {
            fclose( file );
        }
    }
}

#else

void EngineLog::Writef( const char*, const char*, ... )
{
}
void EngineLog::WriteVf( const char*, const char*, va_list )
{
}
void EngineLog::WriteEventf( const char*, ... )
{
}
void EngineLog::FlushAll()
{
}
const char* EngineLog::EventLogPath()
{
    return "";
}
EngineLog::~EngineLog()
{
}

#endif
