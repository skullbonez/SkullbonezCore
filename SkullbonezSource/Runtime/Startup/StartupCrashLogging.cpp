/*
File: StartupCrashLogging.cpp
Purpose:
  Owns debug SEH reporting, symbolized stack capture, and unexpected-terminate
  logging for process startup.

Summary:
  The process filter records the exception code and address, walks a bounded
  stack, flushes persistent diagnostics, and then lets Windows terminate the
  failed process.

Glossary:
  SEH (Structured Exception Handling): Windows exception record delivered for
    faults such as invalid memory access.
  Symbol displacement: Byte offset from a resolved function start to the fault.
  Terminate handler: Last-resort callback for an unexpected exception-free
    engine termination.

Invariants:
  - Stack walking is bounded to 64 frames.
  - Symbol setup failure never hides the raw fault addresses.
  - Both SEH and terminate paths flush logs before returning or aborting.

Related:
  - StartupCrashLogging.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "StartupCrashLogging.h"

#include "../../Core/Common.h"
#include "../../Core/Log.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#ifdef _DEBUG
#include <windows.h>
#include <dbghelp.h>
#pragma comment( lib, "dbghelp.lib" )
#endif

namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
#ifdef _DEBUG
namespace
{
const char* ExceptionCodeName( DWORD code )
{
    switch ( code )
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
        return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    default:
        return "EXCEPTION_UNKNOWN";
    }
}


void WriteDebugCrashStack( EXCEPTION_POINTERS* exceptionInfo )
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    DWORD symOptions = SymGetOptions();
    symOptions |= SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
    SymSetOptions( symOptions );

    const BOOL symbolsReady = SymInitialize( process, nullptr, TRUE );
    if ( !symbolsReady )
    {
        SkullbonezCore::Core::Log().Writef( SkullbonezCore::Core::EngineLog::EventLogPath(),
                                            "    stack_symbols=unavailable error=%lu\n",
                                            GetLastError() );
    }

    CONTEXT context = {};
    if ( exceptionInfo && exceptionInfo->ContextRecord )
    {
        context = *exceptionInfo->ContextRecord;
    }
    else
    {
        RtlCaptureContext( &context );
    }

    STACKFRAME64 frame = {};
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#if defined( _M_X64 )
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#endif

    SkullbonezCore::Core::Log().Writef( SkullbonezCore::Core::EngineLog::EventLogPath(), "    stack_trace:\n" );
    for ( int frameIndex = 0; frameIndex < 64; ++frameIndex )
    {
        BOOL walked = StackWalk64( machineType,
                                   process,
                                   thread,
                                   &frame,
                                   &context,
                                   nullptr,
                                   SymFunctionTableAccess64,
                                   SymGetModuleBase64,
                                   nullptr );
        if ( !walked || frame.AddrPC.Offset == 0 )
        {
            break;
        }

        const DWORD64 address = frame.AddrPC.Offset;
        char symbolStorage[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME] = {};
        // Why: DbgHelp's SYMBOL_INFO is a variable-tail ABI whose Name bytes
        // occupy caller-provided aligned storage immediately after the header.
        PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>( symbolStorage );
        symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 symbolDisplacement = 0;
        const BOOL hasSymbol = symbolsReady && SymFromAddr( process, address, &symbolDisplacement, symbol );

        IMAGEHLP_LINE64 lineInfo = {};
        lineInfo.SizeOfStruct = sizeof( lineInfo );
        DWORD lineDisplacement = 0;
        const BOOL hasLine = symbolsReady && SymGetLineFromAddr64( process, address, &lineDisplacement, &lineInfo );

        if ( hasSymbol && hasLine )
        {
            SkullbonezCore::Core::Log().Writef( SkullbonezCore::Core::EngineLog::EventLogPath(),
                                                "      #%02d 0x%016llX %s+0x%llX (%s:%lu)\n",
                                                frameIndex,
                                                static_cast<unsigned long long>( address ),
                                                symbol->Name,
                                                static_cast<unsigned long long>( symbolDisplacement ),
                                                lineInfo.FileName,
                                                lineInfo.LineNumber );
        }
        else if ( hasSymbol )
        {
            SkullbonezCore::Core::Log().Writef( SkullbonezCore::Core::EngineLog::EventLogPath(),
                                                "      #%02d 0x%016llX %s+0x%llX\n",
                                                frameIndex,
                                                static_cast<unsigned long long>( address ),
                                                symbol->Name,
                                                static_cast<unsigned long long>( symbolDisplacement ) );
        }
        else
        {
            SkullbonezCore::Core::Log().Writef( SkullbonezCore::Core::EngineLog::EventLogPath(),
                                                "      #%02d 0x%016llX <unknown>\n",
                                                frameIndex,
                                                static_cast<unsigned long long>( address ) );
        }
    }

    if ( symbolsReady )
    {
        SymCleanup( process );
    }
}


LONG WINAPI DebugUnhandledExceptionFilter( EXCEPTION_POINTERS* exceptionInfo )
{
    DWORD exceptionCode = 0;
    // Why: Windows reports the fault instruction as an opaque address and the
    // variadic %p diagnostic requires the same ABI pointer representation.
    void* exceptionAddress = nullptr;
    if ( exceptionInfo && exceptionInfo->ExceptionRecord )
    {
        exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
        exceptionAddress = exceptionInfo->ExceptionRecord->ExceptionAddress;
    }

    SkullbonezCore::Core::Log().WriteEventf( "crash exception=0x%08lX name=%s address=%p",
                                             exceptionCode,
                                             ExceptionCodeName( exceptionCode ),
                                             exceptionAddress );
    WriteDebugCrashStack( exceptionInfo );
    SkullbonezCore::Core::Log().FlushAll();

    return EXCEPTION_EXECUTE_HANDLER;
}
} // anonymous namespace

void InstallDebugCrashLogger()
{
    SetUnhandledExceptionFilter( DebugUnhandledExceptionFilter );
    // Hazard: an unexpected terminate in the exception-free engine bypasses
    // the SEH filter above. Persist a fixed diagnostic before aborting.
    std::set_terminate(
        []()
        {
            const char* message = "unexpected termination in exception-free engine";
            SkullbonezCore::Core::Log().WriteEventf( "terminate_abort message=\"%s\"", message );
            fprintf( stderr, "FATAL: terminate_abort %s\n", message );
            fflush( stderr );
            SkullbonezCore::Core::Log().FlushAll();
            std::abort();
        } );
}
#endif
} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
