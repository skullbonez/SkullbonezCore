/*
File: SkullbonezSource/Core/LockOrderValidator.cpp
Purpose:
  Implements debug lock-order cycle detection.

Mental model:
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
  - The validator singleton is a frozen diagnostics exception: function-local
    storage, no config reads, and no dependency on other singleton teardown.

Related:
  - SkullbonezSource/Core/LockOrderValidator.h
*/

#include "LockOrderValidator.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SkullbonezCore
{
namespace Threading
{
namespace
{

std::atomic<uint32_t> g_nextLockId{ 1 };
std::atomic<uint32_t> g_nextThreadId{ 1 };

#ifdef _DEBUG
// Lifetime: each debug thread keeps its own acquisition stack while the global
// graph records order edges across all threads.
thread_local std::vector<uint32_t> g_heldLocks;
thread_local uint32_t g_threadId = g_nextThreadId.fetch_add( 1, std::memory_order_relaxed );

bool HasCycleFrom( uint32_t node,
                   const std::unordered_map<uint32_t, std::unordered_set<uint32_t>>& graph,
                   std::unordered_set<uint32_t>& visiting,
                   std::unordered_set<uint32_t>& visited )
{
    if ( visiting.find( node ) != visiting.end() )
    {
        return true;
    }
    if ( visited.find( node ) != visited.end() )
    {
        return false;
    }

    visiting.insert( node );
    const auto edges = graph.find( node );
    if ( edges != graph.end() )
    {
        for ( uint32_t next : edges->second )
        {
            if ( HasCycleFrom( next, graph, visiting, visited ) )
            {
                return true;
            }
        }
    }
    visiting.erase( node );
    visited.insert( node );
    return false;
}
#endif

uint32_t CurrentThreadId()
{
#ifdef _DEBUG
    return g_threadId;
#else
    return 0;
#endif
}

} // namespace

class LockOrderValidatorState
{
  public:
    std::mutex mutex;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> edges;
    std::unordered_map<uint32_t, const char*> names;
};

LockOrderValidatorState& State()
{
    static LockOrderValidatorState s_state;
    return s_state;
}


LockOrderValidator& LockOrderValidator::Instance()
{
    // Lifetime: the diagnostics object is a function-local static so callers do
    // not depend on external startup or shutdown ordering. It owns no resources
    // whose destructor must coordinate with renderer, config, or worker state.
    static LockOrderValidator s_validator;
    return s_validator;
}


void LockOrderValidator::RegisterLock( uint32_t lockId, const char* name )
{
#ifdef _DEBUG
    std::lock_guard<std::mutex> lock( State().mutex );
    State().names[lockId] = name ? name : "<unnamed>";
#else
    static_cast<void>( lockId );
    static_cast<void>( name );
#endif
}


void LockOrderValidator::RecordAcquisition( uint32_t lockId, uint32_t threadId )
{
#ifdef _DEBUG
    {
        std::lock_guard<std::mutex> lock( State().mutex );
        for ( uint32_t heldLock : g_heldLocks )
        {
            if ( heldLock != lockId )
            {
                State().edges[heldLock].insert( lockId );
            }
        }

        if ( DetectCycleUnlocked() )
        {
            fprintf( stderr,
                     "[workers] Lock-order cycle detected while thread %u acquired lock %u.\n",
                     threadId,
                     lockId );
            assert( false && "Lock-order cycle detected." );
        }
    }

    g_heldLocks.push_back( lockId );
#else
    static_cast<void>( lockId );
    static_cast<void>( threadId );
#endif
}


void LockOrderValidator::RecordRelease( uint32_t lockId, uint32_t threadId )
{
#ifdef _DEBUG
    static_cast<void>( threadId );
    for ( auto it = g_heldLocks.rbegin(); it != g_heldLocks.rend(); ++it )
    {
        if ( *it == lockId )
        {
            g_heldLocks.erase( std::next( it ).base() );
            return;
        }
    }
#else
    static_cast<void>( lockId );
    static_cast<void>( threadId );
#endif
}


bool LockOrderValidator::DetectCycleUnlocked() const
{
#ifdef _DEBUG
    std::unordered_set<uint32_t> visiting;
    std::unordered_set<uint32_t> visited;
    for ( const auto& entry : State().edges )
    {
        if ( HasCycleFrom( entry.first, State().edges, visiting, visited ) )
        {
            return true;
        }
    }
#endif
    return false;
}


TrackedMutex::TrackedMutex( const char* name )
    : m_name( name ), m_id( g_nextLockId.fetch_add( 1, std::memory_order_relaxed ) )
{
    LockOrderValidator::Instance().RegisterLock( m_id, m_name );
}


void TrackedMutex::lock()
{
    LockOrderValidator::Instance().RecordAcquisition( m_id, CurrentThreadId() );
    m_inner.lock();
}


bool TrackedMutex::try_lock()
{
    if ( !m_inner.try_lock() )
    {
        return false;
    }
    LockOrderValidator::Instance().RecordAcquisition( m_id, CurrentThreadId() );
    return true;
}


void TrackedMutex::unlock()
{
    LockOrderValidator::Instance().RecordRelease( m_id, CurrentThreadId() );
    m_inner.unlock();
}

} // namespace Threading
} // namespace SkullbonezCore
