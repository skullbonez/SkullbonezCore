//
// File: SkullbonezTests/TestRuntimeContracts.cpp
// Purpose:
//   Exercises result values, logger concurrency, worker-task lifetime, and
//   Lane F probes, including disabled development-profiler compile contracts.
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
//   Disabled marker seam: Development-profiler macro that must discard its
//     argument tokens when the vendor client is absent from the test build.
//
// Invariants:
//   - Fatal child cases return normally only when the case name is unknown.
//   - A diagnostic store may not finish destruction while a failed result lease
//     still names it.
//   - Diagnostic counter overflow and same-thread lock re-entry terminate in
//     isolated children instead of wrapping, aliasing, or spinning.
//   - Blocking task tests release the worker before local state is destroyed.
//   - Every threaded worker test shuts its pool down before local task state expires.
//   - Disabled development-profiler macros never evaluate caller expressions.
//   - Foreign page-boundary deletes reach allocation Lane F without faulting
//     while probing their inaccessible candidate header.
//   - Release foreign frees are proved in a child so their process-lifetime
//     counter cannot contaminate later parent-process diagnostics.
//   - Allocation-size overflow reaches allocation Lane F before CRT malloc.
//   - The contact-solve phase cursor admits only its full ordered walk and two
//     existing no-work terminal edges; every other edge terminates in Lane F.
//   - Physics storage seeding rejects missing allocation/owner scopes,
//     SceneLoad phase, missing Replay owner, and any Replay owner other than
//     the canonical prediction working set.
//   - Spatial-grid backing reserves only during SceneLoad; fixed-step
//     exhaustion reports the exact owner, capacity, high-water, and phase.
//   - Sleep support edges fail before either the scene-committed reservation or
//     the semantic ceiling can be exceeded.
//   - Pipeline batch counting rejects full-record mode so retained row count
//     cannot diverge from the recorder's canonical event count.
//   - DX12 retirement accounting records a real below-capacity peak, resets at
//     device boundaries, and reports release/fence facts at exhaustion.
//
// Related:
//   - SkullbonezSource/Core/Log.h
//   - SkullbonezSource/Core/AmortizedTask.h
//   - SkullbonezSource/Physics/SpatialGrid.h
//   - SkullbonezSource/Physics/SleepIslandSystem.h
//   - SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h
//   - SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/AmortizedTask.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h"
#endif
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/FatalError.h"
#include "../SkullbonezSource/Core/Log.h"
#include "../SkullbonezSource/Core/SbResult.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"
#include "../SkullbonezSource/Physics/SleepIslandSystem.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsFixedList.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"
#include "../SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h"
#include "../SkullbonezSource/Core/TracyClientOwner.h"
#include "../SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "TestFatalCases.h"
#include "TestSbResultAccess.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace SkullbonezCore::Rendering
{
struct Dx12DeferredReleaseOwnerTestAccess
{
    static void ObservePendingCount( Dx12DeferredReleaseOwner& owner, size_t pendingCount )
    {
        owner.m_diagnostics.ObservePendingCount( pendingCount );
    }
};
} // namespace SkullbonezCore::Rendering

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Core::EngineLog;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::AppendSleepSupportEdge;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Threading::AmortizedTask;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::RunWorkerSystemSelfTest;
using SkullbonezCore::Threading::WorkerChunkRange;
using SkullbonezCore::Threading::WorkerPool;

namespace SkullbonezCore
{
namespace Physics
{
struct PersistentContactSolveTransactionTestAccess
{
    static void Advance( PersistentContactSolveTransaction& transaction, PersistentContactSolvePhaseCursor::Phase next )
    {
        transaction.AdvanceOrFatal( next, "ExhaustiveFatalProbe" );
    }
};
} // namespace Physics

namespace Runtime
{
struct OperatorCommandTransactionTestAccess
{
    static void Advance( OperatorCommandTransaction& transaction, OperatorCommandPhaseCursor::Phase next )
    {
        transaction.AdvanceOrFatal( next, "ExhaustiveFatalProbe" );
    }
};
} // namespace Runtime
} // namespace SkullbonezCore

TEST_CASE( "Tracy disabled marker seams discard caller expressions" )
{
    int evaluatedArguments = 0;
    SKORE_TRACY_NAME_WORKER_THREAD( ++evaluatedArguments );
    SKORE_TRACY_MARK_SUBMITTED_FRAME();
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Disabled", ++evaluatedArguments );
    const uint32_t sourceHandle = SKORE_TRACY_REGISTER_OWNER_ZONE( "Disabled", ++evaluatedArguments );
    const uint32_t zoneToken = SKORE_TRACY_BEGIN_OWNER_ZONE( ++evaluatedArguments );
    SKORE_TRACY_END_OWNER_ZONE( ++evaluatedArguments );
    SKORE_TRACY_PLOT_VALUE( "Disabled", ++evaluatedArguments );
    CHECK( evaluatedArguments == 0 );
    CHECK( sourceHandle == 0u );
    CHECK( zoneToken == 0u );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
TEST_CASE( "Tracy allocation events stay inactive outside heavy capture" )
{
    using namespace SkullbonezCore::Core::Allocation;
    int value = 0;
    SetTracyAllocationTracingEnabled( false );
    const uint64_t connectionId = RecordTracyAllocation( &value, sizeof( value ) );
    CHECK( connectionId == 0u );
    RecordTracyFree( &value, connectionId );
}
#endif

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
    HANDLE file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

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

struct ForeignAllocationHeaderLayout
{
    void* raw = nullptr;
    uint64_t size = 0u;
    uint32_t phase = 0u;
    uint32_t flags = 0u;
    uint16_t owner = 0u;
    uint16_t reserved = 0u;
    uint32_t magic = 0u;
    uint64_t ownershipCookie = 0u;
};

constexpr uint32_t FOREIGN_ALLOCATION_HEADER_MAGIC = 0xA110CA7Eu;

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
    HANDLE output = CreateFileA( outputPath, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_TEMPORARY, nullptr );

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
    result.launched = CreateProcessA( nullptr, commandLine, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process ) != FALSE;

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

#if !defined( _DEBUG ) && !defined( SKULLBONEZ_PROFILE_ENABLED ) && !defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
void ExpectCleanChildCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
    const FatalChildResult child = RunFatalChild( caseName );
    INFO( "clean child output: " << child.output );
    REQUIRE( child.launched );
    REQUIRE_FALSE( child.timedOut );
    CHECK( child.exitCode == 0 );

    for ( const char* expected : expectedDiagnostics )
    {
        CHECK( child.output.find( expected ) != std::string::npos );
    }
}
#endif

struct WorkerFatalProbe
{
    void ExecuteWorkerTask()
    {
        SB_FATAL( "Tests/WorkerFatalProbe", "worker-thread fatal logging probe" );
    }
};
} // namespace

void ExpectRuntimeFatalCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
    ExpectFatalCase( caseName, expectedDiagnostics );
}

bool RunRuntimeFatalCase( const char* caseName )
{
    if ( RunRenderGraphFatalCase( caseName ) )
    {
        return true;
    }

    if ( std::strcmp( caseName, "physics-pipeline-batch-full-mode" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsPipelineTraceRecorder recorder;
        recorder.BeginStep( true );
        recorder.RecordEvents( 1u );
    }

    if ( std::strcmp( caseName, "dx12-retirement-release-snapshot" ) == 0 )
    {
        SkullbonezCore::Rendering::Dx12RetirementDiagnosticState retirementDiagnostics;
        retirementDiagnostics.ObservePendingCount(
            SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );
        retirementDiagnostics.ObserveRelease( 9u, 4u, true, 77u );
        retirementDiagnostics
            .FatalExhaustion( SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS,
                              SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );
    }

    const bool resetForDevice = std::strcmp( caseName, "dx12-retirement-reset-live-device" ) == 0;
    const bool resetAfterShutdown = std::strcmp( caseName, "dx12-retirement-reset-live-shutdown" ) == 0;

    if ( resetForDevice || resetAfterShutdown )
    {
        SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;
        retirements.QuarantineStaticDescriptor( 7u );

        if ( resetForDevice )
        {
            retirements.ResetForDevice();
        }
        else
        {
            retirements.ResetAfterShutdown();
        }

        return true;
    }

    if ( std::strcmp( caseName, "dx12-retirement-capacity" ) == 0 )
    {
        SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;

        for ( size_t index = 0; index < retirements.MAX_PENDING_RETIREMENTS; ++index )
        {
            retirements.QuarantineStaticDescriptor( static_cast<UINT>( index ) );
        }

        retirements.QuarantineStaticDescriptor( static_cast<UINT>( retirements.MAX_PENDING_RETIREMENTS ) );
        return true;
    }

    unsigned int contactSolvePhaseFrom = 0u;
    unsigned int contactSolvePhaseTo = 0u;

    if ( sscanf_s( caseName, "contact-solve-phase-%u-%u", &contactSolvePhaseFrom, &contactSolvePhaseTo ) == 2 )
    {
        using SkullbonezCore::Physics::PersistentContactSolvePhaseCursor;
        using SkullbonezCore::Physics::PersistentContactSolveTransaction;
        using SkullbonezCore::Physics::PersistentContactSolveTransactionTestAccess;
        using Phase = PersistentContactSolvePhaseCursor::Phase;
        constexpr std::array phases { Phase::Idle,
                                      Phase::EntryPolicySetup,
                                      Phase::BodySetup,
                                      Phase::BuildManifolds,
                                      Phase::TerrainRows,
                                      Phase::Precompute,
                                      Phase::SolveRows,
                                      Phase::PointSupportInstability,
                                      Phase::TerrainRestPolicy,
                                      Phase::WriteBack,
                                      Phase::DebugContacts,
                                      Phase::PositionCorrection,
                                      Phase::CacheStore,
                                      Phase::FixedContactRelease,
                                      Phase::Complete,
                                      Phase::Count };

        if ( contactSolvePhaseFrom >= phases.size() - 1u || contactSolvePhaseTo >= phases.size() )
        {
            return false;
        }

        PersistentContactSolveTransaction transaction;

        for ( unsigned int phaseIndex = 1u; phaseIndex <= contactSolvePhaseFrom; ++phaseIndex )
        {
            PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
        }

        PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[contactSolvePhaseTo] );
        return true;
    }

    unsigned int operatorPhaseFrom = 0u;
    unsigned int operatorPhaseTo = 0u;

    if ( sscanf_s( caseName, "operator-command-phase-%u-%u", &operatorPhaseFrom, &operatorPhaseTo ) == 2 )
    {
        using SkullbonezCore::Runtime::OperatorCommandPhaseCursor;
        using SkullbonezCore::Runtime::OperatorCommandTransaction;
        using SkullbonezCore::Runtime::OperatorCommandTransactionTestAccess;
        using Phase = OperatorCommandPhaseCursor::Phase;
        constexpr std::array phases { Phase::Idle,
                                      Phase::DeviceAndMode,
                                      Phase::PhysicsControl,
                                      Phase::RuntimePresentation,
                                      Phase::SimulationPolicy,
                                      Phase::PhysicsMaterial,
                                      Phase::WorldPolicy,
                                      Phase::CinematicPolicy,
                                      Phase::Complete,
                                      Phase::Count };

        if ( operatorPhaseFrom >= phases.size() - 1u || operatorPhaseTo >= phases.size() )
        {
            return false;
        }

        SkullbonezCore::UI::InGameUICommands commands;
        OperatorCommandTransaction transaction( commands );

        for ( unsigned int phaseIndex = 1u; phaseIndex <= operatorPhaseFrom; ++phaseIndex )
        {
            OperatorCommandTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
        }

        OperatorCommandTransactionTestAccess::Advance( transaction, phases[operatorPhaseTo] );
        return true;
    }

    const bool pendingReplayTimeline = std::strcmp( caseName, "replay-restore-pending-timeline-complete" ) == 0;
    const bool unprovedReplayRollback = std::strcmp( caseName, "replay-restore-unproved-rollback" ) == 0;

    if ( pendingReplayTimeline || unprovedReplayRollback )
    {
        using SkullbonezCore::Runtime::ReplayRestoreTransaction;
        using SkullbonezCore::Runtime::ReplaySolverFrameSample;

        ReplayRestoreTransaction transaction;
        transaction.SelectArtifact( 1u, 2u );
        transaction.CaptureLiveBackup( ReplaySolverFrameSample {} );
        transaction.MarkTopologyPrepared( unprovedReplayRollback, unprovedReplayRollback );

        if ( unprovedReplayRollback )
        {
            transaction.MarkRolledBack( "unproved rollback" );
            return true;
        }

        transaction.MarkCheckpointApplied();
        transaction.MarkTargetStepped( 3u, 0u, 0u );
        transaction.MarkTargetVerified();
        transaction.PrepareTimelineReset( 4u, 5, 0xA5u );
        transaction.Complete();
        return true;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-runtime-capacity" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 4>
            values( "fatal.physics-fixed-list.runtime",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );

        {
            RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
            values.Reserve( 1u );
        }
        values.push_back( 1 );
        values.push_back( 2 );
        return true;
    }

    if ( std::strcmp( caseName, "physics-prediction-seed-wrong-replay-owner" ) == 0 )
    {
        using namespace SkullbonezCore::Core::Allocation;
        constexpr int wrongOwnerHardCapacity = 1024;
        const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
            { SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, RuntimeReserveSubsystem::Replay,
              RuntimeReservePhase::Replay, 0, wrongOwnerHardCapacity, RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED, true,
              "Fatal probe for unrelated Replay growth authority" } );

        const RuntimeReserveGrowthResult growth = RuntimeReserveAllocator::
            RequestGrowth( owner, { SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, "PhysicsEngine seed",
                                    RuntimeReservePhase::Replay, 0, 0, wrongOwnerHardCapacity, 1 } );

        if ( !growth.granted )
        {
            return false;
        }

        auto source = std::make_unique<PhysicsEngine>();
        auto destination = std::make_unique<PhysicsEngine>();
        RuntimeAllocationScope replayScope( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, growth );
        destination->SeedReplayPredictionStorageFrom( *source );
        return true;
    }

    const bool predictionSeedMissingScope = std::strcmp( caseName, "physics-prediction-seed-missing-scope" ) == 0;
    const bool predictionSeedSceneLoad = std::strcmp( caseName, "physics-prediction-seed-scene-load" ) == 0;
    const bool predictionSeedMissingOwner = std::strcmp( caseName, "physics-prediction-seed-missing-owner" ) == 0;

    if ( predictionSeedMissingScope || predictionSeedSceneLoad || predictionSeedMissingOwner )
    {
        auto source = std::make_unique<PhysicsEngine>();
        auto destination = std::make_unique<PhysicsEngine>();

        if ( predictionSeedSceneLoad )
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            destination->SeedReplayPredictionStorageFrom( *source );
            return true;
        }

        if ( predictionSeedMissingOwner )
        {
            RuntimeAllocationScope replayScope( RuntimeAllocationPhase::Replay );
            destination->SeedReplayPredictionStorageFrom( *source );
            return true;
        }

        destination->SeedReplayPredictionStorageFrom( *source );
        return true;
    }

    const bool terrainLocateCellRange = std::strcmp( caseName, "terrain-locate-cell-range" ) == 0;
    const bool terrainLocateNonFinite = std::strcmp( caseName, "terrain-locate-nonfinite" ) == 0;
    const bool terrainLocateUnrepresentable = std::strcmp( caseName, "terrain-locate-unrepresentable" ) == 0;
    const bool terrainLocateInvalidScale = std::strcmp( caseName, "terrain-locate-invalid-scale" ) == 0;

    if ( terrainLocateCellRange || terrainLocateNonFinite || terrainLocateUnrepresentable || terrainLocateInvalidScale )
    {
        char temporaryDirectory[MAX_PATH] = {};
        char heightMapPath[MAX_PATH] = {};

        if ( GetTempPathA( MAX_PATH, temporaryDirectory ) == 0 ||
             GetTempFileNameA( temporaryDirectory, "sbt", 0, heightMapPath ) == 0 )
        {
            return false;
        }

        HANDLE heightMap = CreateFileA( heightMapPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                        nullptr );

        if ( heightMap == INVALID_HANDLE_VALUE )
        {
            DeleteFileA( heightMapPath );
            return false;
        }

        constexpr std::array<unsigned char, 16> PIXELS = {};
        DWORD written = 0u;
        const bool wroteHeightMap = WriteFile( heightMap, PIXELS.data(), static_cast<DWORD>( PIXELS.size() ), &written,
                                               nullptr ) != FALSE &&
                                    written == static_cast<DWORD>( PIXELS.size() );

        CloseHandle( heightMap );

        SkullbonezCore::Core::EngineConfig config;
        config.terrainGeometry.scale = 1.0f;
        std::unique_ptr<SkullbonezCore::Geometry::Terrain> terrain;
        const SbResult created = wroteHeightMap
                                     ? SkullbonezCore::Geometry::Terrain::TryCreatePhysicsFromHeightMap( diagnostics,
                                                                                                         heightMapPath, 4, 1,
                                                                                                         1, config, terrain )
                                     : diagnostics.Failure( "Tests/Terrain", "height-map write failed" );

        DeleteFileA( heightMapPath );

        if ( !created.Ok() || !terrain )
        {
            return false;
        }

        if ( terrainLocateInvalidScale )
        {
            config.terrainGeometry.scale = ( std::numeric_limits<float>::quiet_NaN )();
        }

        // The exact upper X edge maps to cell 3 when only cells 0..2 exist.
        // NaN and an invalid scale must terminate before floor-to-integer
        // conversion; a finite maximum float must terminate before the integer
        // cast. These probes exercise LocatePolygon's local query guards.
        const float xPosition = terrainLocateNonFinite
                                    ? ( std::numeric_limits<float>::quiet_NaN )()
                                    : ( terrainLocateUnrepresentable ? ( std::numeric_limits<float>::max )() : 3.0f );

        (void)terrain->LocatePolygon( xPosition, 0.0f );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-foreign-page-boundary" ) == 0 )
    {
        SYSTEM_INFO systemInfo = {};
        GetSystemInfo( &systemInfo );
        const std::size_t pageSize = static_cast<std::size_t>( systemInfo.dwPageSize );
        void* region = VirtualAlloc( nullptr, pageSize * 2u, MEM_RESERVE, PAGE_NOACCESS );

        if ( !region )
        {
            return false;
        }

        auto* committedPage = static_cast<unsigned char*>( region ) + pageSize;

        if ( VirtualAlloc( committedPage, pageSize, MEM_COMMIT, PAGE_READWRITE ) != committedPage )
        {
            VirtualFree( region, 0u, MEM_RELEASE );
            return false;
        }

        // The candidate begins eight bytes inside the inaccessible page, but
        // its magic field is readable in the committed page. A magic-only
        // probe would admit it and then fault on raw; the whole-header copy
        // must classify it as unreadable.
        auto* foreignPointer = committedPage + sizeof( ForeignAllocationHeaderLayout ) - sizeof( uint64_t );
        auto* candidate = foreignPointer - sizeof( ForeignAllocationHeaderLayout );
        auto* readableMagic = reinterpret_cast<uint32_t*>( candidate + offsetof( ForeignAllocationHeaderLayout, magic ) );
        *readableMagic = FOREIGN_ALLOCATION_HEADER_MAGIC;

        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        ::operator delete( foreignPointer );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-foreign-shaped-header" ) == 0 )
    {
        auto* candidate = static_cast<ForeignAllocationHeaderLayout*>(
            std::malloc( sizeof( ForeignAllocationHeaderLayout ) ) );

        if ( !candidate )
        {
            return false;
        }

        SYSTEM_INFO systemInfo = {};
        GetSystemInfo( &systemInfo );
        candidate->raw = VirtualAlloc( nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

        if ( !candidate->raw )
        {
            std::free( candidate );
            return false;
        }

        candidate->size = 64u;
        candidate->phase = static_cast<uint32_t>( RuntimeAllocationPhase::Diagnostics );
        candidate->magic = FOREIGN_ALLOCATION_HEADER_MAGIC;
        candidate->ownershipCookie = 0u;
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        ::operator delete( candidate + 1 );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-size-overflow" ) == 0 )
    {
        static_cast<void>( ::operator new( ( std::numeric_limits<std::size_t>::max )() ) );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-foreign-crt-release" ) == 0 )
    {
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode(
            SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Measure );
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        void* foreignPointer = std::malloc( 64u );

        if ( !foreignPointer )
        {
            return false;
        }

        ::operator delete( foreignPointer );
        const bool counted = SkullbonezCore::Core::Allocation::RuntimeAllocationForeignFreeCount() == 1u;

        const bool guardFailed = SkullbonezCore::Core::Allocation::RuntimeAllocationGuardHasGameplayViolations();

        SkullbonezCore::Core::Allocation::PrintRuntimeAllocationSummary( stdout );
        return counted && guardFailed;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-compile-capacity" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 2>
            values( "fatal.physics-fixed-list.compile",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );

        RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
        values.Reserve( 3u );
        return true;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-phase" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 2>
            values( "fatal.physics-fixed-list.phase", SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );
        values.Reserve( 1u );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-nan" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 8u );
        }
        grid.Insert( 7, Vector3( std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f ), 1.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-extent" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 12u );
        }
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

    if ( std::strcmp( caseName, "spatial-grid-reserve-phase" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );
        grid.ReserveSceneCapacity( 1u );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-entry-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 1u );
        }

        grid.BeginFrame( 1 );
        RuntimeAllocationScope physicsScope( RuntimeAllocationPhase::Physics );
        grid.Insert( 0, Vector3( 0.25f, 0.25f, 0.25f ), 5.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-overlay-entry-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 2u );
        }

        grid.BeginFrame( 2 );
        RuntimeAllocationScope physicsScope( RuntimeAllocationPhase::Physics );
        grid.InsertSwept( 0, Vector3( 0.25f, 0.25f, 0.25f ), Vector3( 2047.0f, 0.0f, 0.0f ), 0.0f );
        grid.InsertSwept( 1, Vector3( 5000.25f, 0.25f, 0.25f ), Vector3( 2050.0f, 0.0f, 0.0f ), 0.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-bucket-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( SpatialGrid::MAX_BUCKETS );
        }

        constexpr int persistentCells = SpatialGrid::MAX_BUCKETS / 2;

        for ( int cell = 0; cell < persistentCells; ++cell )
        {
            grid.Insert( cell, Vector3( static_cast<float>( cell ) + 0.25f, 0.25f, 0.25f ), 0.0f );
        }

        // Hazard: repeated Insert calls now move one persistent body. Fill the
        // remaining legal cells through the bounded swept-overlay path, then
        // request one genuinely new cell to exercise the Lane F table limit.
        const Vector3 sweepStart( static_cast<float>( persistentCells ) + 0.25f, 0.25f, 0.25f );
        grid.InsertSwept( persistentCells, sweepStart, Vector3( static_cast<float>( persistentCells - 1 ), 0.0f, 0.0f ),
                          0.0f );

        grid.Insert( persistentCells + 1, Vector3( static_cast<float>( SpatialGrid::MAX_BUCKETS ) + 0.25f, 0.25f, 0.25f ),
                     0.0f );

        return true;
    }

    if ( std::strcmp( caseName, "sleep-support-edge-reserved-capacity" ) == 0 )
    {
        static SkullbonezCore::Physics::PhysicsCandidatePairList
            edges { "TestRuntimeContracts.sleepSupportEdgesReserved",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            edges.Reserve( 2u );
        }
        edges.clear();
        AppendSleepSupportEdge( edges, 0, 1 );
        AppendSleepSupportEdge( edges, 1, 2 );

        // Hazard: requested=3 is far below the semantic ceiling. The owner must
        // still fail before PhysicsFixedList can silently exceed the actual
        // scene-committed reservation of two rows.
        AppendSleepSupportEdge( edges, 2, 3 );
        return true;
    }

    if ( std::strcmp( caseName, "sleep-support-edge-capacity" ) == 0 )
    {
        static SkullbonezCore::Physics::PhysicsCandidatePairList
            edges { "TestRuntimeContracts.sleepSupportEdges",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
                SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            edges.Reserve( MAX_SLEEP_SUPPORT_EDGES );
        }
        edges.clear();

        for ( std::size_t edgeIndex = 0; edgeIndex <= MAX_SLEEP_SUPPORT_EDGES; ++edgeIndex )
        {
            AppendSleepSupportEdge( edges, 0, 1 );
        }

        return true;
    }

    if ( std::strcmp( caseName, "amortized-task-in-flight-destroy" ) == 0 )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( 1 );
        std::atomic<bool> started { false };
        std::atomic<bool> release { false };
        {
            AmortizedTask task( 1, 1,
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

    if ( std::strcmp( caseName, "scene-capacity-hard-ceiling" ) == 0 )
    {
        auto engine = std::make_unique<PhysicsEngine>();
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        engine->ReserveAuthoredBodyCapacity( 9000u, 9000u, 0u, 0u, 0u );
        return true;
    }

    if ( std::strcmp( caseName, "point-joint-scene-capacity" ) == 0 )
    {
        auto engine = std::make_unique<PhysicsEngine>();
        SkullbonezCore::Physics::PhysicsBodyHandle bodies[2];
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            engine->ReserveAuthoredBodyCapacity( 2u, 2u, 0u, 0u, 12u );
            engine->ReserveAuthoredBodyCapacity( 2u, 2u, 0u, 0u, 8u );

            const SkullbonezCore::Math::CollisionDetection::CollisionShape
                shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );

            for ( uint32_t bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex )
            {
                const SkullbonezCore::Physics::PhysicsSceneObjectId sceneObjectId { bodyIndex + 1u };
                const auto bodyDesc = SkullbonezCore::Physics::
                    MakePhysicsBodyCreateDesc( sceneObjectId, shape,
                                               Vector3( static_cast<float>( bodyIndex ) * 3.0f, 0.0f, 0.0f ),
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                                               SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic,
                                               "fatal-point-joint-body" );

                auto colliderDesc = SkullbonezCore::Physics::MakeColliderCreateDesc( shape, 0.0f, 0u, "fatal" );
                colliderDesc.sceneObjectId = sceneObjectId;
                const auto registration = engine->RegisterAuthoredBody( bodyDesc, colliderDesc );

                if ( !registration.IsValid() )
                {
                    return false;
                }

                bodies[bodyIndex] = registration.body;
            }
        }

        SkullbonezCore::Physics::PhysicsPointJointCreateDesc desc;
        desc.bodyA = bodies[0];
        desc.bodyB = bodies[1];

        for ( int jointIndex = 0; jointIndex < 9; ++jointIndex )
        {
            (void)engine->CreatePointJoint( desc );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-capacity" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        std::array<SkullbonezCore::Core::SbResult, SkullbonezCore::Core::SbDiagnosticStore::CAPACITY + 1u> leases;

        for ( std::size_t index = 0; index < leases.size(); ++index )
        {
            leases[index] = store->Failure( "FatalCapacity", "slot=%zu", index );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-double-release" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        const SkullbonezCore::Core::SbResult lease = store->Failure( "FatalRelease", "double release" );
        const SkullbonezCore::Core::SbDiagnosticIdentity identity = lease.DiagnosticIdentity();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::Release( *store, identity.token );
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::Release( *store, identity.token );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-owner-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        std::array<char, SkullbonezCore::Core::SbDiagnosticStore::OWNER_CAPACITY + 1u> owner = {};
        owner.fill( 'o' );
        owner.back() = '\0';
        (void)store->Failure( owner.data(), "owner overflow" );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-store-destroyed-with-active-lease" ) == 0 )
    {
        SkullbonezCore::Core::SbResult escapedLease;
        {
            SkullbonezCore::Core::SbDiagnosticStore store;
            escapedLease = store.Failure( "FatalLifetime", "lease escapes store scope" );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-lease-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        const SkullbonezCore::Core::SbResult lease = store->Failure( "FatalLeaseOverflow", "saturate lease count" );
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::SaturateLeaseCount( *store, lease.DiagnosticIdentity().token );
        const SkullbonezCore::Core::SbResult copy = lease;
        return copy.Ok();
    }

    if ( std::strcmp( caseName, "sb-diagnostic-generation-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::ExhaustFirstGeneration( *store );
        (void)store->Failure( "FatalGenerationOverflow", "generation must not wrap" );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-lock-reentry" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::ReenterLock( *store );
        return true;
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
                        SkullbonezCore::Core::EngineLog::Get().WriteEventf( "runtime_contract_log_test thread=%d write=%d",
                                                                            threadIndex, writeIndex );
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
    CHECK( static_cast<int>( std::count( contents.begin(), contents.end(), '\n' ) ) == threadCount * writesPerThread );
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
    std::atomic<bool> started { false };
    std::atomic<bool> release { false };
    AmortizedTask inFlightTask( 1, 1,
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

TEST_CASE( "AmortizedTask: partial work resumes at the first unfinished item" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    std::array<int, 3> rangeBegins = {};
    int invocationCount = 0;
    AmortizedTask task( 5, 5,
                        [&]( int begin, int end ) -> int
                        {
                            rangeBegins[static_cast<std::size_t>( invocationCount )] = begin;
                            ++invocationCount;
                            return (std::min)( 2, end - begin );
                        } );

    task.SubmitTick( inlinePool );
    CHECK( task.GetProgress() == doctest::Approx( 0.4f ) );
    task.SubmitTick( inlinePool );
    CHECK( task.GetProgress() == doctest::Approx( 0.8f ) );
    task.SubmitTick( inlinePool );

    CHECK( task.IsComplete() );
    CHECK( rangeBegins == std::array<int, 3> { 0, 2, 4 } );
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
        CHECK( pool.GetThreadCount() == WorkerPool::ResolveThreadCount( threadCount ) );
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
#if defined( _WIN64 )
    static_assert( sizeof( SbResult ) == 16 );
#endif

    const SbResult success = SbResult::Success();
    CHECK( success.Ok() );
    CHECK( std::strcmp( success.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( success.ErrorMessage(), "" ) == 0 );

    SbResult reassignedSuccess = diagnostics.Failure( "Discarded", "discarded failure" );
    reassignedSuccess = success;
    CHECK( reassignedSuccess.Ok() );
    CHECK( std::strcmp( reassignedSuccess.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( reassignedSuccess.ErrorMessage(), "" ) == 0 );

    const SbResult failure = diagnostics.Failure( "SceneParser", "invalid body %d", 17 );
    CHECK_FALSE( failure.Ok() );
    CHECK( std::strcmp( failure.ErrorOwner(), "SceneParser" ) == 0 );
    CHECK( std::strcmp( failure.ErrorMessage(), "invalid body 17" ) == 0 );

    const SbResult copiedFailure = failure;
    CHECK_FALSE( copiedFailure.Ok() );
    CHECK( std::strcmp( copiedFailure.ErrorOwner(), "SceneParser" ) == 0 );
    CHECK( std::strcmp( copiedFailure.ErrorMessage(), "invalid body 17" ) == 0 );

    const SbResult defaultFailure = diagnostics.Failure( nullptr, nullptr );
    CHECK_FALSE( defaultFailure.Ok() );
    CHECK( std::strcmp( defaultFailure.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( defaultFailure.ErrorMessage(), "recoverable operation failed" ) == 0 );
}


TEST_CASE( "DX12 retirement diagnostics retain real peaks and reset at device boundaries" )
{
    using SkullbonezCore::Rendering::Dx12RetirementDiagnosticState;

    Dx12RetirementDiagnosticState retirementDiagnostics;
    retirementDiagnostics.ObservePendingCount( 3u );
    retirementDiagnostics.ObservePendingCount( 2u );
    CHECK( retirementDiagnostics.PendingHighWater() == 3u );
    CHECK( retirementDiagnostics.PendingHighWater() !=
           SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );

    retirementDiagnostics.ObserveRelease( 9u, 4u, true, 77u );
    CHECK( retirementDiagnostics.LastReleaseInputCount() == 9u );
    CHECK( retirementDiagnostics.LastReleasedCount() == 5u );
    CHECK( retirementDiagnostics.LastReleaseSurvivorCount() == 4u );
    CHECK( retirementDiagnostics.LastFrameFenceReady() );
    CHECK( retirementDiagnostics.LastObservedCompletedFence() == 77u );

    retirementDiagnostics.ObserveRelease( 4u, 4u, false, 0u );
    CHECK( retirementDiagnostics.LastReleaseInputCount() == 4u );
    CHECK( retirementDiagnostics.LastReleasedCount() == 0u );
    CHECK( retirementDiagnostics.LastReleaseSurvivorCount() == 4u );
    CHECK_FALSE( retirementDiagnostics.LastFrameFenceReady() );
    CHECK( retirementDiagnostics.LastObservedCompletedFence() == 77u );

    SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;
    SkullbonezCore::Rendering::Dx12DeferredReleaseOwnerTestAccess::ObservePendingCount( retirements, 2u );
    CHECK( retirements.HighWater() == 2u );
    retirements.ResetForDevice();
    CHECK( retirements.HighWater() == 0u );

    SkullbonezCore::Rendering::Dx12DeferredReleaseOwnerTestAccess::ObservePendingCount( retirements, 3u );
    CHECK( retirements.HighWater() == 3u );
    retirements.ResetAfterShutdown();
    CHECK( retirements.HighWater() == 0u );
}


TEST_CASE( "DX12 retirement exhaustion reports truthful queue and fence diagnostics" )
{
    ExpectFatalCase( "dx12-retirement-capacity",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=quarantine", "capacity=512 count=512 high_water=512",
                       "last_release_input=0 last_released=0 last_survivors=0", "fence_ready=0 last_completed_fence=0" } );
    ExpectFatalCase( "dx12-retirement-release-snapshot",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=quarantine", "capacity=512 count=512 high_water=512",
                       "last_release_input=9 last_released=5 last_survivors=4", "fence_ready=1 last_completed_fence=77" } );
    ExpectFatalCase( "dx12-retirement-reset-live-device",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=device_reset", "count=1" } );
    ExpectFatalCase( "dx12-retirement-reset-live-shutdown",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=shutdown_reset", "count=1" } );
}


TEST_CASE( "SbDiagnosticStore bound capacity and lease misuse terminate in child probes" )
{
    ExpectFatalCase( "sb-diagnostic-capacity", { "FATAL[Core/SbDiagnosticStore]", "all 256 diagnostic slots are leased" } );
    ExpectFatalCase( "sb-diagnostic-double-release",
                     { "FATAL[Core/SbDiagnosticStore]", "release used a stale or already released diagnostic token" } );

    ExpectFatalCase( "sb-diagnostic-owner-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic owner exceeds 95-byte bound" } );

    ExpectFatalCase( "sb-diagnostic-store-destroyed-with-active-lease",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic store destroyed with 1 active entries" } );

    ExpectFatalCase( "sb-diagnostic-lease-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic lease count overflowed" } );

    ExpectFatalCase( "sb-diagnostic-generation-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic generation exhausted for slot 0" } );

    ExpectFatalCase( "sb-diagnostic-lock-reentry",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic store lock re-entered by its owning thread" } );
}

TEST_CASE( "Runtime contracts: invalid broadphase and task lifetimes terminate in child probes" )
{
    ExpectFatalCase( "physics-pipeline-batch-full-mode",
                     { "FATAL[Physics/PhysicsStepDiagnostics]",
                       "Count-only pipeline event batches cannot be recorded while full payload retention is active" } );

    ExpectFatalCase( "physics-fixed-list-runtime-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=fatal.physics-fixed-list.runtime", "requested=2",
                       "runtime_capacity=1", "compile_capacity=4", "ceiling=runtime_reservation" } );

    ExpectFatalCase( "physics-fixed-list-compile-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=fatal.physics-fixed-list.compile", "requested=3",
                       "runtime_capacity=0", "compile_capacity=2", "ceiling=compile_time_ceiling" } );

    ExpectFatalCase( "physics-fixed-list-phase",
                     { "FATAL: PhysicsFixedList reserve denied", "owner=fatal.physics-fixed-list.phase", "requested=1",
                       "runtime_capacity=0", "compile_capacity=2", "phase=startup" } );

    ExpectFatalCase( "physics-prediction-seed-wrong-replay-owner",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope",
                       "owner_name=replay_solver_snapshot", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-missing-scope",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=startup", "owner=0",
                       "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-scene-load",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=scene_load",
                       "owner=0", "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-missing-owner",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=replay", "owner=0",
                       "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "terrain-locate-cell-range",
                     { "FATAL[Terrain]", "Terrain polygon cell out of range", "worldXCell=3", "quadsPerSide=3" } );

    ExpectFatalCase( "terrain-locate-nonfinite", { "FATAL[Terrain]", "Terrain polygon query is not finite", "x=nan" } );

    ExpectFatalCase( "terrain-locate-unrepresentable", { "FATAL[Terrain]", "Terrain polygon cell is not representable" } );

    ExpectFatalCase( "terrain-locate-invalid-scale",
                     { "FATAL[Terrain]", "Terrain polygon query is not finite", "scaledStepSize=nan" } );

    ExpectFatalCase( "spatial-grid-nan",
                     { "FATAL[Physics/SpatialGrid]", "body=7", "min=(nan,-1,-1)", "max_world_coordinate=100000" } );

    ExpectFatalCase( "spatial-grid-extent",
                     { "FATAL[Physics/SpatialGrid]", "body=11", "max=(100002,1,1)", "max_world_coordinate=100000" } );

    ExpectFatalCase( "spatial-grid-zero-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-nan-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=nan", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-tiny-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0.25", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-reserve-phase",
                     { "FATAL: PhysicsFixedList reserve denied", "owner=SpatialGrid.entries", "requested=1032",
                       "runtime_capacity=0", "compile_capacity=69636", "phase=startup" } );

    ExpectFatalCase( "spatial-grid-entry-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=SpatialGrid.entries", "requested=1033",
                       "runtime_capacity=1032", "compile_capacity=69636", "high_water=1032", "phase=physics" } );

    ExpectFatalCase( "spatial-grid-overlay-entry-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=SpatialGrid.overlayEntries", "requested=4097",
                       "runtime_capacity=4096", "compile_capacity=4096", "high_water=4096", "phase=physics" } );

    ExpectFatalCase( "spatial-grid-bucket-capacity", { "FATAL[Physics/SpatialGrid]", "bucket capacity exceeded",
                                                       "capacity=8192", "active=8192", "phase=steady_gameplay" } );

    ExpectFatalCase( "sleep-support-edge-reserved-capacity",
                     { "FATAL[Physics/SleepSupportEdges]", "Sleep support edge capacity exceeded", "requested=3",
                       "capacity=32768", "reserved_capacity=2", "high_water=2", "phase=steady_gameplay" } );

    ExpectFatalCase( "sleep-support-edge-capacity",
                     { "FATAL[Physics/SleepSupportEdges]", "Sleep support edge capacity exceeded", "requested=32769",
                       "capacity=32768", "high_water=32768", "phase=steady_gameplay" } );

    ExpectFatalCase( "amortized-task-in-flight-destroy",
                     { "FATAL[Core/AmortizedTask]", "Destroying AmortizedTask while worker chunk is in flight" } );

    ExpectFatalCase( "worker-fatal-log", { "FATAL[Tests/WorkerFatalProbe]", "worker-thread fatal logging probe" } );
    ExpectFatalCase( "replay-restore-pending-timeline-complete",
                     { "FATAL[Runtime/ReplayRestoreTransaction]",
                       "Restore completion reached without satisfying branch timeline state", "required=1", "applied=0" } );

    ExpectFatalCase( "replay-restore-unproved-rollback", { "FATAL[Runtime/ReplayRestoreTransaction]",
                                                           "Rollback completed without verified live-backup application",
                                                           "mutated=1", "backup=1", "applied=0" } );

    ExpectFatalCase( "scene-capacity-hard-ceiling", { "FATAL[Physics/SceneCapacity]", "owner=Physics/PhysicsEngine",
                                                      "requested_bodies=9000", "ceiling=8192" } );

    ExpectFatalCase( "point-joint-scene-capacity", { "FATAL[Physics/PointJoint]", "owner=Physics/PhysicsWorld",
                                                     "requested=9", "capacity=8", "retained_capacity=12" } );

#if defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) || defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
    ExpectFatalCase( "allocation-foreign-page-boundary",
                     { "FATAL[Runtime/Allocation]", "unprovable foreign pointer delete", "phase=diagnostics", "owner=0",
                       "header=unreadable", "foreign_free_count=1" } );

    ExpectFatalCase( "allocation-foreign-shaped-header",
                     { "FATAL[Runtime/Allocation]", "unprovable foreign pointer delete", "phase=diagnostics", "owner=0",
                       "header=bad_provenance", "foreign_free_count=1" } );
#else
    ExpectCleanChildCase( "allocation-foreign-crt-release",
                          { "[allocation-guard] FOREIGN_FREE", "phase=diagnostics", "owner=0", "header=bad_magic",
                            "foreign_free_count=1", "mode=measure", "foreign_frees=1", "VIOLATION:" } );
#endif
    ExpectFatalCase( "allocation-size-overflow", { "FATAL[Runtime/Allocation]", "global operator new failed",
                                                   "reason=size_arithmetic_overflow", "size=18446744073709551615" } );
}

TEST_CASE( "Persistent contact solve transaction enforces every phase edge through Lane F" )
{
    using SkullbonezCore::Physics::PersistentContactSolvePhaseCursor;
    using SkullbonezCore::Physics::PersistentContactSolveTransaction;
    using SkullbonezCore::Physics::PersistentContactSolveTransactionTestAccess;
    using Phase = PersistentContactSolvePhaseCursor::Phase;
    constexpr std::array phases { Phase::Idle,
                                  Phase::EntryPolicySetup,
                                  Phase::BodySetup,
                                  Phase::BuildManifolds,
                                  Phase::TerrainRows,
                                  Phase::Precompute,
                                  Phase::SolveRows,
                                  Phase::PointSupportInstability,
                                  Phase::TerrainRestPolicy,
                                  Phase::WriteBack,
                                  Phase::DebugContacts,
                                  Phase::PositionCorrection,
                                  Phase::CacheStore,
                                  Phase::FixedContactRelease,
                                  Phase::Complete,
                                  Phase::Count };
    constexpr std::size_t entryIndex = 1u;
    constexpr std::size_t terrainRowsIndex = 4u;
    constexpr std::size_t completeIndex = phases.size() - 2u;

    for ( std::size_t fromIndex = 0u; fromIndex < phases.size(); ++fromIndex )
    {

        for ( std::size_t toIndex = 0u; toIndex < phases.size(); ++toIndex )
        {
            const bool adjacent = fromIndex < completeIndex && toIndex == fromIndex + 1u;
            const bool emptyInput = fromIndex == entryIndex && toIndex == completeIndex;
            const bool emptyRows = fromIndex == terrainRowsIndex && toIndex == completeIndex;
            const bool expected = adjacent || emptyInput || emptyRows;
            CHECK( PersistentContactSolvePhaseCursor::IsLegalTransition( phases[fromIndex], phases[toIndex] ) == expected );

            // Count is a sentinel and cannot become the cursor's current state.

            if ( fromIndex == phases.size() - 1u || expected )
            {
                continue;
            }

            char caseName[96] = {};
            std::snprintf( caseName, sizeof( caseName ), "contact-solve-phase-%zu-%zu", fromIndex, toIndex );
            ExpectFatalCase( caseName, { "FATAL[Physics/PersistentContactSolveTransaction]", "Illegal phase transition",
                                         "operation=ExhaustiveFatalProbe" } );
        }
    }

    PersistentContactSolveTransaction transaction;
    CHECK( transaction.Phase() == Phase::Idle );

    for ( std::size_t phaseIndex = 1u; phaseIndex <= completeIndex; ++phaseIndex )
    {
        PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
    }

    CHECK( transaction.Phase() == Phase::Complete );
    static_assert( !std::is_copy_constructible_v<PersistentContactSolveTransaction> );
    static_assert( !std::is_copy_assignable_v<PersistentContactSolveTransaction> );
}

TEST_CASE( "Operator command transaction enforces every phase edge through Lane F" )
{
    using SkullbonezCore::Runtime::OperatorCommandPhaseCursor;
    using SkullbonezCore::Runtime::OperatorCommandTransaction;
    using SkullbonezCore::Runtime::OperatorCommandTransactionTestAccess;
    using Phase = OperatorCommandPhaseCursor::Phase;
    constexpr std::array phases { Phase::Idle,
                                  Phase::DeviceAndMode,
                                  Phase::PhysicsControl,
                                  Phase::RuntimePresentation,
                                  Phase::SimulationPolicy,
                                  Phase::PhysicsMaterial,
                                  Phase::WorldPolicy,
                                  Phase::CinematicPolicy,
                                  Phase::Complete,
                                  Phase::Count };

    for ( std::size_t fromIndex = 0u; fromIndex < phases.size(); ++fromIndex )
    {

        for ( std::size_t toIndex = 0u; toIndex < phases.size(); ++toIndex )
        {
            const bool expected = fromIndex < phases.size() - 2u && toIndex == fromIndex + 1u;
            CHECK( OperatorCommandPhaseCursor::IsLegalTransition( phases[fromIndex], phases[toIndex] ) == expected );

            // Count is a sentinel and cannot become the cursor's current state.

            if ( fromIndex == phases.size() - 1u || expected )
            {
                continue;
            }

            char caseName[96] = {};
            std::snprintf( caseName, sizeof( caseName ), "operator-command-phase-%zu-%zu", fromIndex, toIndex );
            ExpectFatalCase( caseName, { "FATAL[Runtime/OperatorCommandTransaction]", "Illegal phase transition",
                                         "operation=ExhaustiveFatalProbe" } );
        }
    }

    SkullbonezCore::UI::InGameUICommands commands;
    OperatorCommandTransaction transaction( commands );
    CHECK( transaction.Phase() == Phase::Idle );

    for ( std::size_t phaseIndex = 1u; phaseIndex < phases.size() - 1u; ++phaseIndex )
    {
        OperatorCommandTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
    }

    CHECK( transaction.Phase() == Phase::Complete );
    CHECK_FALSE( transaction.Acceptance().toggledVsync );
    static_assert( !std::is_copy_constructible_v<OperatorCommandTransaction> );
    static_assert( !std::is_copy_assignable_v<OperatorCommandTransaction> );
}
