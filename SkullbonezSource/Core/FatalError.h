/*
File: SkullbonezSource/Core/FatalError.h
Purpose:
  Declares the Lane F fatal-invariant helper and SB_FATAL macro.

Summary:
  Lane F is for programmer invariants that must never be false in running
  engine logic. It logs the owner and formatted diagnostics, flushes, then
  terminates the process instead of throwing through gameplay or render paths.

Glossary:
  Lane F: Fatal invariant error handling lane for should-never-happen state.
  Fatal invariant: Program state that indicates an engine bug rather than
    recoverable user, file, device, or automation input.

Invariants:
  - SB_FATAL never throws and never returns.
  - Owner strings name the subsystem responsible for triage.
  - Do not include Common.h here; Common.h is being split, not grown.

Related:
  - SkullbonezSource/Core/FatalError.cpp
  - SkullbonezSource/Core/SbResult.h
  - AGENTS.md (Error Handling Policy)
*/
#pragma once


namespace SkullbonezCore
{
namespace Basics
{
[[noreturn]] void SbFatal( const char* owner, const char* format, ... );
} // namespace Basics
} // namespace SkullbonezCore


// Concept: SB_FATAL is the single Lane F spelling for new fatal invariants.
//
// Callers supply an owner plus printf-style diagnostics. The implementation
// owns logging, debug/profile breaking, flushing, and process termination.
#define SB_FATAL( owner, ... ) ::SkullbonezCore::Basics::SbFatal( ( owner ), __VA_ARGS__ )
