/*
File: SkullbonezSource/Core/LockOrderValidator.cpp
Purpose:
  Implements debug lock-order cycle detection.

Summary:
  Every successful lock acquisition records edges from locks currently held by
  the thread to the new lock. A DFS detects ABBA-style cycles immediately in
  Debug builds.

Glossary:
  ABBA cycle: Deadlock pattern where one path locks A then B while another
  locks B then A.
  DFS (Depth-First Search): Graph walk used here to detect whether lock-order
  edges contain a cycle.
  Debug build: Configuration where validation asserts are active.

Invariants:
  - Debug tracking records observed lock order only; Profile and Release builds
    must not pay graph-validation costs.
  - A thread's held-lock stack must be updated after cycle detection so failed
    acquisitions report the order that introduced the problem.
  - Init owns the validator longer than WorkerPool, and TrackedMutex keeps only
    a Debug borrow into that explicit startup lifetime.

Related:
  - SkullbonezSource/Core/LockOrderValidator.h
*/

#include "LockOrderValidator.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace SkullbonezCore
{
namespace Threading
{
namespace
{

#ifdef _DEBUG
constexpr std::size_t HELD_LOCK_CAPACITY = 64;
// Lifetime: each debug thread keeps its own fixed acquisition stack while the
// startup-owned validator records order edges across all threads.
thread_local std::array<uint32_t, HELD_LOCK_CAPACITY> g_heldLocks = {};
thread_local std::size_t g_heldLockCount = 0;
#endif

} // namespace


uint32_t LockOrderValidator::RegisterLock( const char* name )
{
#ifdef _DEBUG
    std::lock_guard<std::mutex> lock( m_mutex );
    assert( m_nextLockId <= MAX_LOCK_COUNT && "Lock-order validator capacity exhausted." );
    if ( m_nextLockId > MAX_LOCK_COUNT )
    {
        return 0;
    }
    const uint32_t lockId = m_nextLockId++;
    m_names[lockId - 1] = name ? name : "<unnamed>";
    return lockId;
#else
    static_cast<void>( name );
    return 0;
#endif
}


void LockOrderValidator::RecordAcquisition( uint32_t lockId )
{
#ifdef _DEBUG
    if ( lockId == 0 || lockId > MAX_LOCK_COUNT )
    {
        assert( false && "Invalid lock-order validator id." );
        return;
    }
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        for ( std::size_t heldIndex = 0; heldIndex < g_heldLockCount; ++heldIndex )
        {
            const uint32_t heldLock = g_heldLocks[heldIndex];
            if ( heldLock != lockId )
            {
                m_edges[heldLock - 1].set( lockId - 1 );
            }
        }

        if ( DetectCycleUnlocked() )
        {
            fprintf( stderr,
                     "[workers] Lock-order cycle detected while acquiring lock %u (%s).\n",
                     lockId,
                     m_names[lockId - 1] ? m_names[lockId - 1] : "<unnamed>" );
            assert( false && "Lock-order cycle detected." );
        }
    }

    assert( g_heldLockCount < g_heldLocks.size() && "Per-thread held-lock stack exhausted." );
    if ( g_heldLockCount < g_heldLocks.size() )
    {
        g_heldLocks[g_heldLockCount++] = lockId;
    }
#else
    static_cast<void>( lockId );
#endif
}


void LockOrderValidator::RecordRelease( uint32_t lockId )
{
#ifdef _DEBUG
    for ( std::size_t heldIndex = g_heldLockCount; heldIndex > 0; --heldIndex )
    {
        if ( g_heldLocks[heldIndex - 1] == lockId )
        {
            for ( std::size_t moveIndex = heldIndex; moveIndex < g_heldLockCount; ++moveIndex )
            {
                g_heldLocks[moveIndex - 1] = g_heldLocks[moveIndex];
            }
            --g_heldLockCount;
            return;
        }
    }
#else
    static_cast<void>( lockId );
#endif
}


#ifdef _DEBUG
bool LockOrderValidator::HasCycleFrom( uint32_t node,
                                       std::bitset<MAX_LOCK_COUNT>& visiting,
                                       std::bitset<MAX_LOCK_COUNT>& visited ) const
{
    if ( visiting.test( node ) )
    {
        return true;
    }
    if ( visited.test( node ) )
    {
        return false;
    }

    visiting.set( node );
    for ( uint32_t next = 0; next < MAX_LOCK_COUNT; ++next )
    {
        if ( m_edges[node].test( next ) && HasCycleFrom( next, visiting, visited ) )
        {
            return true;
        }
    }
    visiting.reset( node );
    visited.set( node );
    return false;
}


bool LockOrderValidator::DetectCycleUnlocked() const
{
    std::bitset<MAX_LOCK_COUNT> visiting;
    std::bitset<MAX_LOCK_COUNT> visited;
    for ( uint32_t node = 0; node + 1 < m_nextLockId; ++node )
    {
        if ( HasCycleFrom( node, visiting, visited ) )
        {
            return true;
        }
    }
    return false;
}
#endif


#ifdef _DEBUG
TrackedMutex::TrackedMutex( const char* name, LockOrderValidator& validator )
    : m_id( validator.RegisterLock( name ) ), m_validator( &validator )
#else
TrackedMutex::TrackedMutex( const char* name, LockOrderValidator& validator )
#endif
{
#ifndef _DEBUG
    static_cast<void>( name );
    static_cast<void>( validator );
#endif
}


void TrackedMutex::lock()
{
#ifdef _DEBUG
    m_validator->RecordAcquisition( m_id );
#endif
    m_inner.lock();
}


bool TrackedMutex::try_lock()
{
    if ( !m_inner.try_lock() )
    {
        return false;
    }
#ifdef _DEBUG
    m_validator->RecordAcquisition( m_id );
#endif
    return true;
}


void TrackedMutex::unlock()
{
#ifdef _DEBUG
    m_validator->RecordRelease( m_id );
#endif
    m_inner.unlock();
}

} // namespace Threading
} // namespace SkullbonezCore
