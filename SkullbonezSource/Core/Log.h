/*
File: SkullbonezSource/Core/Log.h
Purpose:
  Writes debug-only runtime, crash, and diagnostics logs.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Related:
  - SkullbonezSource/Core/Log.cpp
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#ifdef _DEBUG
#include <cstdio>
#include <unordered_map>
#include <string>
#endif


namespace SkullbonezCore
{
namespace Basics
{
/* -- EngineLog
----------------------------------------------------------------------------------------------------------------------------------------------

    Debug-only singleton logger.  Maps file names to open FILE handles so the caller never
    needs to open, close, or flush anything — just call Writef() and the rest is automatic.

    In Release builds every method is an inline no-op and the class has no data members,
    so the compiler eliminates all call sites completely.

    Usage (from anywhere — Log() is injected into Common.h):

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
    void WriteEventf( const char* fmt, ... );
    void FlushAll();

    static const char* EventLogPath();

  private:
    EngineLog() = default;
    ~EngineLog();
    EngineLog( const EngineLog& ) = delete;
    EngineLog& operator=( const EngineLog& ) = delete;

#ifdef _DEBUG
    FILE* OpenLog( const char* fileName );
    std::unordered_map<std::string, FILE*> m_logs;
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
