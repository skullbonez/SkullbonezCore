// Purpose:
//   Lock allocation-tracker phase accounting and RuntimeReserveAllocator
//   policy contracts.

// Invariants:
//   - Gameplay-phase owners never receive replay growth approval.
//   - Replay growth grants are one-use, owner-specific, and byte-limited.
//   - Denied growth increments policy violations and still records an event.
//   - ResetCounters() clears counters/events without unregistering owners.
//   - RuntimeAllocationScope publishes/restores calling-thread phase
//     independently of guard mode and concurrent scopes.
//   - Development tool scopes do not mask an ordinary gameplay allocation.
//   - Tracker cases restore the process-wide guard to Off before returning.
//   - Rejected owner registrations never advance the fixed registry count.
//   - Registry-capacity probes run in a child because owners are process-lived.
//   - A new capacity session resets the visible and list-local peak lazily.
//   - Grow-only default extension preserves the existing admitted prefix and
//     value-initializes only newly admitted rows.
//   - Non-trivial fixed-list relocation moves every live element without
//     unwinding and destroys the retired prefix exactly once.

#include "../ThirdPtySource/doctest/doctest.h"

#include "TestFatalCases.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Physics/PhysicsFixedList.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPrediction.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <string>
#include <thread>
#include <type_traits>
#if defined( _WIN32 )
#include <process.h>
#endif

using SkullbonezCore::Core::Allocation::GetRuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::GetRuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::INVALID_RUNTIME_RESERVE_OWNER;
using SkullbonezCore::Core::Allocation::PrintRuntimeAllocationSummary;
using SkullbonezCore::Core::Allocation::ResetRuntimeAllocationCounters;
using SkullbonezCore::Core::Allocation::RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED;
using SkullbonezCore::Core::Allocation::RuntimeAllocationForeignFreeCount;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardEnabled;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardHasGameplayViolations;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardModeName;
using SkullbonezCore::Core::Allocation::RuntimeAllocationGuardViolationCount;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhaseName;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveAllocator;
using SkullbonezCore::Core::Allocation::RuntimeReserveCapacityView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthEventView;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthRequest;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthResult;
using SkullbonezCore::Core::Allocation::RuntimeReserveGrowthScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerDesc;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerHandle;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerScope;
using SkullbonezCore::Core::Allocation::RuntimeReserveOwnerStatsView;
using SkullbonezCore::Core::Allocation::RuntimeReservePhase;
using SkullbonezCore::Core::Allocation::RuntimeReserveSubsystem;
using SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode;
using SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase;
using SkullbonezCore::Physics::PhysicsFixedList;
using SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity;
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
using SkullbonezCore::Core::Allocation::CopyDevelopmentToolAllocationStats;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationOwner;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationScope;
using SkullbonezCore::Core::Allocation::DevelopmentToolAllocationStats;
using SkullbonezCore::Core::Allocation::ReleaseDevelopmentToolBackingMemory;
using SkullbonezCore::Core::Allocation::TryAccountDevelopmentToolBackingMemory;
#endif

static_assert( !std::is_copy_constructible_v<RuntimeReserveGrowthResult> );
static_assert( !std::is_copy_assignable_v<RuntimeReserveGrowthResult> );

namespace
{
constexpr const char*
    OWNER_REGISTRY_CHILD_SENTINEL_PATH = "TestOutput/validation/runtime_reserve_registry_capacity_child.ok";
constexpr const char* OWNER_REGISTRY_CHILD_SENTINEL_TEXT = "CORE-002 runtime reserve registry capacity child passed\n";

// Why: owner registration persists for the process lifetime; unique owner names
// let ResetCounters() clear diagnostics between cases without a registry teardown.
RuntimeReserveOwnerDesc MakeReplayOwnerDesc( const char* ownerName, int initialCapacity = 4, int hardCapacity = 10,
                                             int growthLimit = RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED )
{
    RuntimeReserveOwnerDesc desc = {};
    desc.ownerName = ownerName;
    desc.subsystem = RuntimeReserveSubsystem::Replay;
    desc.initPhase = RuntimeReservePhase::Startup;
    desc.initialCapacity = initialCapacity;
    desc.hardCapacity = hardCapacity;
    desc.replayGrowthLimit = growthLimit;
    desc.allowReplayGrowth = true;
    desc.capacityReason = "unit test reserve owner";
    return desc;
}

TEST_CASE( "RuntimeAllocationScope: lifecycle phase remains active when allocation counting is off" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Startup );
    {
        RuntimeAllocationScope renderScope( RuntimeAllocationPhase::Render );
        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
    }
    CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Startup );
}

TEST_CASE( "RuntimeAllocationScope: concurrent threads retain independent nested phases" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Startup );
    std::atomic<bool> workerEnteredReplay { false };
    std::atomic<bool> mainObservedRender { false };
    std::atomic<bool> workerPhasesCorrect { false };

    {
        RuntimeAllocationScope mainRenderScope( RuntimeAllocationPhase::Render );
        std::thread worker(
            [&]()
            {
                SetRuntimeAllocationPhase( RuntimeAllocationPhase::BackendInit );
                bool phasesCorrect = GetRuntimeAllocationPhase() == RuntimeAllocationPhase::BackendInit;
                {
                    RuntimeAllocationScope workerReplayScope( RuntimeAllocationPhase::Replay );
                    phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Replay;
                    workerEnteredReplay.store( true, std::memory_order_release );

                    while ( !mainObservedRender.load( std::memory_order_acquire ) )
                    {
                        std::this_thread::yield();
                    }

                    phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Replay;
                }
                phasesCorrect = phasesCorrect && GetRuntimeAllocationPhase() == RuntimeAllocationPhase::BackendInit;
                workerPhasesCorrect.store( phasesCorrect, std::memory_order_release );
            } );

        while ( !workerEnteredReplay.load( std::memory_order_acquire ) )
        {
            std::this_thread::yield();
        }

        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
        mainObservedRender.store( true, std::memory_order_release );
        worker.join();
        CHECK( workerPhasesCorrect.load( std::memory_order_acquire ) );
        CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Render );
    }
    CHECK( GetRuntimeAllocationPhase() == RuntimeAllocationPhase::Startup );
}

RuntimeReserveGrowthRequest MakeGrowthRequest( const char* ownerName, int oldCapacity, int requestedCapacity,
                                               RuntimeReservePhase phase = RuntimeReservePhase::Replay,
                                               uint64_t allocationBytes = 0u )
{
    RuntimeReserveGrowthRequest request = {};
    request.ownerName = ownerName;
    request.targetName = "unit-test-buffer";
    request.phase = phase;
    request.frameNumber = 42;
    request.oldCapacity = oldCapacity;
    request.requestedCapacity = requestedCapacity;
    request.elementSizeBytes = 16;
    request.allocationBytes = allocationBytes;
    return request;
}

RuntimeReserveGrowthEventView LatestGrowthEvent()
{
    RuntimeReserveGrowthEventView events[2] = {};
    REQUIRE( RuntimeReserveAllocator::CopyRecentGrowthEvents( events, 2 ) >= 1 );
    return events[0];
}

void CheckEventText( const char* actual, const char* expected )
{
    REQUIRE( actual != nullptr );
    CHECK( std::string( actual ) == expected );
}

std::string ReadFileText( FILE* file )
{
    std::string text;
    std::rewind( file );
    char buffer[1024] = {};

    while ( const size_t bytesRead = std::fread( buffer, 1u, sizeof( buffer ), file ) )
    {
        text.append( buffer, bytesRead );
    }

    return text;
}

void ExerciseOwnerRegistryCapacity()
{
    constexpr std::size_t concurrentCallers = 8u;
    constexpr const char* concurrentOwnerName = "unit.reserve.registry-capacity.concurrent";
    constexpr std::size_t registrationAttempts = 512u;
    std::array<std::thread, concurrentCallers> workers;
    std::array<RuntimeReserveOwnerHandle, concurrentCallers> concurrentHandles = {};
    std::atomic<std::size_t> readyCallers { 0u };
    std::atomic<bool> startRegistration { false };
    std::array<std::array<char, 64>, registrationAttempts> ownerNames = {};
    RuntimeReserveAllocator::ResetCounters();

    for ( std::size_t index = 0; index < workers.size(); ++index )
    {
        workers[index] = std::thread(
            [&, index]()
            {
                readyCallers.fetch_add( 1u, std::memory_order_release );

                while ( !startRegistration.load( std::memory_order_acquire ) )
                {
                    std::this_thread::yield();
                }

                concurrentHandles[index] = RuntimeReserveAllocator::RegisterOwner(
                    MakeReplayOwnerDesc( concurrentOwnerName ) );
            } );
    }

    while ( readyCallers.load( std::memory_order_acquire ) != workers.size() )
    {
        std::this_thread::yield();
    }

    startRegistration.store( true, std::memory_order_release );

    for ( std::thread& worker : workers )
    {
        worker.join();
    }

    REQUIRE( concurrentHandles[0] != INVALID_RUNTIME_RESERVE_OWNER );

    for ( const RuntimeReserveOwnerHandle owner : concurrentHandles )
    {
        REQUIRE( owner == concurrentHandles[0] );
    }

    RuntimeReserveOwnerHandle lastRegisteredOwner = INVALID_RUNTIME_RESERVE_OWNER;
    const char* lastRegisteredName = nullptr;
    int rejectedRegistrations = 0;

    for ( std::size_t index = 0; index < ownerNames.size(); ++index )
    {
        std::snprintf( ownerNames[index].data(), ownerNames[index].size(), "unit.reserve.registry-capacity.%zu", index );
        const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
            MakeReplayOwnerDesc( ownerNames[index].data() ) );

        if ( owner == INVALID_RUNTIME_RESERVE_OWNER )
        {
            ++rejectedRegistrations;
            continue;
        }

        REQUIRE( rejectedRegistrations == 0 );
        lastRegisteredOwner = owner;
        lastRegisteredName = ownerNames[index].data();
    }

    REQUIRE( lastRegisteredOwner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( lastRegisteredName != nullptr );
    REQUIRE( rejectedRegistrations > 0 );
    REQUIRE( RuntimeReserveAllocator::PolicyViolationCount() == static_cast<uint64_t>( rejectedRegistrations ) );
    REQUIRE( RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( lastRegisteredName ) ) == lastRegisteredOwner );

    // Test probe: reset and summary traverse every published registry row. A
    // rejected registration must not expand either traversal past fixed storage.
    RuntimeReserveAllocator::ResetCounters();
    REQUIRE_FALSE( RuntimeReserveAllocator::HasPolicyViolations() );
    FILE* summaryFile = nullptr;
    REQUIRE( tmpfile_s( &summaryFile ) == 0 );
    REQUIRE( summaryFile != nullptr );
    RuntimeReserveAllocator::PrintSummary( summaryFile );
    const std::string summary = ReadFileText( summaryFile );
    std::fclose( summaryFile );
    REQUIRE( summary.find( "registered_owners=159" ) != std::string::npos );

    std::error_code directoryError;
    std::filesystem::create_directories( "TestOutput/validation", directoryError );
    REQUIRE_FALSE( directoryError );
    FILE* sentinelFile = nullptr;
    REQUIRE( fopen_s( &sentinelFile, OWNER_REGISTRY_CHILD_SENTINEL_PATH, "wb" ) == 0 );
    REQUIRE( sentinelFile != nullptr );
    const std::size_t sentinelSize = std::strlen( OWNER_REGISTRY_CHILD_SENTINEL_TEXT );
    REQUIRE( std::fwrite( OWNER_REGISTRY_CHILD_SENTINEL_TEXT, 1u, sentinelSize, sentinelFile ) == sentinelSize );
    REQUIRE( std::fclose( sentinelFile ) == 0 );
}

bool EnsureOwnerRegistryCapacitySentinelAbsent()
{
    std::error_code removalError;
    std::filesystem::remove( OWNER_REGISTRY_CHILD_SENTINEL_PATH, removalError );

    if ( removalError )
    {
        return false;
    }

    std::error_code existenceError;
    const bool sentinelExists = std::filesystem::exists( OWNER_REGISTRY_CHILD_SENTINEL_PATH, existenceError );
    return !existenceError && !sentinelExists;
}

bool ConsumeOwnerRegistryCapacitySentinel()
{
    FILE* sentinelFile = nullptr;

    if ( fopen_s( &sentinelFile, OWNER_REGISTRY_CHILD_SENTINEL_PATH, "rb" ) != 0 || !sentinelFile )
    {
        return false;
    }

    const std::string sentinel = ReadFileText( sentinelFile );
    const int closeResult = std::fclose( sentinelFile );
    const bool sentinelRemoved = EnsureOwnerRegistryCapacitySentinelAbsent();
    return closeResult == 0 && sentinelRemoved && sentinel == OWNER_REGISTRY_CHILD_SENTINEL_TEXT;
}

int RunOwnerRegistryCapacityChild( bool& childCompleted )
{
    const char* executable = RuntimeTestExecutablePath();

    if ( !executable )
    {
        return -1;
    }

    // Why: Windows CRT spawn reconstructs a command line from argv strings.
    // A wildcard without spaces keeps the child selector one unambiguous token.
    constexpr const char* filter = "--test-case=*registry*capacity*child*probe";

    // Hazard: doctest exits successfully when a filter selects zero cases. A
    // stale success sentinel must be verifiably absent before that can happen.
    if ( !EnsureOwnerRegistryCapacitySentinelAbsent() )
    {
        return -1;
    }

    int childExit = -1;
#if defined( _WIN32 )
    childExit = static_cast<int>(
        _spawnl( _P_WAIT, executable, executable, filter, "--no-skip=true", static_cast<char*>( nullptr ) ) );
#else
    const std::string command = "\"" + std::string( executable ) + "\" " + filter + " --no-skip=true";
    childExit = std::system( command.c_str() );
#endif
    childCompleted = ConsumeOwnerRegistryCapacitySentinel();
    return childExit;
}
} // namespace

TEST_CASE( "RuntimeAllocationTracker: public mode and phase names cover every lifecycle label" )
{
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Off ) ) == "off" );
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Measure ) ) == "measure" );
    CHECK( std::string( RuntimeAllocationGuardModeName( RuntimeAllocationGuardMode::Gameplay ) ) == "gameplay" );
    CHECK( std::string( RuntimeAllocationGuardModeName( static_cast<RuntimeAllocationGuardMode>( 99 ) ) ) == "unknown" );

    const std::array<const char*, static_cast<size_t>( RuntimeAllocationPhase::Count )> expected =
        { "startup", "scene_load", "backend_init", "steady_gameplay", "physics",
          "render",  "replay",     "capture",      "diagnostics",     "shutdown" };

    for ( size_t index = 0; index < expected.size(); ++index )
    {
        CHECK( std::string( RuntimeAllocationPhaseName( static_cast<RuntimeAllocationPhase>( index ) ) ) ==
               expected[index] );
    }

    CHECK( std::string( RuntimeAllocationPhaseName( RuntimeAllocationPhase::Count ) ) == "unknown" );
}

TEST_CASE( "RuntimeAllocationTracker: measured allocations are attributed and freed in their source phase" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    REQUIRE( GetRuntimeAllocationGuardMode() == RuntimeAllocationGuardMode::Measure );
    REQUIRE( RuntimeAllocationGuardEnabled() );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );

    int* scalar = new int( 17 );
    int* array = new int[4] {};
    REQUIRE( scalar != nullptr );
    REQUIRE( array != nullptr );
    delete scalar;
    delete[] array;

    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    FILE* output = nullptr;
    REQUIRE( tmpfile_s( &output ) == 0 );
    REQUIRE( output != nullptr );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
    // Invariant: explicit allocation-function calls cannot be removed by the
    // standard new-expression elision that Clang applies at high optimization.
    void* reported = ::operator new( sizeof( int ) );
    ::operator delete( reported );
    PrintRuntimeAllocationSummary( output );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    const std::string summary = ReadFileText( output );
    std::fclose( output );
    CHECK( summary.find( "mode=measure" ) != std::string::npos );
    CHECK( summary.find( "foreign_frees=0" ) != std::string::npos );
    CHECK( summary.find( "phase=diagnostics" ) != std::string::npos );
    CHECK( summary.find( "allocations=1" ) != std::string::npos );
    CHECK( summary.find( "frees=1" ) != std::string::npos );
    CHECK( summary.find( "PASS: no steady gameplay allocations" ) != std::string::npos );
}

TEST_CASE( "RuntimeAllocationTracker: gameplay guard reports a physics allocation violation" )
{
    FILE* output = nullptr;
    REQUIRE( tmpfile_s( &output ) == 0 );
    REQUIRE( output != nullptr );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Physics );

    void* value = ::operator new( sizeof( int ) );
    ::operator delete( value );
    const uint64_t violations = RuntimeAllocationGuardViolationCount();
    const bool hasViolations = RuntimeAllocationGuardHasGameplayViolations();
    PrintRuntimeAllocationSummary( output );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    const std::string summary = ReadFileText( output );
    std::fclose( output );
    CHECK( violations >= 1u );
    CHECK( hasViolations );
    CHECK( summary.find( "mode=gameplay" ) != std::string::npos );
    CHECK( summary.find( "phase=physics" ) != std::string::npos );
    CHECK( summary.find( "VIOLATION:" ) != std::string::npos );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
TEST_CASE( "Development tool allocation scopes remain separate without masking gameplay violations" )
{
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Render );

    {
        DevelopmentToolAllocationScope imguiScope( DevelopmentToolAllocationOwner::DearImGui );
        void* imguiBlock = ::operator new( 32u );
        ::operator delete( imguiBlock );
    }
    {
        DevelopmentToolAllocationScope tracyScope( DevelopmentToolAllocationOwner::Tracy );
        void* tracyBlock = ::operator new( 48u );
        ::operator delete( tracyBlock );
    }
    const bool tracyBackingReserved = TryAccountDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner::Tracy,
                                                                              64u * 1024u );

    const uint64_t toolScopeViolations = RuntimeAllocationGuardViolationCount();
    DevelopmentToolAllocationStats imguiStats;
    DevelopmentToolAllocationStats tracyStats;
    const bool copiedImGui = CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner::DearImGui, imguiStats );
    const bool copiedTracy = CopyDevelopmentToolAllocationStats( DevelopmentToolAllocationOwner::Tracy, tracyStats );

    if ( tracyBackingReserved )
    {
        ReleaseDevelopmentToolBackingMemory( DevelopmentToolAllocationOwner::Tracy, 64u * 1024u );
    }

    // Test probe: this unscoped allocation uses the same Render phase as
    // the tool calls. It must still fail the gameplay guard.
    void* gameplayBlock = ::operator new( 16u );
    ::operator delete( gameplayBlock );
    const uint64_t finalViolations = RuntimeAllocationGuardViolationCount();
    const bool guardFailed = RuntimeAllocationGuardHasGameplayViolations();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    CHECK( toolScopeViolations == 0u );
    CHECK( copiedImGui );
    CHECK( copiedTracy );
    CHECK( tracyBackingReserved );
    CHECK( imguiStats.allocations == 1u );
    CHECK( tracyStats.allocations == 2u );
    CHECK( imguiStats.highWaterBytes >= 32u );
    CHECK( tracyStats.activeBytes >= 64u * 1024u );
    CHECK( tracyStats.highWaterBytes >= 64u * 1024u );
    CHECK( imguiStats.hardCapBytes == 64 * 1024 * 1024 );
    CHECK( tracyStats.hardCapBytes == 512 * 1024 * 1024 );
    CHECK( finalViolations >= 1u );
    CHECK( guardFailed );
}
#endif

TEST_CASE( "RuntimeAllocationTracker: global allocation overloads preserve alignment and null-delete behavior" )
{
    struct alignas( 64 ) AlignedValue
    {
        unsigned char bytes[64];
    };

    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Measure );
    void* scalarNothrow = ::operator new( 7u, std::nothrow );
    void* arrayNothrow = ::operator new[]( 9u, std::nothrow );
    void* aligned = ::operator new( sizeof( AlignedValue ), std::align_val_t( alignof( AlignedValue ) ) );
    void* alignedArray = ::operator new[]( sizeof( AlignedValue ) * 2u, std::align_val_t( alignof( AlignedValue ) ),
                                           std::nothrow );

    REQUIRE( scalarNothrow != nullptr );
    REQUIRE( arrayNothrow != nullptr );
    REQUIRE( aligned != nullptr );
    REQUIRE( alignedArray != nullptr );
    CHECK( reinterpret_cast<uintptr_t>( aligned ) % alignof( AlignedValue ) == 0u );
    CHECK( reinterpret_cast<uintptr_t>( alignedArray ) % alignof( AlignedValue ) == 0u );

    ::operator delete( scalarNothrow, std::nothrow );
    ::operator delete[]( arrayNothrow, std::nothrow );
    ::operator delete( aligned, std::align_val_t( alignof( AlignedValue ) ) );
    ::operator delete[]( alignedArray, std::align_val_t( alignof( AlignedValue ) ), std::nothrow );
    ::operator delete( nullptr );
    ResetRuntimeAllocationCounters();
    CHECK( RuntimeAllocationGuardViolationCount() == 0u );
    CHECK( RuntimeAllocationForeignFreeCount() == 0u );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    PrintRuntimeAllocationSummary( nullptr );
    CHECK_FALSE( RuntimeAllocationGuardEnabled() );
}

TEST_CASE( "RuntimeReserveAllocator: owner registry exhaustion remains bounded" )
{
    // Lifetime: owner registrations persist for the process, so this capacity
    // probe runs in a child and cannot consume the parent suite's registry.
    bool childCompleted = false;
    const int childExit = RunOwnerRegistryCapacityChild( childCompleted );
    REQUIRE( childExit != -1 );
    CHECK( childExit == 0 );
    CHECK( childCompleted );
}

TEST_CASE( "RuntimeReserveAllocator: owner registry capacity child probe" * doctest::skip() )
{
    ExerciseOwnerRegistryCapacity();
}

TEST_CASE( "RuntimeReserveAllocator: replay growth under cap grants and records bytes" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.grant";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 4, 8 ) );

    CHECK( result.granted );
    CHECK( result.grantedCapacity == 8 );
    CHECK( result.growthCount == 1 );
    CHECK_FALSE( RuntimeReserveAllocator::HasPolicyViolations() );
    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 1u );

    const RuntimeReserveGrowthEventView event = LatestGrowthEvent();
    CHECK( event.granted );
    CheckEventText( event.ownerName, ownerName );
    CheckEventText( event.targetName, "unit-test-buffer" );
    CheckEventText( event.phaseName, "replay" );
    CheckEventText( event.reason, "granted" );
    CHECK( event.bytes == 64u );
    CHECK( event.oldCapacity == 4 );
    CHECK( event.requestedCapacity == 8 );
    CHECK( event.grantedCapacity == 8 );
    CHECK( event.elementSizeBytes == 16 );
    CHECK( event.growthCount == 1 );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth scope approves only the granted replay window" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.scope";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 4, 6, RuntimeReservePhase::Replay, 48u ) );

    REQUIRE( result.granted );

    CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
    {
        RuntimeReserveGrowthScope scope( owner, RuntimeReservePhase::Replay, result );
        CHECK( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 3 ) );
        CHECK( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 32u ) );
        CHECK_FALSE( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 17u ) );
        CHECK( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 16u ) );
        CHECK_FALSE( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 1u ) );
    }
    CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );

    // The scope consumed this result's private token; its public success value
    // cannot reopen the allocation window.
    RuntimeReserveGrowthScope reusedScope( owner, RuntimeReservePhase::Replay, result );
    CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
}


TEST_CASE( "RuntimeReserveAllocator: nested replay grants restore the outer byte allowance" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.nested-scope";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    RuntimeReserveGrowthResult outer = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 4, 8, RuntimeReservePhase::Replay, 64u ) );
    RuntimeReserveGrowthResult inner = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 8, 9, RuntimeReservePhase::Replay, 16u ) );
    REQUIRE( outer.granted );
    REQUIRE( inner.granted );

    RuntimeReserveGrowthScope outerScope( owner, RuntimeReservePhase::Replay, outer );
    CHECK( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 8u ) );
    {
        RuntimeReserveGrowthScope innerScope( owner, RuntimeReservePhase::Replay, inner );
        CHECK( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 16u ) );
        CHECK_FALSE( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 1u ) );
    }
    CHECK( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 56u ) );
    CHECK_FALSE( RuntimeReserveAllocator::TryConsumeApprovedReplayGrowthAllocation( owner, 6, 1u ) );
}


TEST_CASE( "RuntimeAllocationTracker: real replay allocations consume only their exact grant" )
{
    RuntimeReserveAllocator::ResetCounters();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    SetRuntimeAllocationPhase( RuntimeAllocationPhase::Replay );
    const uint64_t unownedPolicyViolations = RuntimeReserveAllocator::PolicyViolationCount();
    void* unowned = ::operator new( 7u );
    ::operator delete( unowned );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == unownedPolicyViolations );

    constexpr const char* ownerName = "unit.reserve.e1.operator-new";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    RuntimeReserveGrowthResult offResult = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 4, 6, RuntimeReservePhase::Replay, 32u ) );
    REQUIRE( offResult.granted );
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, offResult );
        void* exact = ::operator new( 32u );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
        RuntimeReserveOwnerStatsView activeStats = {};
        REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, activeStats ) );
        CHECK( activeStats.activeBytes == 32u );
        CHECK( activeStats.pendingReplayGrantBytes == 0u );
        ::operator delete( exact );
    }
    RuntimeReserveOwnerStatsView freedStats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, freedStats ) );
    CHECK( freedStats.activeBytes == 0u );
    CHECK( freedStats.pendingReplayGrantBytes == 0u );

    RuntimeReserveGrowthResult guardedResult = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 6, 8, RuntimeReservePhase::Replay, 32u ) );
    REQUIRE( guardedResult.granted );
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, guardedResult );
        const uint64_t violationsBefore = RuntimeAllocationGuardViolationCount();
        void* tooLarge = ::operator new( 33u );
        ::operator delete( tooLarge );
        CHECK( RuntimeAllocationGuardViolationCount() == violationsBefore + 1u );
        CHECK( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );

        void* exact = ::operator new( 32u );
        ::operator delete( exact );
        CHECK( RuntimeAllocationGuardViolationCount() == violationsBefore + 1u );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
    }
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
}


TEST_CASE( "Replay prediction archive candidate grant covers object and constructor backing allocations" )
{
    using SkullbonezCore::Runtime::ReplayPredictionReserveOperations::ReplayPredictionReserveOwner;
    using SkullbonezCore::Runtime::ReplayPredictionReserveOperations::RequestReplayPredictionReserveGrowth;
    using SkullbonezCore::Runtime::ReplayPredictionSolverEvidenceBanks;
    using SkullbonezCore::Runtime::RunReplayPredictionState;

    RuntimeReserveAllocator::ResetCounters();
    const uint64_t allocationBytes = sizeof( RunReplayPredictionState ) +
                                     sizeof( ReplayPredictionSolverEvidenceBanks ) +
                                     SkullbonezCore::Gameplay::TornadoGameplay::MAX_ACTIVE_FORCE_FIELDS *
                                         sizeof( SkullbonezCore::Gameplay::TornadoVortexConfig ) +
                                     2u * SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS * sizeof( float );
    REQUIRE( allocationBytes <= static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) );
    RuntimeReserveGrowthResult result = {};
    REQUIRE( RequestReplayPredictionReserveGrowth( "unit.archive.candidate", -1, 0,
                                                   static_cast<int>( allocationBytes ), 1, result,
                                                   allocationBytes ) );

    const RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const uint64_t violationsBefore = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, result );
        auto prediction = std::make_unique<RunReplayPredictionState>();
        auto evidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
        REQUIRE( prediction != nullptr );
        REQUIRE( evidence != nullptr );

        RuntimeReserveOwnerStatsView stats = {};
        REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, stats ) );
        CHECK( stats.activeBytes == allocationBytes );
        CHECK( stats.pendingReplayGrantBytes == 0u );
        CHECK( RuntimeAllocationGuardViolationCount() == violationsBefore );
    }
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth grant rejects a different owner without consuming its token" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.bound-owner";
    constexpr const char* wrongOwnerName = "unit.reserve.e1.wrong-owner";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    const RuntimeReserveOwnerHandle wrongOwner =
        RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( wrongOwnerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( wrongOwner != INVALID_RUNTIME_RESERVE_OWNER );
    RuntimeReserveGrowthResult result =
        RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) );
    REQUIRE( result.granted );

    {
        RuntimeReserveGrowthScope wrongScope( wrongOwner, RuntimeReservePhase::Replay, result );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( wrongOwner, 6 ) );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
    }

    {
        RuntimeReserveGrowthScope matchingScope( owner, RuntimeReservePhase::Replay, result );
        CHECK( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( owner, 6 ) );
        CHECK_FALSE( RuntimeReserveAllocator::IsApprovedReplayGrowthAllocation( wrongOwner, 6 ) );
    }
}


TEST_CASE( "RuntimeReserveAllocator: replay growth rejects an allocation budget larger than its backing request" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.oversized-allocation-budget";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth(
        owner, MakeGrowthRequest( ownerName, 4, 6, RuntimeReservePhase::Replay, 97u ) );

    CHECK_FALSE( result.granted );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );
    CheckEventText( LatestGrowthEvent().reason, "allocation_bytes_out_of_range" );
}


TEST_CASE( "RuntimeReserveAllocator: over-cap growth denies and records a policy violation" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.over-cap";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner( MakeReplayOwnerDesc( ownerName, 4, 6 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth( owner,
                                                                                      MakeGrowthRequest( ownerName, 4, 8 ) );

    CHECK_FALSE( result.granted );
    CHECK( result.grantedCapacity == 4 );
    CHECK( result.growthCount == 0 );
    CHECK( RuntimeReserveAllocator::HasPolicyViolations() );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );

    const RuntimeReserveGrowthEventView event = LatestGrowthEvent();
    CHECK_FALSE( event.granted );
    CheckEventText( event.reason, "capacity_out_of_range" );
    CHECK( event.bytes == 0u );
    CHECK( event.grantedCapacity == 4 );
}


TEST_CASE( "RuntimeReserveAllocator: replay growth count limit denies later bumps" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.limit";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 4, 12, 1 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    const RuntimeReserveGrowthResult first = RuntimeReserveAllocator::RequestGrowth( owner,
                                                                                     MakeGrowthRequest( ownerName, 4, 6 ) );

    const RuntimeReserveGrowthResult second = RuntimeReserveAllocator::RequestGrowth( owner,
                                                                                      MakeGrowthRequest( ownerName, 6, 8 ) );

    CHECK( first.granted );
    CHECK( first.growthCount == 1 );
    CHECK_FALSE( second.granted );
    CHECK( second.grantedCapacity == 6 );
    CHECK( second.growthCount == 1 );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );
    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 2u );
    CheckEventText( LatestGrowthEvent().reason, "growth_count_limit" );
}


TEST_CASE( "RuntimeReserveAllocator: replay byte owners share one active allocation cap" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.aggregate-bytes";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 0, 100 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    RuntimeReserveAllocator::RecordAllocation( owner, 6, 80u );
    // active + growth delta is 90 and would fit; active + the full 30-byte
    // replacement backing is 110 and must be rejected while the old backing
    // remains live.
    RuntimeReserveGrowthRequest request = MakeGrowthRequest( ownerName, 20, 30 );
    request.elementSizeBytes = 1;
    const RuntimeReserveGrowthResult result = RuntimeReserveAllocator::RequestGrowth( owner, request );

    CHECK_FALSE( result.granted );
    CHECK( RuntimeReserveAllocator::PolicyViolationCount() == 1u );
    CheckEventText( LatestGrowthEvent().reason, "owner_byte_budget" );

    RuntimeReserveOwnerStatsView stats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, stats ) );
    CHECK( stats.activeBytes == 80u );
    CHECK( stats.failedGrowths == 1u );
    RuntimeReserveAllocator::RecordFree( owner, 80u );
}


TEST_CASE( "RuntimeReserveAllocator: preissued replay grants cannot overbook one owner byte cap" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.pending-byte-cap";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 0, 100 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );

    RuntimeReserveGrowthRequest firstRequest = MakeGrowthRequest( ownerName, 0, 60, RuntimeReservePhase::Replay, 60u );
    firstRequest.elementSizeBytes = 1;
    RuntimeReserveGrowthRequest secondRequest = MakeGrowthRequest( ownerName, 0, 60, RuntimeReservePhase::Replay, 60u );
    secondRequest.elementSizeBytes = 1;
    RuntimeReserveGrowthResult first = RuntimeReserveAllocator::RequestGrowth( owner, firstRequest );
    const RuntimeReserveGrowthResult second = RuntimeReserveAllocator::RequestGrowth( owner, secondRequest );

    REQUIRE( first.granted );
    CHECK_FALSE( second.granted );
    CheckEventText( LatestGrowthEvent().reason, "owner_byte_budget" );
    RuntimeReserveOwnerStatsView pendingStats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, pendingStats ) );
    CHECK( pendingStats.activeBytes == 0u );
    CHECK( pendingStats.pendingReplayGrantBytes == 60u );

    {
        RuntimeReserveGrowthScope unusedScope( owner, RuntimeReservePhase::Replay, first );
    }
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, pendingStats ) );
    CHECK( pendingStats.pendingReplayGrantBytes == 0u );
}


TEST_CASE( "RuntimeReserveAllocator: ResetCounters clears growth events without unregistering owners" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.reset";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 4, 10, 1 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 6 ) ).granted );
    REQUIRE( RuntimeReserveAllocator::GrowthEventCount() == 1u );

    RuntimeReserveAllocator::ResetCounters();

    CHECK( RuntimeReserveAllocator::GrowthEventCount() == 0u );
    CHECK_FALSE( RuntimeReserveAllocator::HasPolicyViolations() );
    RuntimeReserveGrowthEventView events[1] = {};
    CHECK( RuntimeReserveAllocator::CopyRecentGrowthEvents( events, 1 ) == 0 );
    const RuntimeReserveGrowthResult afterReset = RuntimeReserveAllocator::RequestGrowth( owner,
                                                                                          MakeGrowthRequest( ownerName, 4,
                                                                                                             6 ) );

    CHECK( afterReset.granted );
    CHECK( afterReset.growthCount == 1 );
}


TEST_CASE( "RuntimeReserveAllocator: owner stats expose fixed-registry growth evidence by name" )
{
    RuntimeReserveAllocator::ResetCounters();
    constexpr const char* ownerName = "unit.reserve.e1.owner-stats";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 4, 12 ) );
    REQUIRE( owner != INVALID_RUNTIME_RESERVE_OWNER );
    REQUIRE( RuntimeReserveAllocator::RequestGrowth( owner, MakeGrowthRequest( ownerName, 4, 9 ) ).granted );

    RuntimeReserveOwnerStatsView stats = {};
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStatsByName( ownerName, stats ) );
    CHECK( std::string( stats.ownerName ) == ownerName );
    CHECK( stats.subsystem == RuntimeReserveSubsystem::Replay );
    CHECK( stats.initPhase == RuntimeReservePhase::Startup );
    CHECK( stats.currentCapacity == 9 );
    CHECK( stats.hardCapacity == 12 );
    CHECK( stats.highWaterCapacity == 9 );
    CHECK( stats.replayGrowths == 1u );
    CHECK( stats.failedGrowths == 0u );
    CHECK( stats.lastGrowthFrame == 42 );
    CHECK( stats.allowReplayGrowth );

    RuntimeReserveOwnerStatsView missing = {};
    CHECK_FALSE( RuntimeReserveAllocator::CopyOwnerStatsByName( "unit.reserve.e1.missing", missing ) );
}


TEST_CASE( "PhysicsFixedList: scene-load reserve fills exact runtime capacity through contiguous pointers" )
{
    using List = PhysicsFixedList<int, 8>;
    static_assert( std::ranges::contiguous_range<List> );
    static_assert( std::ranges::contiguous_range<const List> );

    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    List values( "unit.physics-fixed-list.reserve-fill", ExplicitTestCapacity );
    CHECK( values.owner_handle() != INVALID_RUNTIME_RESERVE_OWNER );
    CHECK( std::strcmp( values.capacity_reason(), ExplicitTestCapacity ) == 0 );
    CHECK( values.capacity() == 0u );
    CHECK( values.max_capacity() == 8u );
    values.Reserve( 3u );
    CHECK( values.capacity() == 3u );
    CHECK( reinterpret_cast<std::uintptr_t>( values.data() ) % 32u == 0u );

    values.push_back( 11 );
    values.push_back( 22 );
    values.push_back( 33 );
    CHECK( values.size() == values.capacity() );
    CHECK( values.high_water() == 3u );
    CHECK( values.end() - values.begin() == 3 );
    CHECK( values.data()[0] == 11 );
    CHECK( values.data()[2] == 33 );

    const auto findCapacityRow = []( const char* ownerName ) -> const RuntimeReserveCapacityView*
    {
        const std::span<const RuntimeReserveCapacityView> rows = RuntimeReserveAllocator::CapacityRows();

        const auto row = std::find_if( rows.begin(), rows.end(),
                                       [ownerName]( const RuntimeReserveCapacityView& candidate )
                                       {
                                           return candidate.ownerName && std::strcmp( candidate.ownerName, ownerName ) == 0;
                                       } );
        return row != rows.end() ? &*row : nullptr;
    };

    ResetRuntimeAllocationCounters();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    const RuntimeReserveCapacityView* filled = nullptr;
    {
        RuntimeAllocationScope steadyGameplay( RuntimeAllocationPhase::SteadyGameplay );
        filled = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    }
    const uint64_t queryAllocationViolations = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );

    CHECK( queryAllocationViolations == 0u );
    REQUIRE( filled != nullptr );
    CHECK( filled->subsystem == RuntimeReserveSubsystem::Physics );
    CHECK( filled->elementSizeBytes == static_cast<int>( sizeof( int ) ) );
    CHECK( filled->currentCapacity == 3 );
    CHECK( filled->liveCount == 3 );
    CHECK( filled->sessionHighWater == 3 );
    CHECK( filled->residentBytes == 3u * sizeof( int ) );

    values.clear();
    const RuntimeReserveCapacityView* cleared = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    REQUIRE( cleared != nullptr );
    CHECK( cleared->currentCapacity == 3 );
    CHECK( cleared->liveCount == 0 );
    CHECK( cleared->sessionHighWater == 3 );
    CHECK( cleared->residentBytes == 3u * sizeof( int ) );

    RuntimeReserveAllocator::BeginCapacitySession();
    values.push_back( 44 );
    const RuntimeReserveCapacityView* nextScene = findCapacityRow( "unit.physics-fixed-list.reserve-fill" );
    REQUIRE( nextScene != nullptr );
    CHECK( nextScene->currentCapacity == 3 );
    CHECK( nextScene->liveCount == 1 );
    CHECK( nextScene->sessionHighWater == 1 );

    FILE* capacityLog = nullptr;
    REQUIRE( tmpfile_s( &capacityLog ) == 0 );
    REQUIRE( capacityLog != nullptr );
    RuntimeReserveAllocator::PrintCapacityRows( capacityLog, "unit-capacity.scene", "scene_unload" );
    const std::string capacityText = ReadFileText( capacityLog );
    CHECK( capacityText.find( "[capacity] scene=\"unit-capacity.scene\" status=scene_unload" ) != std::string::npos );
    CHECK( capacityText.find( "owner=\"unit.physics-fixed-list.reserve-fill\"" ) != std::string::npos );
    CHECK( capacityText.find( "capacity=3 live=1 high_water=1 utilisation=33.33% resident_bytes=12" ) != std::string::npos );

    ResetRuntimeAllocationCounters();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    {
        RuntimeAllocationScope steadyGameplay( RuntimeAllocationPhase::SteadyGameplay );
        RuntimeReserveAllocator::PrintCapacityRows( capacityLog, "unit-capacity.scene", "scene_unload" );
    }
    const uint64_t warmedLogAllocationViolations = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    CHECK( warmedLogAllocationViolations == 0u );
    std::fclose( capacityLog );
}


TEST_CASE( "PhysicsFixedList: grow-only default extension preserves the admitted prefix" )
{
    PhysicsFixedList<int, 8> values( "unit.physics-fixed-list.extend-default", ExplicitTestCapacity );

    {
        RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
        values.Reserve( 2u );
        values.ExtendDefaultTo( 2u );
    }

    REQUIRE( values.size() == 2u );
    values[0] = 17;
    values[1] = 29;

    {
        RuntimeAllocationScope additionalSceneLoad( RuntimeAllocationPhase::SceneLoad );
        values.Reserve( 5u );
        values.ExtendDefaultTo( 5u );
    }

    REQUIRE( values.size() == 5u );
    CHECK( values[0] == 17 );
    CHECK( values[1] == 29 );
    CHECK( values[2] == 0 );
    CHECK( values[3] == 0 );
    CHECK( values[4] == 0 );
    CHECK( values.high_water() == 5u );

    values.ExtendDefaultTo( 3u );
    CHECK( values.size() == 5u );
    CHECK( values[0] == 17 );
    CHECK( values[1] == 29 );
}


TEST_CASE( "PhysicsFixedList: one canonical publisher survives same-name clone destruction" )
{
    using List = PhysicsFixedList<int, 8>;
    constexpr const char* ownerName = "unit.physics-fixed-list.canonical-publisher";
    const auto findCapacityRow = []( const char* targetOwner ) -> const RuntimeReserveCapacityView*
    {
        const std::span<const RuntimeReserveCapacityView> rows = RuntimeReserveAllocator::CapacityRows();

        const auto row = std::find_if( rows.begin(), rows.end(),
                                       [targetOwner]( const RuntimeReserveCapacityView& candidate )
                                       {
                                           return candidate.ownerName &&
                                                  std::strcmp( candidate.ownerName, targetOwner ) == 0;
                                       } );
        return row != rows.end() ? &*row : nullptr;
    };

    {
        RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
        List canonical( ownerName, ExplicitTestCapacity );
        canonical.Reserve( 3u );
        canonical.resize( 2u );

        {
            List sameNameClone( ownerName, ExplicitTestCapacity );
            sameNameClone.Reserve( 7u );
            sameNameClone.resize( 6u );

            const RuntimeReserveCapacityView* whileCloneLives = findCapacityRow( ownerName );
            REQUIRE( whileCloneLives != nullptr );
            CHECK( whileCloneLives->currentCapacity == 3 );
            CHECK( whileCloneLives->liveCount == 2 );
            CHECK( whileCloneLives->sessionHighWater == 2 );
            CHECK( whileCloneLives->residentBytes == 3u * sizeof( int ) );
        }

        const RuntimeReserveCapacityView* afterCloneDestruction = findCapacityRow( ownerName );
        REQUIRE( afterCloneDestruction != nullptr );
        CHECK( afterCloneDestruction->currentCapacity == 3 );
        CHECK( afterCloneDestruction->liveCount == 2 );
        CHECK( afterCloneDestruction->sessionHighWater == 2 );
        CHECK( afterCloneDestruction->residentBytes == 3u * sizeof( int ) );
    }

    const RuntimeReserveCapacityView* afterCanonicalDestruction = findCapacityRow( ownerName );
    REQUIRE( afterCanonicalDestruction != nullptr );
    CHECK( afterCanonicalDestruction->currentCapacity == 0 );
    CHECK( afterCanonicalDestruction->liveCount == 0 );
    CHECK( afterCanonicalDestruction->sessionHighWater == 2 );
    CHECK( afterCanonicalDestruction->residentBytes == 0u );
}


TEST_CASE( "PhysicsFixedList: runtime and compile-time ceilings remain distinct" )
{
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    PhysicsFixedList<int, 7> values( "unit.physics-fixed-list.ceilings", ExplicitTestCapacity );
    values.Reserve( 2u );

    CHECK( values.capacity() == 2u );
    CHECK( values.max_capacity() == 7u );
    values.reserve( 2u );
    CHECK( values.capacity() == 2u );
}


namespace
{
struct PhysicsFixedListRelocationValue
{
    explicit PhysicsFixedListRelocationValue( int initialValue = 0 ) : value( initialValue )
    {
        ++liveCount;
    }

    PhysicsFixedListRelocationValue( const PhysicsFixedListRelocationValue& ) = delete;
    PhysicsFixedListRelocationValue& operator=( const PhysicsFixedListRelocationValue& ) = delete;
    PhysicsFixedListRelocationValue( PhysicsFixedListRelocationValue&& other ) noexcept : value( other.value )
    {
        ++moveConstructions;
        other.value = -1;
        ++liveCount;
    }
    PhysicsFixedListRelocationValue& operator=( PhysicsFixedListRelocationValue&& ) = delete;

    ~PhysicsFixedListRelocationValue()
    {
        --liveCount;
    }

    int value = 0;
    static inline int liveCount = 0;
    static inline int moveConstructions = 0;
};
} // namespace


TEST_CASE( "PhysicsFixedList: non-trivial relocation preserves values and retires the old prefix" )
{
    RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
    using List = PhysicsFixedList<PhysicsFixedListRelocationValue, 8>;
    PhysicsFixedListRelocationValue::liveCount = 0;
    PhysicsFixedListRelocationValue::moveConstructions = 0;

    {
        List source( "unit.physics-fixed-list.relocation-source", ExplicitTestCapacity );
        source.Reserve( 3u );
        source.push_back( PhysicsFixedListRelocationValue( 17 ) );
        source.push_back( PhysicsFixedListRelocationValue( 23 ) );
        REQUIRE( PhysicsFixedListRelocationValue::liveCount == 2 );

        PhysicsFixedListRelocationValue::moveConstructions = 0;
        source.Reserve( 5u );

        CHECK( PhysicsFixedListRelocationValue::moveConstructions == 2 );
        CHECK( source.capacity() == 5u );
        CHECK( source.size() == 2u );
        CHECK( source[0].value == 17 );
        CHECK( source[1].value == 23 );
        CHECK( PhysicsFixedListRelocationValue::liveCount == 2 );
        source.clear();
        const std::span<const RuntimeReserveCapacityView> capacityRows = RuntimeReserveAllocator::CapacityRows();
        const auto sourceRow = std::find_if( capacityRows.begin(), capacityRows.end(),
                                             []( const RuntimeReserveCapacityView& candidate )
                                              {
                                                  return candidate.ownerName &&
                                                         std::strcmp( candidate.ownerName,
                                                                      "unit.physics-fixed-list.relocation-source" ) == 0;
                                              } );
        REQUIRE( sourceRow != capacityRows.end() );
        CHECK( sourceRow->currentCapacity == 5 );
        CHECK( sourceRow->liveCount == 0 );
        CHECK( sourceRow->sessionHighWater == 2 );
    }

    CHECK( PhysicsFixedListRelocationValue::liveCount == 0 );
}


TEST_CASE( "PhysicsFixedList: replay reserve requires an approved outer owner and growth scope" )
{
    constexpr const char* ownerName = "unit.physics-fixed-list.replay-owner";
    const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
        MakeReplayOwnerDesc( ownerName, 0, 1024 ) );
    RuntimeReserveGrowthRequest request = MakeGrowthRequest( ownerName, 0, 128 );
    request.elementSizeBytes = 1;
    RuntimeReserveGrowthResult growth = RuntimeReserveAllocator::RequestGrowth( owner, request );
    REQUIRE( growth.granted );

    PhysicsFixedList<int, 8> values( "unit.physics-fixed-list.replay-target", ExplicitTestCapacity );
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, growth );
        values.Reserve( 4u );
    }

    CHECK( values.capacity() == 4u );
}


TEST_CASE( "PhysicsFixedList: object size no longer scales with compile-time capacity" )
{
    CHECK( sizeof( PhysicsFixedList<uint8_t, 8192> ) == sizeof( PhysicsFixedList<uint8_t, 8> ) );
    CHECK( sizeof( PhysicsFixedList<uint8_t, 8192> ) <= 64u );
}
