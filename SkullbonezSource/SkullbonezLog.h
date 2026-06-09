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
/* -- SkullbonezLog ----------------------------------------------------------------------------------------------------------------------------------------------

    Debug-only singleton logger.  Maps file names to open FILE handles so the caller never
    needs to open, close, or flush anything — just call Writef() and the rest is automatic.

    In Release builds every method is an inline no-op and the class has no data members,
    so the compiler eliminates all call sites completely.

    Usage (from anywhere — Log() is injected into SkullbonezCommon.h):

        Log().Writef( "Debug/physics.csv", "terrain,%d,%.2f,%.2f\n", frame, x, y );
        Log().WriteEventf( "scene_started index=%d path=\"%s\"", index, path );

    The file is created on the first Writef() for that name.  Subsequent calls to the same
    name append to the already-open handle.  Writef() uses a generous file buffer so hot
    diagnostic paths can emit many rows without forcing a disk flush on every row.  Event
    logs still flush immediately.  All files are flushed and closed when the process exits.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezLog
{

  public:
    static SkullbonezLog& Get();

    void Writef( const char* fileName, const char* fmt, ... );
    void WriteEventf( const char* fmt, ... );
    void FlushAll();

    static const char* EventLogPath();

  private:
    SkullbonezLog() = default;
    ~SkullbonezLog();
    SkullbonezLog( const SkullbonezLog& ) = delete;
    SkullbonezLog& operator=( const SkullbonezLog& ) = delete;

#ifdef _DEBUG
    FILE* OpenLog( const char* fileName );
    std::unordered_map<std::string, FILE*> m_logs;
#endif
};
} // namespace Basics
} // namespace SkullbonezCore
