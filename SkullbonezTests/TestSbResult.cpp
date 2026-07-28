/*
File: SkullbonezTests/TestSbResult.cpp
Purpose:
  Verifies compact SbResult identity, lease lifetime, and diagnostic copy-out.

Summary:
  Focused tests exercise the zero success sentinel, bounded immutable failure
  publication, copy/move ownership, generation reuse, fixed capacity, and
  concurrent publication without relying on Runtime owners.

Glossary:
  Exact identity: The store pointer and packed slot/generation token together.
  Reclaim: The transition that frees a slot when its final result lease dies.

Invariants:
  - Success operations never change store counters.
  - A copied failure keeps one entry live until the final copy is released.
  - CopyDiagnostic accepts only a live identity from the exact store.
  - Concurrent publication preserves each thread/result owner-message pair
    byte-exact while every lease remains retained.

Related:
  - SkullbonezSource/Core/SbResult.h
  - SkullbonezSource/Core/SbDiagnosticStore.h
  - Agentic/Reports/2026-07-28/sbresult-compact-success-path-sr2-implementation.md
*/
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

#include "doctest/doctest.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
#include <utility>

using SkullbonezCore::Core::SbDiagnosticCopyStatus;
using SkullbonezCore::Core::SbDiagnosticIdentity;
using SkullbonezCore::Core::SbDiagnosticStore;
using SkullbonezCore::Core::SbResult;

static_assert( std::is_nothrow_destructible_v<SbDiagnosticStore>,
               "SbDiagnosticStore lifetime failures must terminate without destructor unwinding" );


TEST_CASE( "SbResult success is a compact store-free sentinel" )
{
    SbDiagnosticStore store;
    SbResult success;
    CHECK( success.Ok() );
    CHECK( success.DiagnosticIdentity().store == nullptr );
    CHECK( success.DiagnosticIdentity().token == 0u );
    CHECK( std::strcmp( success.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( success.ErrorMessage(), "" ) == 0 );

    SbResult copied = success;
    SbResult moved = std::move( copied );
    success = moved;
    moved = std::move( success );
    CHECK( moved.Ok() );
    CHECK( store.ActiveEntryCount() == 0u );
    CHECK( store.SessionHighWater() == 0u );
    CHECK( sizeof( SbResult ) == 16u );
}


TEST_CASE( "SbDiagnosticStore formats immutable bounded diagnostics" )
{
    SbDiagnosticStore store;
    std::array<char, SbDiagnosticStore::OWNER_CAPACITY> maximumOwner = {};
    maximumOwner.fill( 'o' );
    maximumOwner.back() = '\0';
    std::array<char, SbDiagnosticStore::MESSAGE_CAPACITY + 64u> input = {};
    input.fill( 'm' );
    input.back() = '\0';

    const SbResult failure = store.Failure( maximumOwner.data(), "code=%d %s", 17, input.data() );
    CHECK_FALSE( failure.Ok() );
    CHECK( std::strcmp( failure.ErrorOwner(), maximumOwner.data() ) == 0 );
    CHECK( std::strlen( failure.ErrorOwner() ) == SbDiagnosticStore::OWNER_CAPACITY - 1u );
    CHECK( std::strncmp( failure.ErrorMessage(), "code=17 ", 8u ) == 0 );
    CHECK( std::strlen( failure.ErrorMessage() ) == SbDiagnosticStore::MESSAGE_CAPACITY - 1u );

    const SbResult defaults = store.Failure( nullptr, nullptr );
    CHECK( std::strcmp( defaults.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( defaults.ErrorMessage(), "recoverable operation failed" ) == 0 );
}


TEST_CASE( "SbResult copy and move operations retain or transfer exactly one lease" )
{
    SbDiagnosticStore store;
    SbResult first = store.Failure( "Lease", "first" );
    const SbDiagnosticIdentity identity = first.DiagnosticIdentity();
    CHECK( store.ActiveEntryCount() == 1u );

    {
        SbResult copy = first;
        SbResult assigned;
        assigned = copy;
        CHECK( copy.DiagnosticIdentity().store == identity.store );
        CHECK( copy.DiagnosticIdentity().token == identity.token );
        CHECK( assigned.DiagnosticIdentity().token == identity.token );
        CHECK( store.ActiveEntryCount() == 1u );

        copy = copy;
        copy = std::move( copy );
        CHECK( copy.DiagnosticIdentity().token == identity.token );

        SbResult moved = std::move( assigned );
        CHECK( assigned.Ok() );
        CHECK( moved.DiagnosticIdentity().token == identity.token );

        SbResult replacement = store.Failure( "Lease", "replacement" );
        CHECK( store.ActiveEntryCount() == 2u );
        replacement = std::move( moved );
        CHECK( moved.Ok() );
        CHECK( replacement.DiagnosticIdentity().token == identity.token );
        CHECK( store.ActiveEntryCount() == 1u );
    }

    CHECK( store.ActiveEntryCount() == 1u );
    first = SbResult::Success();
    CHECK( store.ActiveEntryCount() == 0u );
}


TEST_CASE( "SbDiagnosticStore copy-out validates success foreign and stale identities" )
{
    SbDiagnosticStore store;
    SbDiagnosticStore foreignStore;
    char owner[SbDiagnosticStore::OWNER_CAPACITY] = {};
    char message[SbDiagnosticStore::MESSAGE_CAPACITY] = {};

    const SbDiagnosticIdentity success;
    CHECK( store.CopyDiagnostic( success, owner, message ) == SbDiagnosticCopyStatus::SuccessIdentity );
    CHECK( owner[0] == '\0' );
    CHECK( message[0] == '\0' );

    SbDiagnosticIdentity stale;
    {
        const SbResult failure = store.Failure( "CopyOut", "immutable %d", 29 );
        stale = failure.DiagnosticIdentity();
        CHECK( store.CopyDiagnostic( stale, owner, message ) == SbDiagnosticCopyStatus::Copied );
        CHECK( std::strcmp( owner, "CopyOut" ) == 0 );
        CHECK( std::strcmp( message, "immutable 29" ) == 0 );
        CHECK( foreignStore.CopyDiagnostic( stale, owner, message ) == SbDiagnosticCopyStatus::ForeignStore );
        CHECK( owner[0] == '\0' );
        CHECK( message[0] == '\0' );
    }

    CHECK( store.CopyDiagnostic( stale, owner, message ) == SbDiagnosticCopyStatus::Stale );
    CHECK( owner[0] == '\0' );
    CHECK( message[0] == '\0' );
}


TEST_CASE( "SbDiagnosticStore reclaims slots and advances generation identity" )
{
    SbDiagnosticStore store;
    SbDiagnosticIdentity firstIdentity;
    {
        const SbResult first = store.Failure( "Generation", "first" );
        firstIdentity = first.DiagnosticIdentity();
    }

    CHECK( store.ActiveEntryCount() == 0u );
    const SbResult second = store.Failure( "Generation", "second" );
    const SbDiagnosticIdentity secondIdentity = second.DiagnosticIdentity();
    CHECK( ( firstIdentity.token & 0xffu ) == ( secondIdentity.token & 0xffu ) );
    CHECK( firstIdentity.token != secondIdentity.token );
    CHECK( store.ActiveEntryCount() == 1u );
}


TEST_CASE( "SbDiagnosticStore supports all fixed slots and reuses them after final release" )
{
    SbDiagnosticStore store;
    std::array<SbResult, SbDiagnosticStore::CAPACITY> leases;

    for ( std::size_t index = 0; index < leases.size(); ++index )
    {
        leases[index] = store.Failure( "Capacity", "slot=%zu", index );
    }

    CHECK( store.ActiveEntryCount() == SbDiagnosticStore::CAPACITY );
    CHECK( store.SessionHighWater() == SbDiagnosticStore::CAPACITY );

    for ( SbResult& lease : leases )
    {
        lease = SbResult::Success();
    }

    CHECK( store.ActiveEntryCount() == 0u );
    const SbResult reused = store.Failure( "Capacity", "reused" );
    CHECK_FALSE( reused.Ok() );
    CHECK( store.ActiveEntryCount() == 1u );
}


TEST_CASE( "SbDiagnosticStore serializes concurrent fixed-capacity publication" )
{
    constexpr std::size_t THREAD_COUNT = 8u;
    constexpr std::size_t RESULTS_PER_THREAD = 16u;
    SbDiagnosticStore store;
    std::array<std::array<SbResult, RESULTS_PER_THREAD>, THREAD_COUNT> results;
    std::array<std::thread, THREAD_COUNT> workers;
    std::atomic<std::size_t> ready = 0u;
    std::atomic<bool> start = false;
    std::atomic<bool> release = false;

    for ( std::size_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex )
    {
        workers[threadIndex] = std::thread( [&, threadIndex]()
                                            {
                                                std::array<char, 32u> owner = {};

                                                std::snprintf( owner.data(), owner.size(), "thread=%zu", threadIndex );

                                                while ( !start.load( std::memory_order_acquire ) )
                                                {
                                                }

                                                for ( std::size_t resultIndex = 0; resultIndex < RESULTS_PER_THREAD; ++resultIndex )
                                                {
                                                    results[threadIndex][resultIndex] = store.Failure( owner.data(), "result=%zu", resultIndex );
                                                }

                                                ready.fetch_add( 1u, std::memory_order_release );

                                                while ( !release.load( std::memory_order_acquire ) )
                                                {
                                                }
                                            } );
    }

    start.store( true, std::memory_order_release );

    while ( ready.load( std::memory_order_acquire ) != THREAD_COUNT )
    {
    }

    CHECK( store.ActiveEntryCount() == THREAD_COUNT * RESULTS_PER_THREAD );

    for ( std::size_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex )
    {
        std::array<char, 32u> expectedOwner = {};
        std::snprintf( expectedOwner.data(), expectedOwner.size(), "thread=%zu", threadIndex );

        for ( std::size_t resultIndex = 0; resultIndex < RESULTS_PER_THREAD; ++resultIndex )
        {
            std::array<char, 32u> expectedMessage = {};
            std::snprintf( expectedMessage.data(), expectedMessage.size(), "result=%zu", resultIndex );
            CHECK( std::strcmp( results[threadIndex][resultIndex].ErrorOwner(), expectedOwner.data() ) == 0 );
            CHECK( std::strcmp( results[threadIndex][resultIndex].ErrorMessage(), expectedMessage.data() ) == 0 );
        }
    }

    for ( std::size_t left = 0; left < THREAD_COUNT * RESULTS_PER_THREAD; ++left )
    {
        const SbDiagnosticIdentity leftIdentity = results[left / RESULTS_PER_THREAD][left % RESULTS_PER_THREAD]
                                                      .DiagnosticIdentity();

        for ( std::size_t right = left + 1u; right < THREAD_COUNT * RESULTS_PER_THREAD; ++right )
        {
            const SbDiagnosticIdentity rightIdentity = results[right / RESULTS_PER_THREAD][right % RESULTS_PER_THREAD]
                                                           .DiagnosticIdentity();

            CHECK( leftIdentity.token != rightIdentity.token );
        }
    }

    release.store( true, std::memory_order_release );

    for ( std::thread& worker : workers )
    {
        worker.join();
    }

    for ( auto& threadResults : results )
    {

        for ( SbResult& result : threadResults )
        {
            result = SbResult::Success();
        }
    }

    CHECK( store.ActiveEntryCount() == 0u );
    CHECK( store.SessionHighWater() == THREAD_COUNT * RESULTS_PER_THREAD );
}
