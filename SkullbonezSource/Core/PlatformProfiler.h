/*
File: SkullbonezSource/Core/PlatformProfiler.h
Purpose:
  Bridges engine profiler markers to platform tools such as PIX when available.

Summary:
  PlatformProfiler exposes tool-neutral marker domains and bounded decoration
  helpers, so callers emit the same profiling structure whether PIX is present
  or absent.

Glossary:
  Marker domain: Stable engine category used to group profiler ranges without
    exposing tool-specific state to callers.
  Decorated marker: Bounded label that adds worker/chunk context to a caller's
    original profiler name.

Invariants:
  - All functions are safe to call when platform profiling is unavailable; they
    become no-ops or deterministic helpers.
  - Decorated marker names must fit caller-provided buffers and remain
    null-terminated.

Related:
  - SkullbonezSource/Core/PlatformProfiler.cpp
  - SkullbonezSource/Core/PlatformWin32.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "PlatformWin32.h"

#include <cstddef>
#include <cstdint>

#if defined( SKULLBONEZ_PROFILE_ENABLED ) && defined( _WIN32 ) && defined( SKULLBONEZ_PLATFORM_PROFILER_PIX ) &&            \
    defined( __has_include )
#if __has_include( <pix3.h> )
#define SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3 1
#include <pix3.h>
#endif
#endif

#ifndef SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3
#define SKULLBONEZ_PLATFORM_PROFILER_HAVE_PIX3 0
#endif

namespace SkullbonezCore
{
namespace Core
{
namespace PlatformProfiler
{

bool IsAvailable();
void SetEnabled( bool enabled );
bool IsEnabled();
void SetDetailedRangesEnabled( bool enabled );
bool AreDetailedRangesEnabled();

static constexpr std::size_t MAX_DECORATED_MARKER_NAME_CHARS = 256;

uint64_t ColorForMarker( const char* name, uint32_t hash );
const char* DecorateMarkerName( const char* name, const char* suffix, char* buffer, std::size_t bufferSize );

void CpuBegin( const char* name, uint32_t hash );
void CpuEnd();

} // namespace PlatformProfiler
} // namespace Core
} // namespace SkullbonezCore
