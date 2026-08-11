/*
File: SkullbonezSource/Core/SbResult.cpp
Purpose:
  Implements compact Lane R leases and the fixed diagnostic store.

Summary:
  Failure publication claims one fixed slot, copies complete diagnostic bytes,
  and publishes a generation-bearing lease. All failed-result lifetime changes
  serialize through the store; the success sentinel is store-free.

Glossary:
  Live entry: Slot whose lease count is nonzero and whose generation matches.
  Stale identity: Previously valid token whose slot is free or has been reused.
  Lease churn: Retain/release work; moves deliberately avoid it.
  Thread token: Allocation-free process-local identity used only to detect lock
    re-entry by the thread that already owns this store.

Invariants:
  - No heap allocation, logging, callback, or result-producing operation occurs

    while the store lock is held.
  - Message formatting is bounded to 511 bytes plus a null terminator.
  - Retain-before-release makes copy assignment safe for shared-entry aliases.
  - Lane F is emitted only after the store lock has been released.

Related:
  - SkullbonezSource/Core/SbResult.h
  - SkullbonezSource/Core/SbDiagnosticStore.h
  - SkullbonezSource/Core/FatalError.h
*/
#include "SbDiagnosticStore.h"

#include "FatalError.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>


namespace SkullbonezCore
{
namespace Core
{
namespace
{
constexpr std::uint64_t SLOT_MASK = 0xffu;
constexpr std::uint64_t MAX_GENERATION = ( std::uint64_t { 1 } << 56u ) - 1u;
std::atomic<std::uint32_t> g_nextDiagnosticThreadToken = 1u;


std::uint32_t CurrentDiagnosticThreadToken() noexcept
{

    // Lifetime: assigning a token once per thread avoids OS or heap-backed
    // identity lookup on every diagnostic lease operation.
    thread_local const std::uint32_t token = g_nextDiagnosticThreadToken.fetch_add( 1u, std::memory_order_relaxed );

    if ( token == 0u )
    {
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic lock thread-token space exhausted" );
    }

    return token;
}


std::size_t BoundedLength( const char* text, std::size_t capacity ) noexcept
{
    if ( !text )
    {
        return 0;
    }

    std::size_t length = 0;

    while ( length < capacity && text[length] != '\0' )
    {
        ++length;
    }

    return length;
}
} // namespace


SbResult::~SbResult() noexcept
{
    Release();
}


SbResult::SbResult( const SbResult& source ) noexcept : m_store( source.m_store ), m_token( source.m_token )
{
    if ( m_store )
    {
        m_store->Retain( m_token );
    }
}


SbResult& SbResult::operator=( const SbResult& source ) noexcept
{
    if ( this == &source )
    {
        return *this;
    }

    // Invariant: retain the incoming lease before releasing the destination so
    // two result objects sharing one entry cannot transiently reclaim it.
    if ( source.m_store )
    {
        source.m_store->Retain( source.m_token );
    }

    Release();
    m_store = source.m_store;
    m_token = source.m_token;
    return *this;
}


SbResult::SbResult( SbResult&& source ) noexcept : m_store( source.m_store ), m_token( source.m_token )
{
    source.m_store = nullptr;
    source.m_token = 0;
}


SbResult& SbResult::operator=( SbResult&& source ) noexcept
{
    if ( this == &source )
    {
        return *this;
    }

    SbDiagnosticStore* incomingStore = source.m_store;
    const std::uint64_t incomingToken = source.m_token;
    source.m_store = nullptr;
    source.m_token = 0;
    Release();
    m_store = incomingStore;
    m_token = incomingToken;
    return *this;
}


SbResult SbResult::Success() noexcept
{
    return {};
}


bool SbResult::Ok() const noexcept
{
    return m_store == nullptr && m_token == 0;
}


const char* SbResult::ErrorOwner() const noexcept
{
    return m_store ? m_store->BorrowOwner( m_token ) : "";
}


const char* SbResult::ErrorMessage() const noexcept
{
    return m_store ? m_store->BorrowMessage( m_token ) : "";
}


SbDiagnosticIdentity SbResult::DiagnosticIdentity() const noexcept
{
    return { m_store, m_token };
}


SbResult::SbResult( SbDiagnosticStore& store, std::uint64_t token ) noexcept : m_store( &store ), m_token( token )
{
}


void SbResult::Release() noexcept
{
    if ( !m_store )
    {
        return;
    }

    SbDiagnosticStore* store = m_store;
    const std::uint64_t token = m_token;
    m_store = nullptr;
    m_token = 0;
    store->Release( token );
}


SbResult SbDiagnosticStore::Failure( const char* owner, const char* format, ... ) noexcept
{
    va_list args;
    va_start( args, format );
    SbResult result = FailureV( owner, format, args );
    va_end( args );
    return result;
}


SbDiagnosticStore::~SbDiagnosticStore() noexcept
{
    const std::uint32_t activeEntries = ActiveEntryCount();

    if ( activeEntries != 0 )
    {

        // Hazard: every failed SbResult contains a raw store pointer. Failing
        // here catches the lifetime inversion while the store is still valid,
        // before a later result destructor could dereference dead storage.
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic store destroyed with %u active entries", activeEntries );
    }
}


SbResult SbDiagnosticStore::FailureV( const char* owner, const char* format, va_list args ) noexcept
{
    const char* safeOwner = owner ? owner : "";
    const std::size_t ownerLength = BoundedLength( safeOwner, OWNER_CAPACITY );

    if ( ownerLength >= OWNER_CAPACITY )
    {
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic owner exceeds %zu-byte bound", OWNER_CAPACITY - 1u );
    }

    Lock();
    std::size_t slotIndex = CAPACITY;

    for ( std::size_t index = 0; index < CAPACITY; ++index )
    {
        if ( m_entries[index].leaseCount == 0 )
        {
            slotIndex = index;
            break;
        }
    }

    if ( slotIndex == CAPACITY )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "all %zu diagnostic slots are leased", CAPACITY );
    }

    Entry& entry = m_entries[slotIndex];

    if ( entry.generation == MAX_GENERATION )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic generation exhausted for slot %zu", slotIndex );
    }

    ++entry.generation;
    std::memset( entry.owner, 0, sizeof( entry.owner ) );
    std::memcpy( entry.owner, safeOwner, ownerLength );
    std::memset( entry.message, 0, sizeof( entry.message ) );
    std::vsnprintf( entry.message, sizeof( entry.message ), format ? format : "recoverable operation failed", args );
    entry.message[MESSAGE_CAPACITY - 1u] = '\0';
    entry.leaseCount = 1;
    ++m_activeEntries;

    if ( m_activeEntries > m_sessionHighWater )
    {
        m_sessionHighWater = m_activeEntries;
    }

    const std::uint64_t token = ( entry.generation << 8u ) | static_cast<std::uint64_t>( slotIndex );
    Unlock();
    return SbResult( *this, token );
}


SbDiagnosticCopyStatus SbDiagnosticStore::CopyDiagnostic( SbDiagnosticIdentity identity, char ( &owner )[OWNER_CAPACITY],
                                                          char ( &message )[MESSAGE_CAPACITY] ) const noexcept
{
    owner[0] = '\0';
    message[0] = '\0';

    if ( identity.token == 0 && identity.store == nullptr )
    {
        return SbDiagnosticCopyStatus::SuccessIdentity;
    }

    if ( identity.store != this )
    {
        return SbDiagnosticCopyStatus::ForeignStore;
    }

    Lock();
    std::size_t slotIndex = 0;

    if ( !ResolveLiveEntry( identity.token, slotIndex ) )
    {
        Unlock();
        return SbDiagnosticCopyStatus::Stale;
    }

    std::memcpy( owner, m_entries[slotIndex].owner, OWNER_CAPACITY );
    std::memcpy( message, m_entries[slotIndex].message, MESSAGE_CAPACITY );
    Unlock();
    return SbDiagnosticCopyStatus::Copied;
}


std::uint32_t SbDiagnosticStore::ActiveEntryCount() const noexcept
{
    Lock();
    const std::uint32_t active = m_activeEntries;
    Unlock();
    return active;
}


std::uint32_t SbDiagnosticStore::SessionHighWater() const noexcept
{
    Lock();
    const std::uint32_t highWater = m_sessionHighWater;
    Unlock();
    return highWater;
}


void SbDiagnosticStore::Retain( std::uint64_t token ) noexcept
{
    Lock();
    std::size_t slotIndex = 0;

    if ( !ResolveLiveEntry( token, slotIndex ) )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "retain used a stale diagnostic token" );
    }

    Entry& entry = m_entries[slotIndex];

    if ( entry.leaseCount == std::numeric_limits<std::uint32_t>::max() )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic lease count overflowed" );
    }

    ++entry.leaseCount;
    Unlock();
}


void SbDiagnosticStore::Release( std::uint64_t token ) noexcept
{
    Lock();
    std::size_t slotIndex = 0;

    if ( !ResolveLiveEntry( token, slotIndex ) )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "release used a stale or already released diagnostic token" );
    }

    Entry& entry = m_entries[slotIndex];
    --entry.leaseCount;

    if ( entry.leaseCount == 0 )
    {
        --m_activeEntries;
    }

    Unlock();
}


const char* SbDiagnosticStore::BorrowOwner( std::uint64_t token ) const noexcept
{
    Lock();
    std::size_t slotIndex = 0;

    if ( !ResolveLiveEntry( token, slotIndex ) )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "live result resolved a stale diagnostic owner" );
    }

    const char* owner = m_entries[slotIndex].owner;
    Unlock();
    return owner;
}


const char* SbDiagnosticStore::BorrowMessage( std::uint64_t token ) const noexcept
{
    Lock();
    std::size_t slotIndex = 0;

    if ( !ResolveLiveEntry( token, slotIndex ) )
    {
        Unlock();
        SB_FATAL( "Core/SbDiagnosticStore", "live result resolved a stale diagnostic message" );
    }

    const char* message = m_entries[slotIndex].message;
    Unlock();
    return message;
}


void SbDiagnosticStore::Lock() const noexcept
{
    const std::uint32_t currentThread = CurrentDiagnosticThreadToken();

    if ( m_lockOwnerThread.load( std::memory_order_relaxed ) == currentThread )
    {

        // Hazard: a diagnostic callback that republishes while this store is
        // locked would otherwise spin forever. Clear the outer ownership only
        // because Lane F terminates immediately after this point.
        m_lockOwnerThread.store( 0u, std::memory_order_relaxed );
        m_lock.clear( std::memory_order_release );
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic store lock re-entered by its owning thread" );
    }

    while ( m_lock.test_and_set( std::memory_order_acquire ) )
    {
    }

    m_lockOwnerThread.store( currentThread, std::memory_order_relaxed );
}


void SbDiagnosticStore::Unlock() const noexcept
{
    const std::uint32_t currentThread = CurrentDiagnosticThreadToken();

    if ( m_lockOwnerThread.load( std::memory_order_relaxed ) != currentThread )
    {
        m_lockOwnerThread.store( 0u, std::memory_order_relaxed );
        m_lock.clear( std::memory_order_release );
        SB_FATAL( "Core/SbDiagnosticStore", "diagnostic store lock released by a non-owning thread" );
    }

    m_lockOwnerThread.store( 0u, std::memory_order_relaxed );
    m_lock.clear( std::memory_order_release );
}


bool SbDiagnosticStore::ResolveLiveEntry( std::uint64_t token, std::size_t& slotIndex ) const noexcept
{
    if ( token == 0 )
    {
        return false;
    }

    slotIndex = static_cast<std::size_t>( token & SLOT_MASK );
    const std::uint64_t generation = token >> 8u;
    const Entry& entry = m_entries[slotIndex];
    return generation != 0 && entry.generation == generation && entry.leaseCount != 0;
}
} // namespace Core
} // namespace SkullbonezCore
