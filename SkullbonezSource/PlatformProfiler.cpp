/*
File: SkullbonezSource/PlatformProfiler.cpp
Purpose:
  Bridges engine profiler markers to platform tools such as PIX when available.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/PlatformProfiler.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Common.h"
#include "PlatformProfiler.h"

#if defined( SKULLBONEZ_PROFILE_ENABLED )
#include <cstring>
#endif

namespace SkullbonezCore
{
namespace Basics
{
namespace PlatformProfiler
{
namespace
{
bool g_enabled = false;
int g_cpuDepth = 0;

uint32_t HashRuntimeName( const char* name )
{
    uint32_t hash = 2166136261u;
    if ( !name )
    {
        return hash;
    }
    for ( const unsigned char* p = reinterpret_cast<const unsigned char*>( name ); *p; ++p )
    {
        hash = ( hash ^ static_cast<uint32_t>( *p ) ) * 16777619u;
    }
    return hash;
}
} // namespace

bool IsAvailable()
{
#if defined( SKULLBONEZ_PROFILE_ENABLED ) && SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    return true;
#else
    return false;
#endif
}

void SetEnabled( bool enabled )
{
    g_enabled = enabled && IsAvailable();
    if ( !g_enabled )
    {
        g_cpuDepth = 0;
    }
}

bool IsEnabled()
{
    return g_enabled;
}

uint64_t ColorForMarker( const char* name, uint32_t hash )
{
    const uint32_t h = hash != 0 ? hash : HashRuntimeName( name );
    const uint8_t r = static_cast<uint8_t>( 72u + ( h & 0x7Fu ) );
    const uint8_t g = static_cast<uint8_t>( 72u + ( ( h >> 8 ) & 0x7Fu ) );
    const uint8_t b = static_cast<uint8_t>( 72u + ( ( h >> 16 ) & 0x7Fu ) );
    return 0xff000000ull | ( static_cast<uint64_t>( r ) << 16 ) | ( static_cast<uint64_t>( g ) << 8 ) |
           static_cast<uint64_t>( b );
}

void CpuBegin( const char* name, uint32_t hash )
{
    if ( !IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    const char* markerName = name ? name : "(null)";
    PIXBeginEvent( ColorForMarker( markerName, hash ), "%s", markerName );
    ++g_cpuDepth;
#else
    (void)name;
    (void)hash;
#endif
}

void CpuEnd()
{
    if ( !IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( g_cpuDepth <= 0 )
    {
        Log().WriteEventf( "platform_profiler_cpu_end_without_begin" );
        return;
    }
    PIXEndEvent();
    --g_cpuDepth;
#endif
}

void CpuMarker( const char* name, uint32_t hash )
{
    if ( !IsEnabled() )
    {
        return;
    }

#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    const char* markerName = name ? name : "(null)";
    PIXSetMarker( ColorForMarker( markerName, hash ), "%s", markerName );
#else
    (void)name;
    (void)hash;
#endif
}

} // namespace PlatformProfiler
} // namespace Basics
} // namespace SkullbonezCore
