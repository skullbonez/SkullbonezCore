#include "SkullbonezLog.h"

#include <cstdio>
#include <cstdarg>

using namespace SkullbonezCore::Basics;


SkullbonezLog& SkullbonezLog::Get()
{
    static SkullbonezLog s_instance;
    return s_instance;
}


#ifdef _DEBUG

void SkullbonezLog::Writef( const char* fileName, const char* fmt, ... )
{
    FILE* f = nullptr;
    auto it = m_logs.find( fileName );
    if ( it == m_logs.end() )
    {
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

    if ( f )
    {
        va_list args;
        va_start( args, fmt );
        vfprintf( f, fmt, args );
        va_end( args );
        fflush( f );
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
SkullbonezLog::~SkullbonezLog()
{
}

#endif
