/*
File: SkullbonezSource/SkullbonezLog.cpp
Purpose:
  Writes debug-only runtime, crash, and diagnostics logs.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  SkullScope: Queryable physics diagnostics workflow backed by bounded trace
  output and local queries.
  CSV (Comma-Separated Values): Text table format used for byte-exact physics
  regression output.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/SkullbonezLog.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezLog.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

#ifdef _DEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace SkullbonezCore::Basics;


SkullbonezLog& SkullbonezLog::Get()
{
    static SkullbonezLog s_instance;
    return s_instance;
}


#ifdef _DEBUG

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


const char* SkullbonezLog::EventLogPath()
{
    return "Debug/runtime_events.log";
}


FILE* SkullbonezLog::OpenLog( const char* fileName )
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


void SkullbonezLog::Writef( const char* fileName, const char* fmt, ... )
{
    FILE* f = OpenLog( fileName );

    if ( f )
    {
        // Intentionally no fflush here. Hot diagnostic paths call Writef many
        // times per frame; flushing each row makes SkullScope trace generation
        // dominated by I/O. Callers that need durable output at a boundary use
        // FlushAll(), and the logger destructor flushes/closes every file.
        va_list args;
        va_start( args, fmt );
        vfprintf( f, fmt, args );
        va_end( args );
    }
}


void SkullbonezLog::WriteEventf( const char* fmt, ... )
{
    char message[2048] = {};
    va_list args;
    va_start( args, fmt );
    vsnprintf_s( message, sizeof( message ), _TRUNCATE, fmt, args );
    va_end( args );

    SYSTEMTIME now = {};
    GetLocalTime( &now );

    char line[2304] = {};
    snprintf( line,
              sizeof( line ),
              "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
              now.wYear,
              now.wMonth,
              now.wDay,
              now.wHour,
              now.wMinute,
              now.wSecond,
              now.wMilliseconds,
              message );

    OutputDebugStringA( line );

    FILE* f = OpenLog( EventLogPath() );
    if ( f )
    {
        fputs( line, f );
        fflush( f );
    }
}


void SkullbonezLog::FlushAll()
{
    for ( auto& [name, file] : m_logs )
    {
        if ( file )
        {
            fflush( file );
        }
    }
}


SkullbonezLog::~SkullbonezLog()
{
    for ( auto& [name, file] : m_logs )
    {
        if ( file )
        {
            fclose( file );
        }
    }
}

#else

void SkullbonezLog::Writef( const char*, const char*, ... )
{
}
void SkullbonezLog::WriteEventf( const char*, ... )
{
}
void SkullbonezLog::FlushAll()
{
}
const char* SkullbonezLog::EventLogPath()
{
    return "";
}
SkullbonezLog::~SkullbonezLog()
{
}

#endif
