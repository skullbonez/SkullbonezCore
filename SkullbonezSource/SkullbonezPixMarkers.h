#pragma once

#include <cstdint>

#if defined( SKULLBONEZ_PROFILE_ENABLED ) && defined( SKULLBONEZ_PIX_ENABLED ) && defined( __has_include )
#if __has_include( <pix3.h> )
#define SKULLBONEZ_PIX_MARKERS_HAVE_PIX3 1
#include <pix3.h>
#endif
#endif

#ifndef SKULLBONEZ_PIX_MARKERS_HAVE_PIX3
#define SKULLBONEZ_PIX_MARKERS_HAVE_PIX3 0
#endif

namespace SkullbonezCore
{
namespace Basics
{
namespace PixMarkers
{

bool IsAvailable();
void SetEnabled( bool enabled );
bool IsEnabled();

uint64_t ColorForMarker( const char* name, uint32_t hash );

void CpuBegin( const char* name, uint32_t hash );
void CpuEnd();
void CpuMarker( const char* name, uint32_t hash );

} // namespace PixMarkers
} // namespace Basics
} // namespace SkullbonezCore
