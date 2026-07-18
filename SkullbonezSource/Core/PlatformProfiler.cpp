/*
File: SkullbonezSource/Core/PlatformProfiler.cpp
Purpose:
  Bridges engine profiler markers to platform tools such as PIX when available.

Summary:
  PlatformProfiler.cpp bridges engine profiler markers to platform tools such
  as PIX when available. As an implementation unit, keep edits anchored on
  process-wide contracts, diagnostics, and validation-sensitive state and on
  the glossary/invariants below.

Glossary:

Invariants:
  - Platform profiler calls must be optional; engine profiling remains valid
    when PIX headers or runtime capture tooling are unavailable.
  - CPU range depth is thread-local so worker markers cannot corrupt main-thread
    begin/end nesting.

Related:
  - SkullbonezSource/Core/PlatformProfiler.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Common.h"
#include "Log.h"
#include "PlatformProfiler.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Core
{
namespace PlatformProfiler
{
namespace
{
std::atomic<bool> g_enabled{ false };
std::atomic<bool> g_detailedRangesEnabled{ false };
thread_local int g_cpuDepth = 0;

enum class MarkerDomain
{
    Frame,
    Physics,
    PostPhysics,
    Render,
    Shadows,
    UI,
    Replay,
    SoA,
    Sync,
    Fallback
};

enum class MarkerContext
{
    Default,
    Worker,
    Chunk,
    Record,
    Gpu
};

uint32_t HashRuntimeName( const char* name )
{
    // Concept: runtime-generated marker names use the same FNV-1a family as
    // compile-time profiler markers so colors remain stable across captures.
    uint32_t hash = 2166136261u;
    if ( !name )
    {
        return hash;
    }
    // Why: FNV hashes the unsigned character representation so bytes above
    // ASCII cannot sign-extend differently across compiler char defaults.
    for ( const unsigned char* p = reinterpret_cast<const unsigned char*>( name ); *p; ++p )
    {
        hash = ( hash ^ static_cast<uint32_t>( *p ) ) * 16777619u;
    }
    return hash;
}

bool HasPathPrefix( const char* name, const char* prefix )
{
    if ( !name || !prefix )
    {
        return false;
    }
    const std::size_t prefixLength = std::strlen( prefix );
    return std::strncmp( name, prefix, prefixLength ) == 0 &&
           ( name[prefixLength] == '\0' || name[prefixLength] == '/' );
}

bool EndsWith( const char* name, const char* suffix )
{
    if ( !name || !suffix )
    {
        return false;
    }
    const std::size_t nameLength = std::strlen( name );
    const std::size_t suffixLength = std::strlen( suffix );
    return suffixLength <= nameLength && std::strcmp( name + nameLength - suffixLength, suffix ) == 0;
}

MarkerDomain ClassifyDomain( const char* name )
{
    if ( !name )
    {
        return MarkerDomain::Fallback;
    }
    if ( std::strcmp( name, "Frame" ) == 0 )
    {
        return MarkerDomain::Frame;
    }
    if ( HasPathPrefix( name, "Frame/Physics" ) )
    {
        return MarkerDomain::Physics;
    }
    if ( HasPathPrefix( name, "Frame/PostPhysics" ) )
    {
        return MarkerDomain::PostPhysics;
    }
    if ( HasPathPrefix( name, "Frame/Shadows" ) )
    {
        return MarkerDomain::Shadows;
    }
    if ( HasPathPrefix( name, "Frame/Render" ) )
    {
        return MarkerDomain::Render;
    }
    if ( HasPathPrefix( name, "Frame/UI" ) )
    {
        return MarkerDomain::UI;
    }
    if ( HasPathPrefix( name, "Frame/Replay" ) )
    {
        return MarkerDomain::Replay;
    }
    if ( HasPathPrefix( name, "Frame/SoA" ) )
    {
        return MarkerDomain::SoA;
    }
    if ( HasPathPrefix( name, "Frame/VsyncWait" ) || HasPathPrefix( name, "Frame/PipelineSync" ) )
    {
        return MarkerDomain::Sync;
    }
    return HasPathPrefix( name, "Frame" ) ? MarkerDomain::Fallback : MarkerDomain::Fallback;
}

MarkerContext ClassifyContext( const char* name )
{
    if ( EndsWith( name, "_Worker" ) )
    {
        return MarkerContext::Worker;
    }
    if ( EndsWith( name, "_Chunk" ) )
    {
        return MarkerContext::Chunk;
    }
    if ( EndsWith( name, "_Record" ) )
    {
        return MarkerContext::Record;
    }
    if ( EndsWith( name, "_GPU" ) )
    {
        return MarkerContext::Gpu;
    }
    return MarkerContext::Default;
}

uint32_t BaseColorForDomain( MarkerDomain domain )
{
    switch ( domain )
    {
    case MarkerDomain::Frame:
        return 0xff9aa0a6u;
    case MarkerDomain::Physics:
        return 0xff2f80edu;
    case MarkerDomain::PostPhysics:
        return 0xff56ccf2u;
    case MarkerDomain::Render:
        return 0xffeb5757u;
    case MarkerDomain::Shadows:
        return 0xff9b51e0u;
    case MarkerDomain::UI:
        return 0xfff2c94cu;
    case MarkerDomain::Replay:
        return 0xff00b8a9u;
    case MarkerDomain::SoA:
        return 0xff27ae60u;
    case MarkerDomain::Sync:
        return 0xff6c757du;
    case MarkerDomain::Fallback:
    default:
        return 0xffb0bec5u;
    }
}

int ClampColorChannel( int value )
{
    return std::clamp( value, 0, 255 );
}

void ApplyBrightness( int& r, int& g, int& b, int delta )
{
    r = ClampColorChannel( r + delta );
    g = ClampColorChannel( g + delta );
    b = ClampColorChannel( b + delta );
}

void ScaleSaturation( int& r, int& g, int& b, int numerator, int denominator )
{
    const int gray = ( r + g + b ) / 3;
    r = ClampColorChannel( gray + ( r - gray ) * numerator / denominator );
    g = ClampColorChannel( gray + ( g - gray ) * numerator / denominator );
    b = ClampColorChannel( gray + ( b - gray ) * numerator / denominator );
}

uint64_t PackArgb( int r, int g, int b )
{
    return 0xff000000ull | ( static_cast<uint64_t>( ClampColorChannel( r ) ) << 16 ) |
           ( static_cast<uint64_t>( ClampColorChannel( g ) ) << 8 ) | static_cast<uint64_t>( ClampColorChannel( b ) );
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
    g_enabled.store( enabled && IsAvailable(), std::memory_order_relaxed );
    if ( !g_enabled.load( std::memory_order_relaxed ) )
    {
        g_detailedRangesEnabled.store( false, std::memory_order_relaxed );
    }
}

bool IsEnabled()
{
    return g_enabled.load( std::memory_order_relaxed );
}

void SetDetailedRangesEnabled( bool enabled )
{
    g_detailedRangesEnabled.store( enabled && IsEnabled(), std::memory_order_relaxed );
}

bool AreDetailedRangesEnabled()
{
    return g_detailedRangesEnabled.load( std::memory_order_relaxed );
}

uint64_t ColorForMarker( const char* name, uint32_t hash )
{
    const uint32_t h = hash != 0 ? hash : HashRuntimeName( name );
    if ( !AreDetailedRangesEnabled() )
    {
        const uint8_t r = static_cast<uint8_t>( 72u + ( h & 0x7Fu ) );
        const uint8_t g = static_cast<uint8_t>( 72u + ( ( h >> 8 ) & 0x7Fu ) );
        const uint8_t b = static_cast<uint8_t>( 72u + ( ( h >> 16 ) & 0x7Fu ) );
        return 0xff000000ull | ( static_cast<uint64_t>( r ) << 16 ) | ( static_cast<uint64_t>( g ) << 8 ) |
               static_cast<uint64_t>( b );
    }

    const char* markerName = name ? name : "(null)";
    const uint32_t base = BaseColorForDomain( ClassifyDomain( markerName ) );
    int r = static_cast<int>( ( base >> 16 ) & 0xffu );
    int g = static_cast<int>( ( base >> 8 ) & 0xffu );
    int b = static_cast<int>( base & 0xffu );

    switch ( ClassifyContext( markerName ) )
    {
    case MarkerContext::Worker:
        ScaleSaturation( r, g, b, 3, 4 );
        ApplyBrightness( r, g, b, -16 );
        break;
    case MarkerContext::Chunk:
        ApplyBrightness( r, g, b, 18 );
        break;
    case MarkerContext::Record:
        ScaleSaturation( r, g, b, 4, 5 );
        ApplyBrightness( r, g, b, -6 );
        break;
    case MarkerContext::Gpu:
        ScaleSaturation( r, g, b, 6, 5 );
        ApplyBrightness( r, g, b, 10 );
        break;
    case MarkerContext::Default:
    default:
        break;
    }

    const int variation = static_cast<int>( ( h >> 24 ) & 0x0fu ) - 8;
    ApplyBrightness( r, g, b, variation );
    return PackArgb( r, g, b );
}

const char* DecorateMarkerName( const char* name, const char* suffix, char* buffer, std::size_t bufferSize )
{
    const char* markerName = name ? name : "(null)";
    if ( !buffer || bufferSize == 0 )
    {
        return markerName;
    }

    const char* markerSuffix = suffix ? suffix : "";
    _snprintf_s( buffer, bufferSize, _TRUNCATE, "%s%s", markerName, markerSuffix );
    return buffer;
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
#if SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
    if ( g_cpuDepth <= 0 )
    {
        if ( IsEnabled() )
        {
            Log().WriteEventf( "platform_profiler_cpu_end_without_begin" );
        }
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
} // namespace Core
} // namespace SkullbonezCore
