/*
File: SkullbonezSource/Core/WorkerPool.cpp
Purpose:
  Implements the fixed worker pool and deterministic fork-join helpers.

Summary:
  The main thread queues bounded work chunks, wakes persistent workers, then
  waits on a fence. Worker-disabled mode runs the same work inline.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Fork-join: Pattern where the main thread splits work, workers run chunks, and
  the main thread waits before merging results.
  Fence: Synchronization primitive used to wait for all queued chunks.
  Lane F: Fatal invariant path used when bounded worker contracts cannot be
  preserved.

Invariants:
  - Worker-disabled mode runs work inline through the same public helpers so
    validation can compare threaded and non-threaded behavior.
  - Worker callbacks obey the engine-wide no-exceptions policy; a returned
    callback completed normally, while invariant failures terminate in Lane F.

Related:
  - SkullbonezSource/Core/WorkerPool.h
  - SkullbonezSource/Core/AmortizedTask.h
*/

#include "WorkerPool.h"
#include "FatalError.h"
#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <utility>

namespace SkullbonezCore
{
namespace Threading
{
namespace
{
thread_local bool g_isWorkerThread = false;
thread_local int g_workerThreadIndex = -1;
} // namespace

WorkerPool::WorkerPool()
    : m_taskHead( 0 ), m_taskCount( 0 ), m_taskHighWater( 0 ), m_parallelTaskHead( 0 ), m_parallelTaskCount( 0 ),
      m_parallelTaskHighWater( 0 ), m_profiler( nullptr ), m_stopping( false ), m_minParallelItems( 32 )
{
}

void WorkerPool::BindProfiler( Core::Profiler* profiler ) noexcept
{
    m_profiler = profiler;
}


WorkerPool::~WorkerPool()
{
    Shutdown();
}


int WorkerPool::MaxThreadCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return (std::max)( 1, static_cast<int>( hardwareThreads ) );
}


int WorkerPool::ResolveThreadCount( int requestedThreadCount )
{
    const int maxThreadCount = MaxThreadCount();
    if ( requestedThreadCount == 0 )
    {
        return 0;
    }

    if ( requestedThreadCount < 0 )
    {
        if ( maxThreadCount <= 1 )
        {
            return 0;
        }
        return maxThreadCount - 1;
    }

    return (std::min)( requestedThreadCount, maxThreadCount );
}


bool WorkerPool::IsCurrentThreadWorker()
{
    return g_isWorkerThread;
}


int WorkerPool::CurrentWorkerIndex()
{
    return g_workerThreadIndex;
}


void WorkerPool::Initialise( int requestedThreadCount )
{
    Shutdown();

    const int threadCount = ResolveThreadCount( requestedThreadCount );
    if ( threadCount <= 0 )
    {
        fprintf( stdout, "[workers] Worker pool disabled.\n" );
        return;
    }

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_stopping = false;
    }

    m_threads.reserve( static_cast<size_t>( threadCount ) );
    for ( int i = 0; i < threadCount; ++i )
    {
        m_threads.emplace_back( [this, i]() { WorkerLoop( i ); } );
    }

    fprintf( stdout, "[workers] Worker pool initialized with %d thread(s).\n", threadCount );
}


void WorkerPool::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_stopping = true;
    }
    m_workAvailable.notify_all();

    for ( std::thread& thread : m_threads )
    {
        if ( thread.joinable() )
        {
            thread.join();
        }
    }

    m_threads.clear();
    {
        std::lock_guard<std::mutex> lock( m_mutex );
        m_taskHead = 0;
        m_taskCount = 0;
        m_taskHighWater = 0;
        m_parallelTaskHead = 0;
        m_parallelTaskCount = 0;
        m_parallelTaskHighWater = 0;
        m_stopping = false;
    }
}


void WorkerPool::SubmitTaskRecord( void* taskState, TaskDispatcher dispatch )
{
    if ( !dispatch )
    {
        return;
    }

    if ( GetThreadCount() == 0 )
    {
        dispatch( taskState );
        return;
    }

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_stopping )
        {
            SB_FATAL( "WorkerPool",
                      "SubmitNoAlloc called while shutting down: owner=Core/WorkerPool phase=runtime_dispatch." );
        }
        if ( m_taskCount >= WORKER_PARALLEL_TASK_CAPACITY )
        {
            SB_FATAL( "WorkerPool",
                      "Fixed task queue exhausted: owner=Core/WorkerPool phase=runtime_dispatch count=%d capacity=%d "
                      "high_water=%d.",
                      m_taskCount,
                      WORKER_PARALLEL_TASK_CAPACITY,
                      m_taskHighWater );
        }
        const int tail = ( m_taskHead + m_taskCount ) % WORKER_PARALLEL_TASK_CAPACITY;
        m_tasks[tail] = { taskState, dispatch };
        ++m_taskCount;
        m_taskHighWater = (std::max)( m_taskHighWater, m_taskCount );
    }
    m_workAvailable.notify_one();
}


int WorkerPool::BuildChunkRangesNoAlloc( int begin,
                                         int end,
                                         int minParallelItems,
                                         WorkerChunkRange* outChunks,
                                         int outCapacity ) const
{
    return BuildChunks( begin, end, minParallelItems, outChunks, outCapacity );
}


int WorkerPool::BuildChunks( int begin,
                             int end,
                             int minParallelItems,
                             WorkerChunkRange* outChunks,
                             int outCapacity ) const
{
    const int itemCount = end - begin;
    if ( itemCount <= 0 || !outChunks || outCapacity <= 0 )
    {
        return 0;
    }

    if ( ShouldRunInline( itemCount, minParallelItems ) )
    {
        outChunks[0] = { 0, begin, end };
        return 1;
    }

    const int workerCount = (std::max)( 1, GetThreadCount() );
    const int chunkCount = (std::max)( 1, (std::min)( workerCount, itemCount ) );
    if ( chunkCount > outCapacity )
    {
        SB_FATAL( "WorkerPool",
                  "Parallel chunk capacity exceeded: chunks=%d capacity=%d items=%d workers=%d.",
                  chunkCount,
                  outCapacity,
                  itemCount,
                  workerCount );
    }

    const int baseChunkSize = itemCount / chunkCount;
    const int remainder = itemCount % chunkCount;
    int cursor = begin;
    for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
    {
        const int chunkSize = baseChunkSize + ( chunkIndex < remainder ? 1 : 0 );
        const int chunkEnd = cursor + chunkSize;
        outChunks[chunkIndex] = { chunkIndex, cursor, chunkEnd };
        cursor = chunkEnd;
    }

    return chunkCount;
}


void WorkerPool::SubmitParallelChunk( void* dispatchState,
                                      ParallelTaskDispatcher dispatch,
                                      const WorkerChunkRange& chunk )
{
    if ( GetThreadCount() == 0 )
    {
        if ( dispatch )
        {
            dispatch( dispatchState, chunk );
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_stopping )
        {
            SB_FATAL( "WorkerPool", "SubmitParallelChunk called while shutting down." );
        }
        if ( m_parallelTaskCount >= WORKER_PARALLEL_TASK_CAPACITY )
        {
            SB_FATAL( "WorkerPool",
                      "Fixed parallel task queue exhausted: owner=Core/WorkerPool phase=runtime_parallel_dispatch "
                      "count=%d capacity=%d high_water=%d.",
                      m_parallelTaskCount,
                      WORKER_PARALLEL_TASK_CAPACITY,
                      m_parallelTaskHighWater );
        }

        const int tail = ( m_parallelTaskHead + m_parallelTaskCount ) % WORKER_PARALLEL_TASK_CAPACITY;
        m_parallelTasks[tail] = { dispatchState, dispatch, chunk };
        ++m_parallelTaskCount;
        m_parallelTaskHighWater = (std::max)( m_parallelTaskHighWater, m_parallelTaskCount );
    }
    m_workAvailable.notify_one();
}


int WorkerPool::GetThreadCount() const
{
    return static_cast<int>( m_threads.size() );
}


int WorkerPool::GetMinParallelItems() const
{
    return m_minParallelItems;
}


bool WorkerPool::IsInitialised() const
{
    return GetThreadCount() > 0;
}


bool WorkerPool::ShouldRunInline( int itemCount, int minParallelItems ) const
{
    const int threshold = minParallelItems > 0 ? minParallelItems : m_minParallelItems;
    return itemCount < threshold || GetThreadCount() == 0 || IsCurrentThreadWorker();
}


void WorkerPool::WorkerLoop( int workerIndex )
{
    g_isWorkerThread = true;
    g_workerThreadIndex = workerIndex;

    while ( true )
    {
        TaskRecord task = {};
        ParallelTaskRecord parallelTask = {};
        bool hasParallelTask = false;
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            m_workAvailable.wait( lock, [&]() { return m_stopping || m_taskCount > 0 || m_parallelTaskCount > 0; } );

            if ( m_stopping && m_taskCount == 0 && m_parallelTaskCount == 0 )
            {
                break;
            }

            if ( m_parallelTaskCount > 0 )
            {
                parallelTask = m_parallelTasks[m_parallelTaskHead];
                m_parallelTaskHead = ( m_parallelTaskHead + 1 ) % WORKER_PARALLEL_TASK_CAPACITY;
                --m_parallelTaskCount;
                hasParallelTask = true;
            }
            else
            {
                task = m_tasks[m_taskHead];
                m_taskHead = ( m_taskHead + 1 ) % WORKER_PARALLEL_TASK_CAPACITY;
                --m_taskCount;
            }
        }

        if ( hasParallelTask )
        {
            if ( parallelTask.dispatch )
            {
                parallelTask.dispatch( parallelTask.dispatchState, parallelTask.chunk );
            }
        }
        else
        {
            if ( task.dispatch )
            {
                task.dispatch( task.state );
            }
        }
    }

    g_isWorkerThread = false;
    g_workerThreadIndex = -1;
}


bool RunWorkerSystemSelfTest( WorkerPool& pool, FILE* out )
{
    struct FixedTaskProbe
    {
        explicit FixedTaskProbe( int taskCount ) : fence( taskCount )
        {
        }

        std::atomic<int> completed{ 0 };
        Fence fence;

        void ExecuteWorkerTask()
        {
            completed.fetch_add( 1, std::memory_order_relaxed );
            fence.Signal();
        }
    };
    constexpr int fixedTaskCount = 32;
    constexpr int fixedTaskRounds = 10;
    for ( int round = 0; round < fixedTaskRounds; ++round )
    {
        FixedTaskProbe fixedTaskProbe( fixedTaskCount );
        for ( int taskIndex = 0; taskIndex < fixedTaskCount; ++taskIndex )
        {
            pool.SubmitNoAlloc( fixedTaskProbe );
        }
        fixedTaskProbe.fence.Wait();
        if ( fixedTaskProbe.completed.load( std::memory_order_relaxed ) != fixedTaskCount )
        {
            fprintf( out, "[worker-self-test] Fixed task ring did not execute every submitted task.\n" );
            return false;
        }
    }

    std::vector<int> squares( 257, 0 );
    pool.ParallelForNoAlloc(
        0,
        static_cast<int>( squares.size() ),
        [&]( int index ) { squares[static_cast<size_t>( index )] = index * index; },
        1,
        "Frame/Workers/SelfTest/ParallelFor",
        HashStr( "Frame/Workers/SelfTest/ParallelFor" ) );

    for ( int index = 0; index < static_cast<int>( squares.size() ); ++index )
    {
        if ( squares[static_cast<size_t>( index )] != index * index )
        {
            fprintf( out, "[worker-self-test] ParallelFor mismatch at %d.\n", index );
            return false;
        }
    }

    std::vector<std::vector<int>> chunkOutputs;
    std::vector<int> merged;
    pool.ParallelCollectOrdered<std::vector<int>>(
        0,
        257,
        chunkOutputs,
        []( int, int begin, int end, std::vector<int>& local )
        {
            local.reserve( static_cast<size_t>( end - begin ) );
            for ( int index = begin; index < end; ++index )
            {
                local.push_back( index );
            }
        },
        [&]( int, const std::vector<int>& local ) { merged.insert( merged.end(), local.begin(), local.end() ); },
        1 );

    if ( merged.size() != squares.size() )
    {
        fprintf( out, "[worker-self-test] Ordered collection size mismatch.\n" );
        return false;
    }

    for ( int index = 0; index < static_cast<int>( merged.size() ); ++index )
    {
        if ( merged[static_cast<size_t>( index )] != index )
        {
            fprintf( out, "[worker-self-test] Ordered collection mismatch at %d.\n", index );
            return false;
        }
    }

    fprintf( out, "[worker-self-test] PASS: ParallelFor and ordered chunk collection are deterministic.\n" );
    return true;
}

} // namespace Threading
} // namespace SkullbonezCore
