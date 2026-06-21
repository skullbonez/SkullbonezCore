/*
File: SkullbonezSource/WorkerPool.cpp
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

Related:
  - SkullbonezSource/WorkerPool.h
  - SkullbonezSource/AmortizedTask.h
*/

#include "WorkerPool.h"
#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <memory>
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

struct ParallelForChunksState
{
    ParallelForChunksState( int taskCount, const WorkerPool::ChunkFunction& function )
        : fence( taskCount ), fn( function )
    {
    }

    void CaptureCurrentException()
    {
        std::lock_guard<std::mutex> lock( exceptionMutex );
        if ( !firstException )
        {
            firstException = std::current_exception();
        }
    }

    Fence fence;
    WorkerPool::ChunkFunction fn;
    std::mutex exceptionMutex;
    std::exception_ptr firstException;
};

class FenceSignalGuard
{
  public:
    explicit FenceSignalGuard( Fence& targetFence ) : m_fence( targetFence ), m_active( true )
    {
    }

    ~FenceSignalGuard()
    {
        if ( m_active )
        {
            m_fence.Signal();
        }
    }

    FenceSignalGuard( const FenceSignalGuard& ) = delete;
    FenceSignalGuard& operator=( const FenceSignalGuard& ) = delete;

  private:
    Fence& m_fence;
    bool m_active;
};
} // namespace

WorkerPool::WorkerPool() : m_stopping( false ), m_minParallelItems( 32 )
{
}


WorkerPool::~WorkerPool()
{
    Shutdown();
}


WorkerPool& WorkerPool::Instance()
{
    static WorkerPool s_pool;
    return s_pool;
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

    const IndexFunction fnCopy = fn;
    const char* markerPath = workerMarkerPath ? workerMarkerPath : "Frame/Workers/ParallelFor";
    const uint32_t markerHash = workerMarkerPath ? workerMarkerHash : HashStr( "Frame/Workers/ParallelFor" );
    const auto runChunk = [fnCopy, markerPath, markerHash]( int, int chunkBegin, int chunkEnd )
    {
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        ::SkullbonezCore::Basics::WorkerProfilerScope workerScope( markerPath, markerHash );
#else
        static_cast<void>( markerPath );
        static_cast<void>( markerHash );
#endif
        for ( int index = chunkBegin; index < chunkEnd; ++index )
        {
            fnCopy( index );
        }
    };

    if ( ShouldRunInline( itemCount, minParallelItems ) )
    {
        runChunk( 0, begin, end );
        return;
    }

    const std::vector<WorkerChunkRange> chunks = MakeChunks( begin, end, minParallelItems );
    ParallelForChunks( chunks, runChunk );
}


void WorkerPool::ParallelForChunks( const std::vector<WorkerChunkRange>& chunks, const ChunkFunction& fn )
{
    if ( chunks.empty() || !fn )
    {
        return;
    }

    if ( GetThreadCount() == 0 || IsCurrentThreadWorker() )
    {
        for ( const WorkerChunkRange& chunk : chunks )
        {
            fn( chunk.chunkIndex, chunk.begin, chunk.end );
        }
        return;
    }

    const std::shared_ptr<ParallelForChunksState> state =
        std::make_shared<ParallelForChunksState>( static_cast<int>( chunks.size() ), fn );

    for ( const WorkerChunkRange& chunk : chunks )
    {
        try
        {
            Submit(
                [state, chunk]()
                {
                    FenceSignalGuard signalGuard( state->fence );
                    try
                    {
                        state->fn( chunk.chunkIndex, chunk.begin, chunk.end );
                    }
                    catch ( ... )
                    {
                        state->CaptureCurrentException();
                    }
                } );
        }
        catch ( ... )
        {
            state->CaptureCurrentException();
            state->fence.Signal();
        }
    }

    state->fence.Wait();
    if ( state->firstException )
    {
        std::rethrow_exception( state->firstException );
    }
}


std::vector<WorkerChunkRange> WorkerPool::MakeChunks( int begin, int end, int minParallelItems ) const
{
    std::vector<WorkerChunkRange> chunks;
    const int itemCount = end - begin;
    if ( itemCount <= 0 )
    {
        return chunks;
    }

    if ( ShouldRunInline( itemCount, minParallelItems ) )
    {
        chunks.push_back( { 0, begin, end } );
        return chunks;
    }

    const int workerCount = (std::max)( 1, GetThreadCount() );
    const int chunkCount = (std::max)( 1, (std::min)( workerCount, itemCount ) );
    chunks.reserve( static_cast<size_t>( chunkCount ) );

    const int baseChunkSize = itemCount / chunkCount;
    const int remainder = itemCount % chunkCount;
    int cursor = begin;
    for ( int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
    {
        const int chunkSize = baseChunkSize + ( chunkIndex < remainder ? 1 : 0 );
        const int chunkEnd = cursor + chunkSize;
        chunks.push_back( { chunkIndex, cursor, chunkEnd } );
        cursor = chunkEnd;
    }

    return chunks;
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
        {
            std::unique_lock<std::mutex> lock( m_mutex );
            m_workAvailable.wait( lock, [&]() { return m_stopping || !m_tasks.empty(); } );

            if ( m_stopping && m_tasks.empty() )
            {
                break;
            }

            task = std::move( m_tasks.front() );
            m_tasks.pop_front();
        }

        try
        {
            task();
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


bool RunWorkerSystemSelfTest( FILE* out )
{
    WorkerPool& pool = WorkerPool::Instance();
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
