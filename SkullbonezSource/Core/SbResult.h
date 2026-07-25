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

Invariants:
  - Failure messages are bounded and stored inline.
  - SbResult has no heap ownership and no exception behavior.
  - Callers must inspect or explicitly retain every result; silent discard is a
    compiler diagnostic.
  - Add an expected-like value payload only when a caller actually needs one.

Related:
  - SkullbonezSource/Core/FatalError.h
  - AGENTS.md (Error Handling Policy)
*/
#pragma once

#include <cstdarg>
#include <cstdio>


namespace SkullbonezCore
{
namespace Core
{
struct SbError
{
    const char* owner = "";
    char message[512] = {};
};


struct [[nodiscard]] SbResult
{
    bool ok = true;
    SbError error;

    static SbResult Success()
    {
        return {};
    }

    static SbResult Failure( const char* owner, const char* format, ... )
    {
        SbResult result;
        result.ok = false;
        result.error.owner = owner ? owner : "";

        va_list args;
        va_start( args, format );
        std::vsnprintf( result.error.message,
                        sizeof( result.error.message ),
                        format ? format : "recoverable operation failed",
                        args );
        va_end( args );
        return result;
    }
};
} // namespace Core
} // namespace SkullbonezCore
