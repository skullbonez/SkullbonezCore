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
        EnsureParentDirectory( fileName );
        fopen_s( &f, fileName, "w" );
        if ( f )
        {
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
        va_list args;
        va_start( args, fmt );
        vfprintf( f, fmt, args );
        va_end( args );
        fflush( f );
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
