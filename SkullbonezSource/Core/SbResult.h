/*
File: SkullbonezSource/Core/SbResult.h
Purpose:
  Declares the minimal Lane R recoverable-result carrier.

Summary:
  Lane R covers failures caused by external input such as scene files, assets,
  editor commands, automation scripts, or device/environment limits. The
  operation reports an owner and bounded message; the app stays alive.

Glossary:
  Lane R: Recoverable result error handling lane for input/environment failure.
  Result carrier: Small value that contains either success or a bounded error.
  Success sentinel: The empty owner and leading message byte that make a
    successful result's diagnostic strings observably empty.

Invariants:
  - Failure messages are bounded and stored inline.
  - SbResult has no heap ownership and no exception behavior.
  - Success initializes only the observable empty-string sentinels; it does not
    clear the failure-only tail of the 512-byte message buffer.
  - Copy and move operations branch on ok: success copies only the sentinels;
    failure copies the completely initialized inline diagnostic.
  - Callers must inspect or explicitly retain every result; silent discard is a
    compiler diagnostic.
  - Add an expected-like value payload only when a caller actually needs one.

Representation ruling:
  The 512-byte failure capacity is retained because current bounded-message
  coverage requires all 511 payload bytes. Keeping the buffer inline prevents a
  dangling diagnostic, while sentinel-only success construction avoids paying
  to clear failure-only storage on frame-reachable success paths. On Win64 the
  carrier remains 528 bytes; return-value elision writes the sentinels directly
  into caller storage.

Related:
  - SkullbonezSource/Core/FatalError.h
  - AGENTS.md (Error Handling Policy)
*/
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstring>


namespace SkullbonezCore
{
namespace Core
{
struct SbError
{
    SbError() noexcept : owner( "" )
    {
        message[0] = '\0';
    }

    const char* owner;
    char message[512];
};


struct [[nodiscard]] SbResult
{
    bool ok = true;
    SbError error;

    SbResult() noexcept = default;

    SbResult( const SbResult& source ) noexcept : ok( source.ok )
    {
        CopyErrorFrom( source );
    }

    SbResult& operator=( const SbResult& source ) noexcept
    {

        if ( this != &source )
        {
            ok = source.ok;
            CopyErrorFrom( source );
        }

        return *this;
    }

    SbResult( SbResult&& source ) noexcept : SbResult( source )
    {
    }

    SbResult& operator=( SbResult&& source ) noexcept
    {
        return operator=( source );
    }

    static SbResult Success()
    {
        return {};
    }

    static SbResult Failure( const char* owner, const char* format, ... )
    {
        SbResult result;
        result.ok = false;
        result.error.owner = owner ? owner : "";
        std::memset( result.error.message, 0, sizeof( result.error.message ) );

        va_list args;
        va_start( args, format );
        std::vsnprintf( result.error.message, sizeof( result.error.message ),
                        format ? format : "recoverable operation failed", args );
        va_end( args );
        return result;
    }

  private:
    void CopyErrorFrom( const SbResult& source ) noexcept
    {

        if ( source.ok )
        {
            error.owner = "";
            error.message[0] = '\0';
            return;
        }

        error.owner = source.error.owner;
        std::memcpy( error.message, source.error.message, sizeof( error.message ) );
    }
};
} // namespace Core
} // namespace SkullbonezCore
