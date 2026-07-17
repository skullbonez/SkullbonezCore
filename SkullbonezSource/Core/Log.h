/*
File: SkullbonezSource/Core/Log.h
Purpose:
  Writes debug-only runtime, crash, and diagnostics logs.

Summary:
  Log.h writes debug-only runtime, crash, and diagnostics logs. As a public
  header, keep edits anchored on process-wide contracts, diagnostics, and
  validation-sensitive state and on the glossary/invariants below.

Glossary:
  Engine log: Process-wide debug/test owner that lazily opens and retains
  diagnostic FILE handles.
  Lane F: Fatal invariant path that must record diagnostics from any thread
  before terminating.

Invariants:
  - File handles are opened lazily and owned by EngineLog until process exit or
    FlushAll teardown.
  - Debug/test logging serializes map and FILE access so a worker-side Lane F
    diagnostic cannot race an ordinary main-thread write or flush.
  - Release builds keep the interface shape but carry no FILE handle state.
  - EngineLog::Get is the sole sanctioned cold/fatal magic static. It must not
    become a frame-service locator or be resolved from ordinary hot loops.

Related:
  - SkullbonezSource/Core/Log.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdarg>

#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG )
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#endif


namespace SkullbonezCore
{
namespace Core
{
/* -- EngineLog
----------------------------------------------------------------------------------------------------------------------------------------------

    Debug-only singleton logger.  Maps file names to open FILE handles so the caller never
    needs to open, close, or flush anything — just call Writef() and the rest is automatic.

    In Release builds every method is an inline no-op and the class has no data members,
    so the compiler eliminates all call sites completely.

    Usage (include Log.h in each file that writes diagnostics):

        Log().Writef( "Debug/physics.csv", "terrain,%d,%.2f,%.2f\n", frame, x, y );
        Log().WriteEventf( "scene_started index=%d path=\"%s\"", index, path );

    The file is created on the first Writef() for that name.  Subsequent calls to the same
    name append to the already-open handle.  Writef() uses a generous file buffer so hot
    diagnostic paths can emit many rows without forcing a disk flush on every row.  Event
    logs still flush immediately.  All files are flushed and closed when the process exits.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class EngineLog
{

  public:
    static EngineLog& Get();

    void Writef( const char* fileName, const char* fmt, ... );
    // Caller contract: forwards one active va_list without taking ownership of
    // its lifetime; the caller still owns va_end.
    void WriteVf( const char* fileName, const char* fmt, va_list args );
    void WriteEventf( const char* fmt, ... );
    void FlushAll();
#if defined( SKULLBONEZ_TEST_ENGINE_LOG )
    // Test-only cold boundary: closes retained handles after a concurrency
    // probe so the test can inspect exact bytes on Windows.
    void CloseAllForTests();
#endif

    static const char* EventLogPath();

  private:
    EngineLog() = default;
    ~EngineLog();
    EngineLog( const EngineLog& ) = delete;
    EngineLog& operator=( const EngineLog& ) = delete;

    // Invariant: callers hold m_logMutex for the complete OpenLog + FILE
    // operation. Returning a borrowed FILE outside that critical section would
    // make the map safe while leaving the CRT stream itself racy.
#if defined( _DEBUG ) || defined( SKULLBONEZ_TEST_ENGINE_LOG )
    FILE* OpenLog( const char* fileName );
    std::mutex m_logMutex;
    std::unordered_map<std::string, FILE*> m_logs;
#endif
};

// Why: Log() stays as a tiny convenience wrapper, but callers now include this
// owner header directly instead of receiving logging through Common.h.
inline EngineLog& Log()
{
    return EngineLog::Get();
}
} // namespace Core
} // namespace SkullbonezCore
