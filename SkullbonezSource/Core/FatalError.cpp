/*
File: SkullbonezSource/Core/FatalError.cpp
Purpose:
  Implements the Lane F fatal-invariant termination path.

Mental model:
  Fatal invariants are not recoverable input failures. The useful behavior is
  to capture a compact owner/message record, flush it, and stop immediately so
  validation and crash triage see the first broken invariant.

Glossary:
  Lane F: Fatal invariant error handling lane for should-never-happen state.
  Profile build: Optimized validation build with SKULLBONEZ_PROFILE_ENABLED.

Invariants:
  - SbFatal never throws and never returns.
  - The formatted message is bounded before it reaches logs or stderr.
  - Debug/Profile builds break before aborting so an attached debugger stops at
    the owning invariant rather than at the CRT abort frame.

Related:
  - SkullbonezSource/Core/FatalError.h
  - SkullbonezSource/Core/Log.h
  - AGENTS.md (Error Handling Policy)
*/
#include "FatalError.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if ( defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) ) && defined( _MSC_VER )
#include <intrin.h>
#endif


namespace SkullbonezCore
{
namespace Basics
{
namespace
{
const char* SafeText( const char* text )
{
    return text ? text : "";
}
} // namespace


[[noreturn]] void SbFatal( const char* owner, const char* format, ... )
{
    char message[1024] = {};
    va_list args;
    va_start( args, format );
    std::vsnprintf( message, sizeof( message ), format ? format : "fatal invariant failed", args );
    va_end( args );

    const char* safeOwner = SafeText( owner );
    EngineLog::Get().WriteEventf( "fatal owner=\"%s\" message=\"%s\"", safeOwner, message );
    std::fprintf( stderr, "FATAL[%s]: %s\n", safeOwner, message );
    std::fflush( stderr );
    EngineLog::Get().FlushAll();

#if ( defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) ) && defined( _MSC_VER )
    __debugbreak();
#endif

    std::abort();
}
} // namespace Basics
} // namespace SkullbonezCore
