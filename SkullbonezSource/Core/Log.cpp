/*
File: SkullbonezSource/Core/Log.cpp
Purpose:
  Writes developer and Automation runtime, crash, and diagnostics logs.

Summary:
  This implementation owns diagnostic-build file handles and byte-exact writes
  for runtime, crash, and diagnostic channels; shipping builds retain a no-op API.

Invariants:
  - Diagnostic logs are opened in binary mode so newline bytes stay byte-exact for
    validation artifacts.
  - Release builds keep the logging API callable but compile the side effects
    away.

Related:
  - SkullbonezSource/Core/Log.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Log.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG ) || defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
#include "PlatformWin32.h"
#include <share.h>
#endif

using namespace SkullbonezCore::Core;


EngineLog& EngineLog::Get()
{
    static EngineLog s_instance;
    return s_instance;
}


#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG ) || defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )

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
        // Why: diagnostics are a live observation surface. Denying neither
        // reads nor writes lets a local query process tail complete flushed
        // rows while EngineLog retains the producing handle.
        f = _fsopen( fileName, "wb", _SH_DENYNO );

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


void EngineLog::ResetLog( const char* fileName )
{
    // Why: OpenLog only truncates ("wb") on the first open of a path and then
    // retains the handle. A same-process re-run therefore appends onto the prior
    // run. Closing and erasing the handle here makes the next OpenLog reopen in
    // truncate mode, so each run owns exactly one complete file. Guarded by the
    // same mutex as OpenLog because it mutates the shared handle map.
    std::lock_guard<std::mutex> lock( m_logMutex );
    auto it = m_logs.find( fileName );

    if ( it != m_logs.end() )
    {
        if ( it->second )
        {
            fclose( it->second );
        }

        m_logs.erase( it );
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
void EngineLog::ResetLog( const char* )
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
