/*
File: SkullbonezSource/Core/WorkerPool.cpp
Purpose:
  Implements the fixed worker pool and deterministic fork-join helpers.

Mental model:
  The main thread queues bounded work chunks, wakes persistent workers, then
  waits on a fence. Worker-disabled mode runs the same work inline.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Fork-join: Pattern where the main thread splits work, workers run chunks, and
  the main thread waits before merging results.
  Fence: Synchronization primitive used to wait for all queued chunks.

Invariants:
  - Worker-disabled mode runs work inline through the same public helpers so
    validation can compare threaded and non-threaded behavior.
  - Fork-join helpers capture the first worker exception and rethrow it on the
    calling thread after every queued chunk has signaled its fence.

Related:
  - SkullbonezSource/Core/WorkerPool.h
  - SkullbonezSource/Core/AmortizedTask.h
*/

#include "WorkerPool.h"
#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
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
    : m_parallelTaskHead( 0 ), m_parallelTaskCount( 0 ), m_stopping( false ), m_minParallelItems( 32 )
{
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
        m_tasks.clear();
        m_parallelTaskHead = 0;
        m_parallelTaskCount = 0;
        m_stopping = false;
    }
}


void WorkerPool::Submit( Task task )
{
    if ( !task )
    {
        return;
    }

    if ( GetThreadCount() == 0 )
    {
        task();
        return;
    }

    {
        std::lock_guard<std::mutex> lock( m_mutex );
        if ( m_stopping )
        {
            throw std::runtime_error( "WorkerPool::Submit called while shutting down." );
        }
        m_tasks.push_back( std::move( task ) );
    }
    m_workAvailable.notify_one();
}


void WorkerPool::ParallelFor( int begin,
                              int end,
                              const IndexFunction& fn,
                              int minParallelItems,
                              const char* workerMarkerPath,
                              uint32_t workerMarkerHash )
{
    const int itemCount = end - begin;
    if ( itemCount <= 0 || !fn )
    {
        return;
    }

    ParallelForNoAlloc( begin, end, fn, minParallelItems, workerMarkerPath, workerMarkerHash );
}


void WorkerPool::ParallelForChunks( const std::vector<WorkerChunkRange>& chunks, const ChunkFunction& fn )
{
    ParallelForChunks( chunks.data(), static_cast<int>( chunks.size() ), fn );
}


void WorkerPool::ParallelForChunks( const WorkerChunkRange* chunks, int chunkCount, const ChunkFunction& fn )
{
    if ( fn )
    {
        ParallelForChunksNoAlloc( chunks, chunkCount, fn );
    }
}


std::vector<WorkerChunkRange> WorkerPool::MakeChunks( int begin, int end, int minParallelItems ) const
{
    std::vector<WorkerChunkRange> chunks;
    WorkerChunkRange fixedChunks[WORKER_PARALLEL_TASK_CAPACITY];
    const int chunkCount = BuildChunks( begin, end, minParallelItems, fixedChunks, WORKER_PARALLEL_TASK_CAPACITY );
    chunks.reserve( static_cast<size_t>( chunkCount ) );
    for ( int index = 0; index < chunkCount; ++index )
    {
        chunks.push_back( fixedChunks[index] );
    }
    return chunks;
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
        throw std::runtime_error( "WorkerPool parallel chunk capacity exceeded." );
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
            throw std::runtime_error( "WorkerPool::SubmitParallelChunk called while shutting down." );
        }
        if ( m_parallelTaskCount >= WORKER_PARALLEL_TASK_CAPACITY )
        {
            throw std::runtime_error( "WorkerPool fixed parallel task queue exhausted." );
        }

        const int tail = ( m_parallelTaskHead + m_parallelTaskCount ) % WORKER_PARALLEL_TASK_CAPACITY;
        m_parallelTasks[tail] = { dispatchState, dispatch, chunk };
        ++m_parallelTaskCount;
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
        Task task;
        ParallelTaskRecord parallelTask = {};
        bool hasParallelTask = false;
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            m_workAvailable.wait( lock, [&]() { return m_stopping || !m_tasks.empty() || m_parallelTaskCount > 0; } );

            if ( m_stopping && m_tasks.empty() && m_parallelTaskCount == 0 )
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
                task = std::move( m_tasks.front() );
                m_tasks.pop_front();
            }
        }

        try
        {
            if ( hasParallelTask )
            {
                if ( parallelTask.dispatch )
                {
                    parallelTask.dispatch( parallelTask.dispatchState, parallelTask.chunk );
                }
            }
            else
            {
                task();
            }
        }
        catch ( const std::exception& e )
        {
            fprintf( stderr, "[workers] Unhandled worker task exception: %s\n", e.what() );
        }
        catch ( ... )
        {
            fprintf( stderr, "[workers] Unhandled worker task exception.\n" );
        }
    }

    g_isWorkerThread = false;
    g_workerThreadIndex = -1;
}


bool RunWorkerSystemSelfTest( WorkerPool& pool, FILE* out )
{
    std::vector<int> squares( 257, 0 );
    pool.ParallelFor(
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
