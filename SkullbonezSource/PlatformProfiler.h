/*
File: SkullbonezSource/PlatformProfiler.h
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
  - SkullbonezSource/PlatformProfiler.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include <cstdint>

#if defined( SKULLBONEZ_PROFILE_ENABLED ) && defined( _WIN32 ) && defined( SKULLBONEZ_PLATFORM_PROFILER_PIX ) &&       \
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
namespace Basics
{
namespace PlatformProfiler
{

bool IsAvailable();
void SetEnabled( bool enabled );
bool IsEnabled();

uint64_t ColorForMarker( const char* name, uint32_t hash );

void CpuBegin( const char* name, uint32_t hash );
void CpuEnd();
void CpuMarker( const char* name, uint32_t hash );

} // namespace PlatformProfiler
} // namespace Basics
} // namespace SkullbonezCore
