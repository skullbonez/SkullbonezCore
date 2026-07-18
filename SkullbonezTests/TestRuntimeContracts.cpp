//
// File: SkullbonezTests/TestRuntimeContracts.cpp
// Purpose:
//   Exercises result values, logger concurrency, worker-task lifetime, and
//   Lane F probes.
//
// Summary:
//   Ordinary contracts run in doctest. Contracts that must abort launch this
//   executable as a named child case so the parent suite survives.
//
// Glossary:
//   Fatal probe: Child invocation expected to end through SB_FATAL.
//   In-flight task: AmortizedTask range currently owned by a worker callback.
//   Lane F: Fatal-invariant error path that records diagnostics and terminates.
//   Lane R: Recoverable result path that returns an owned error instead of
//     terminating the engine.
//
// Invariants:
//   - Fatal child cases return normally only when the case name is unknown.
//   - Blocking task tests release the worker before local state is destroyed.
//   - Every threaded worker test shuts its pool down before local task state expires.
//
// Related:
//   - SkullbonezSource/Core/Log.h
//   - SkullbonezSource/Core/AmortizedTask.h
//   - SkullbonezSource/Physics/SpatialGrid.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/AmortizedTask.h"
#include "../SkullbonezSource/Core/FatalError.h"
#include "../SkullbonezSource/Core/Log.h"
#include "../SkullbonezSource/Core/SbResult.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"
#include "TestFatalCases.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using SkullbonezCore::Core::EngineLog;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Threading::AmortizedTask;
using SkullbonezCore::Threading::RunWorkerSystemSelfTest;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::WorkerChunkRange;
using SkullbonezCore::Threading::WorkerPool;

namespace
{
std::string ReadHandleText( HANDLE file )
{
    std::string text;
    if ( file == INVALID_HANDLE_VALUE || SetFilePointer( file, 0, nullptr, FILE_BEGIN ) == INVALID_SET_FILE_POINTER )
    {
        return text;
    }

    char buffer[4096] = {};
    DWORD bytesRead = 0;
    while ( ReadFile( file, buffer, sizeof( buffer ), &bytesRead, nullptr ) && bytesRead > 0 )
    {
        text.append( buffer, buffer + bytesRead );
    }
    return text;
}

std::string ReadSharedFileText( const char* path )
{
    HANDLE file = CreateFileA( path,
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr );
    if ( file == INVALID_HANDLE_VALUE )
    {
        return {};
    }
    std::string text = ReadHandleText( file );
    CloseHandle( file );
    return text;
}

struct FatalChildResult
{
    bool launched = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    std::string output;
};

FatalChildResult RunFatalChild( const char* caseName )
{
    FatalChildResult result;
    const char* executable = RuntimeTestExecutablePath();
    if ( !executable )
    {
        return result;
    }

    char temporaryDirectory[MAX_PATH] = {};
    char outputPath[MAX_PATH] = {};
    if ( GetTempPathA( MAX_PATH, temporaryDirectory ) == 0 ||
         GetTempFileNameA( temporaryDirectory, "sbf", 0, outputPath ) == 0 )
    {
        return result;
    }

    SECURITY_ATTRIBUTES security = { sizeof( security ), nullptr, TRUE };
    HANDLE output = CreateFileA( outputPath,
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 &security,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_TEMPORARY,
                                 nullptr );
    if ( output == INVALID_HANDLE_VALUE )
    {
        DeleteFileA( outputPath );
        return result;
    }

    char commandLine[4096] = {};
    snprintf( commandLine, sizeof( commandLine ), "\"%s\" --fatal-case \"%s\"", executable, caseName );
    STARTUPINFOA startup = {};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process = {};
    result.launched = CreateProcessA( nullptr,
                                      commandLine,
                                      nullptr,
                                      nullptr,
                                      TRUE,
                                      CREATE_NO_WINDOW,
                                      nullptr,
                                      nullptr,
                                      &startup,
                                      &process ) != FALSE;
    if ( result.launched )
    {
        constexpr DWORD FATAL_CHILD_TIMEOUT_MS = 10000;
        const DWORD waitResult = WaitForSingleObject( process.hProcess, FATAL_CHILD_TIMEOUT_MS );
        if ( waitResult == WAIT_TIMEOUT )
        {
            // Hazard: terminate only the exact child process handle created for
            // this probe. A regressed fatal contract must not hang validation.
            result.timedOut = true;
            TerminateProcess( process.hProcess, 0xDEADu );
            WaitForSingleObject( process.hProcess, 5000 );
        }
        GetExitCodeProcess( process.hProcess, &result.exitCode );
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
    }

    FlushFileBuffers( output );
    result.output = ReadHandleText( output );
    CloseHandle( output );
    DeleteFileA( outputPath );
    return result;
}

void ExpectFatalCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
#if defined( __SANITIZE_ADDRESS__ )
    // ASan reports the deliberate abort as a sanitizer signal. The healthy
    // ASan lane targets the concurrent logger test; normal CPU gates own fatal
    // child proof.
    static_cast<void>( caseName );
    static_cast<void>( expectedDiagnostics );
#else
    const FatalChildResult child = RunFatalChild( caseName );
    INFO( "fatal child output: " << child.output );
    REQUIRE( child.launched );
    REQUIRE_FALSE( child.timedOut );
    CHECK( child.exitCode != 0 );
    for ( const char* expected : expectedDiagnostics )
    {
        CHECK( child.output.find( expected ) != std::string::npos );
    }
#endif
}

struct WorkerFatalProbe
{
    void ExecuteWorkerTask()
    {
        SB_FATAL( "Tests/WorkerFatalProbe", "worker-thread fatal logging probe" );
    }
};
} // namespace

bool RunRuntimeFatalCase( const char* caseName )
{
    if ( std::strcmp( caseName, "spatial-grid-nan" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        grid.Insert( 7, Vector3( std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f ), 1.0f );
        return true;
    }
    if ( std::strcmp( caseName, "spatial-grid-extent" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        grid.Insert( 11, Vector3( SpatialGrid::MAX_WORLD_COORDINATE + 1.0f, 0.0f, 0.0f ), 1.0f );
        return true;
    }
    if ( std::strcmp( caseName, "spatial-grid-zero-cell" ) == 0 )
    {
        static SpatialGrid grid( 0.0f );
        return true;
    }
    if ( std::strcmp( caseName, "spatial-grid-nan-cell" ) == 0 )
    {
        static SpatialGrid grid( std::numeric_limits<float>::quiet_NaN() );
        return true;
    }
    if ( std::strcmp( caseName, "spatial-grid-tiny-cell" ) == 0 )
    {
        static SpatialGrid grid( SpatialGrid::MIN_CELL_SIZE * 0.5f );
        return true;
    }
    if ( std::strcmp( caseName, "spatial-grid-bucket-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );
        grid.Clear();
        for ( int cell = 0; cell <= SpatialGrid::MAX_BUCKETS; ++cell )
        {
            grid.Insert( 0, Vector3( static_cast<float>( cell ) + 0.25f, 0.25f, 0.25f ), 0.0f );
        }
        return true;
    }
    if ( std::strcmp( caseName, "amortized-task-in-flight-destroy" ) == 0 )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( 1 );
        std::atomic<bool> started{ false };
        std::atomic<bool> release{ false };
        {
            AmortizedTask task(
                1,
                1,
                [&]( int, int )
                {
                    started.store( true, std::memory_order_release );
                    while ( !release.load( std::memory_order_acquire ) )
                    {
                        std::this_thread::yield();
                    }
                } );
            task.SubmitTick( pool );
            while ( !started.load( std::memory_order_acquire ) )
            {
                std::this_thread::yield();
            }
        }
        return true;
    }
    if ( std::strcmp( caseName, "worker-fatal-log" ) == 0 )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( 1 );
        WorkerFatalProbe probe;
        pool.SubmitNoAlloc( probe );
        for ( ;; )
        {
            std::this_thread::yield();
        }
    }
    return false;
}

TEST_CASE( "SkullbonezCore::Core::EngineLog: concurrent file and event writes share one safe owner boundary" )
{
#if defined( SKULLBONEZ_TEST_ENGINE_LOG )
    constexpr const char* path = "Debug/runtime_contract_log_test.log";
    constexpr int threadCount = 6;
    constexpr int writesPerThread = 64;
    std::remove( path );

    std::vector<std::thread> threads;
    threads.reserve( threadCount );
    for ( int threadIndex = 0; threadIndex < threadCount; ++threadIndex )
    {
        threads.emplace_back(
            [threadIndex, path, writesPerThread]()
            {
                for ( int writeIndex = 0; writeIndex < writesPerThread; ++writeIndex )
                {
                    SkullbonezCore::Core::EngineLog::Get().Writef( path, "%d,%d\n", threadIndex, writeIndex );
                    if ( writeIndex % 16 == 0 )
                    {
                        SkullbonezCore::Core::EngineLog::Get().WriteEventf(
                            "runtime_contract_log_test thread=%d write=%d", threadIndex, writeIndex );
                    }
                }
            } );
    }
    for ( std::thread& thread : threads )
    {
        thread.join();
    }
    SkullbonezCore::Core::EngineLog::Get().FlushAll();
    SkullbonezCore::Core::EngineLog::Get().CloseAllForTests();

    const std::string contents = ReadSharedFileText( path );
    CHECK( static_cast<int>( std::count( contents.begin(), contents.end(), '\n' ) ) ==
           threadCount * writesPerThread );
#endif
}

TEST_CASE( "AmortizedTask: Reset reports idle success and in-flight refusal" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    int completedItems = 0;
    AmortizedTask idleTask( 2, 1, [&]( int begin, int end ) { completedItems += end - begin; } );
    idleTask.SubmitTick( inlinePool );
    idleTask.SubmitTick( inlinePool );
    REQUIRE( idleTask.IsComplete() );
    CHECK( completedItems == 2 );
    CHECK( idleTask.Reset() );
    CHECK_FALSE( idleTask.IsComplete() );

    WorkerPool workerPool( lockOrderValidator );
    workerPool.Initialise( 1 );
    std::atomic<bool> started{ false };
    std::atomic<bool> release{ false };
    AmortizedTask inFlightTask(
        1,
        1,
        [&]( int, int )
        {
            started.store( true, std::memory_order_release );
            while ( !release.load( std::memory_order_acquire ) )
            {
                std::this_thread::yield();
            }
        } );
    inFlightTask.SubmitTick( workerPool );
    while ( !started.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }
    CHECK_FALSE( inFlightTask.Reset() );
    release.store( true, std::memory_order_release );
    while ( inFlightTask.IsInFlight() )
    {
        std::this_thread::yield();
    }
    workerPool.Shutdown();
}

TEST_CASE( "WorkerPool: inline and threaded self-tests preserve deterministic collection" )
{
    for ( const int threadCount : { 0, 2 } )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( threadCount );
        FILE* output = nullptr;
        REQUIRE( tmpfile_s( &output ) == 0 );
        REQUIRE( output != nullptr );

        CHECK( RunWorkerSystemSelfTest( pool, output ) );
        CHECK( pool.IsInitialised() == ( threadCount > 0 ) );
        CHECK( pool.GetThreadCount() == WorkerPool::ResolveThreadCount( threadCount ) );
        CHECK( pool.GetMinParallelItems() == 32 );
        pool.Shutdown();
        std::fclose( output );
    }

    CHECK( WorkerPool::MaxThreadCount() >= 1 );
    CHECK( WorkerPool::ResolveThreadCount( -1 ) >= 0 );
    CHECK_FALSE( WorkerPool::IsCurrentThreadWorker() );
    CHECK( WorkerPool::CurrentWorkerIndex() == -1 );
}

TEST_CASE( "WorkerPool: chunk ranges cover a half-open interval once and in order" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool pool( lockOrderValidator );
    pool.Initialise( 2 );
    WorkerChunkRange chunks[8] = {};

    const int chunkCount = pool.BuildChunkRangesNoAlloc( 3, 14, 1, chunks, 8 );
    REQUIRE( chunkCount >= 1 );
    CHECK( chunks[0].begin == 3 );
    CHECK( chunks[chunkCount - 1].end == 14 );
    for ( int index = 0; index < chunkCount; ++index )
    {
        CHECK( chunks[index].chunkIndex == index );
        CHECK( chunks[index].begin < chunks[index].end );
        if ( index > 0 )
        {
            CHECK( chunks[index - 1].end == chunks[index].begin );
        }
    }

    CHECK( pool.BuildChunkRangesNoAlloc( 4, 4, 1, chunks, 8 ) == 0 );
    CHECK( pool.BuildChunkRangesNoAlloc( 0, 4, 1, nullptr, 8 ) == 0 );
    CHECK( pool.BuildChunkRangesNoAlloc( 0, 4, 1, chunks, 0 ) == 0 );
    pool.Shutdown();
}

TEST_CASE( "SbResult: success and formatted failure values propagate owner and message" )
{
    const SbResult success = SbResult::Success();
    CHECK( success.ok );
    CHECK( std::strcmp( success.error.owner, "" ) == 0 );
    CHECK( std::strcmp( success.error.message, "" ) == 0 );

    const SbResult failure = SbResult::Failure( "SceneParser", "invalid body %d", 17 );
    CHECK_FALSE( failure.ok );
    CHECK( std::strcmp( failure.error.owner, "SceneParser" ) == 0 );
    CHECK( std::strcmp( failure.error.message, "invalid body 17" ) == 0 );

    const SbResult defaultFailure = SbResult::Failure( nullptr, nullptr );
    CHECK_FALSE( defaultFailure.ok );
    CHECK( std::strcmp( defaultFailure.error.owner, "" ) == 0 );
    CHECK( std::strcmp( defaultFailure.error.message, "recoverable operation failed" ) == 0 );
}

TEST_CASE( "Runtime contracts: invalid broadphase and task lifetimes terminate in child probes" )
{
    ExpectFatalCase( "spatial-grid-nan",
                     { "FATAL[Physics/SpatialGrid]", "body=7", "min=(nan,-1,-1)", "max_world_coordinate=100000" } );
    ExpectFatalCase( "spatial-grid-extent",
                     { "FATAL[Physics/SpatialGrid]", "body=11", "max=(100002,1,1)",
                       "max_world_coordinate=100000" } );
    ExpectFatalCase( "spatial-grid-zero-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0", "minimum=0.5" } );
    ExpectFatalCase( "spatial-grid-nan-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=nan", "minimum=0.5" } );
    ExpectFatalCase( "spatial-grid-tiny-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0.25", "minimum=0.5" } );
    ExpectFatalCase( "spatial-grid-bucket-capacity",
                     { "FATAL[Physics/SpatialGrid]", "bucket capacity exceeded", "capacity=8192", "active=8192",
                       "phase=steady_gameplay" } );
    ExpectFatalCase( "amortized-task-in-flight-destroy",
                     { "FATAL[Core/AmortizedTask]", "Destroying AmortizedTask while worker chunk is in flight" } );
    ExpectFatalCase( "worker-fatal-log",
                     { "FATAL[Tests/WorkerFatalProbe]", "worker-thread fatal logging probe" } );
}
