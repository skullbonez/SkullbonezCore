/*
File: SkullbonezSource/Core/WorkerPool.h
Purpose:
  Declares the fixed worker pool and deterministic chunk helpers.

Mental model:
  Worker threads are created once at startup. Frame code submits fixed callback
  records or uses no-allocation fork-join helpers; ordered collection merges
  deterministic local output on the caller thread.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Fork-join: Pattern where the main thread splits work, workers run chunks, and
  the main thread waits before merging results.
  Deterministic merge: Main-thread combine step that consumes worker chunks in a
  stable order so validation output remains reproducible.

Invariants:
  - WorkerChunkRange uses half-open ranges [begin, end), and chunkIndex is the
    deterministic merge order.
  - ParallelCollectOrdered lets workers build local output only; mergeChunk runs
    on the caller thread in chunk order.
  - General and parallel task rings are fixed-capacity; submission never grows
    a type-erased queue in steady runtime.

Related:
  - SkullbonezSource/Core/Fence.h
*/

#pragma once
#include "../Assets/AssetKeys.h"

#include "Fence.h"
#include "Profiler.h"

#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace SkullbonezCore
{
namespace Threading
{

struct WorkerChunkRange
{
    int chunkIndex;
    int begin;
    int end;
};

class WorkerPool
{
  public:
    static constexpr int MAX_PARALLEL_TASKS = 256;

    WorkerPool();
    ~WorkerPool();

    WorkerPool( const WorkerPool& ) = delete;
    WorkerPool& operator=( const WorkerPool& ) = delete;

    void Initialise( int requestedThreadCount );
    void Shutdown();

    // Submits a caller-owned typed task to the fixed ring. TaskT must provide
    // ExecuteWorkerTask(), and the object must outlive execution. Type erasure
    // stays private so runtime callers cannot build callback-style interfaces.
    template <typename TaskT> void SubmitNoAlloc( TaskT& task );
    template <typename IndexFunctionT>
    void ParallelForNoAlloc( int begin,
                             int end,
                             IndexFunctionT&& fn,
                             int minParallelItems,
                             const char* workerMarkerPath,
                             uint32_t workerMarkerHash );
    template <typename ChunkFunctionT>
    void ParallelForChunksNoAlloc( const WorkerChunkRange* chunks, int chunkCount, ChunkFunctionT&& fn );
    // Returns deterministic chunk ranges in caller-owned storage. Use this for
    // hot-path two-pass jobs that need prefix sums or fixed scratch before
    // calling ParallelForChunksNoAlloc().
    int BuildChunkRangesNoAlloc( int begin,
                                 int end,
                                 int minParallelItems,
                                 WorkerChunkRange* outChunks,
                                 int outCapacity ) const;

    int GetThreadCount() const;
    int GetMinParallelItems() const;
    bool IsInitialised() const;

    static int MaxThreadCount();
    static int ResolveThreadCount( int requestedThreadCount );
    static bool IsCurrentThreadWorker();
    static int CurrentWorkerIndex();

    template <typename ChunkOutput, typename BuildChunk, typename MergeChunk>
    void ParallelCollectOrdered( int begin,
                                 int end,
                                 std::vector<ChunkOutput>& chunkOutputs,
                                 BuildChunk buildChunk,
                                 MergeChunk mergeChunk,
                                 int minParallelItems = 0 )
    {
        WorkerChunkRange chunks[WORKER_PARALLEL_TASK_CAPACITY];
        const int chunkCount = BuildChunks( begin, end, minParallelItems, chunks, WORKER_PARALLEL_TASK_CAPACITY );
        chunkOutputs.clear();
        chunkOutputs.resize( static_cast<size_t>( chunkCount ) );

        ParallelForChunksNoAlloc(
            chunks,
            chunkCount,
            [&]( int chunkIndex, int chunkBegin, int chunkEnd )
            { buildChunk( chunkIndex, chunkBegin, chunkEnd, chunkOutputs[static_cast<size_t>( chunkIndex )] ); } );

        for ( size_t chunkIndex = 0; chunkIndex < chunkOutputs.size(); ++chunkIndex )
        {
            mergeChunk( static_cast<int>( chunkIndex ), chunkOutputs[chunkIndex] );
        }
    }

  private:
    using TaskDispatcher = void ( * )( void* taskState );
    using ParallelTaskDispatcher = void ( * )( void* dispatchState, const WorkerChunkRange& chunk );

    struct TaskRecord
    {
        void* state;
        TaskDispatcher dispatch;
    };

    struct ParallelTaskRecord
    {
        void* dispatchState;
        ParallelTaskDispatcher dispatch;
        WorkerChunkRange chunk;
    };

    static constexpr int WORKER_PARALLEL_TASK_CAPACITY = MAX_PARALLEL_TASKS;

    template <typename ChunkFunctionT> struct ParallelForChunksState
    {
        using Function = typename std::remove_reference<ChunkFunctionT>::type;

        ParallelForChunksState( int taskCount, Function& function ) : fence( taskCount ), fn( &function )
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
        Function* fn;
        std::mutex exceptionMutex;
        std::exception_ptr firstException;
    };

    template <typename ChunkFunctionT>
    static void ExecuteParallelChunkTask( void* dispatchState, const WorkerChunkRange& chunk );
    template <typename TaskT> static void ExecuteTaskRecord( void* taskState );

    void WorkerLoop( int workerIndex );
    bool ShouldRunInline( int itemCount, int minParallelItems ) const;
    int BuildChunks( int begin, int end, int minParallelItems, WorkerChunkRange* outChunks, int outCapacity ) const;
    void SubmitTaskRecord( void* taskState, TaskDispatcher dispatch );
    void SubmitParallelChunk( void* dispatchState, ParallelTaskDispatcher dispatch, const WorkerChunkRange& chunk );

    mutable std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    // Fixed callback records keep asynchronous replay slices allocation-free.
    TaskRecord m_tasks[WORKER_PARALLEL_TASK_CAPACITY];
    int m_taskHead;
    int m_taskCount;
    int m_taskHighWater;
    // Runtime allocation policy:
    //   Preallocated as inline WorkerPool storage for worker-count bounded
    //   parallel dispatch. Steady gameplay must not grow task records; overflow
    //   fails through SB_FATAL with the worker queue capacity instead of
    //   allocating.
    ParallelTaskRecord m_parallelTasks[WORKER_PARALLEL_TASK_CAPACITY];
    int m_parallelTaskHead;
    int m_parallelTaskCount;
    int m_parallelTaskHighWater;
    std::vector<std::thread> m_threads;
    bool m_stopping;
    int m_minParallelItems;
};

template <typename TaskT> void WorkerPool::SubmitNoAlloc( TaskT& task )
{
    SubmitTaskRecord( &task, &WorkerPool::ExecuteTaskRecord<TaskT> );
}

template <typename TaskT> void WorkerPool::ExecuteTaskRecord( void* taskState )
{
    static_cast<TaskT*>( taskState )->ExecuteWorkerTask();
}

template <typename IndexFunctionT>
void WorkerPool::ParallelForNoAlloc( int begin,
                                     int end,
                                     IndexFunctionT&& fn,
                                     int minParallelItems,
                                     const char* workerMarkerPath,
                                     uint32_t workerMarkerHash )
{
    const int itemCount = end - begin;
    if ( itemCount <= 0 )
    {
        return;
    }

    IndexFunctionT& indexFn = fn;
    const char* markerPath = workerMarkerPath ? workerMarkerPath : "Frame/Workers/ParallelFor";
    const uint32_t markerHash = workerMarkerPath ? workerMarkerHash : HashStr( "Frame/Workers/ParallelFor" );
    const auto runChunk = [&]( int, int chunkBegin, int chunkEnd )
    {
#if defined( SKULLBONEZ_PROFILE_ENABLED )
        ::SkullbonezCore::Basics::WorkerProfilerScope workerScope( markerPath, markerHash );
#else
        static_cast<void>( markerPath );
        static_cast<void>( markerHash );
#endif
        for ( int index = chunkBegin; index < chunkEnd; ++index )
        {
            indexFn( index );
        }
    };

    if ( ShouldRunInline( itemCount, minParallelItems ) )
    {
        runChunk( 0, begin, end );
        return;
    }

    WorkerChunkRange chunks[WORKER_PARALLEL_TASK_CAPACITY];
    const int chunkCount = BuildChunks( begin, end, minParallelItems, chunks, WORKER_PARALLEL_TASK_CAPACITY );
    ParallelForChunksNoAlloc( chunks, chunkCount, runChunk );
}


template <typename ChunkFunctionT>
void WorkerPool::ParallelForChunksNoAlloc( const WorkerChunkRange* chunks, int chunkCount, ChunkFunctionT&& fn )
{
    if ( !chunks || chunkCount <= 0 )
    {
        return;
    }

    typename std::remove_reference<ChunkFunctionT>::type& chunkFn = fn;
    if ( GetThreadCount() == 0 || IsCurrentThreadWorker() )
    {
        for ( int index = 0; index < chunkCount; ++index )
        {
            const WorkerChunkRange& chunk = chunks[index];
            chunkFn( chunk.chunkIndex, chunk.begin, chunk.end );
        }
        return;
    }

    ParallelForChunksState<ChunkFunctionT> state( chunkCount, chunkFn );

    for ( int index = 0; index < chunkCount; ++index )
    {
        const WorkerChunkRange& chunk = chunks[index];
        try
        {
            SubmitParallelChunk( &state, &WorkerPool::ExecuteParallelChunkTask<ChunkFunctionT>, chunk );
        }
        catch ( ... )
        {
            state.CaptureCurrentException();
            state.fence.Signal();
        }
    }

    state.fence.Wait();
    if ( state.firstException )
    {
        std::rethrow_exception( state.firstException );
    }
}


template <typename ChunkFunctionT>
void WorkerPool::ExecuteParallelChunkTask( void* dispatchState, const WorkerChunkRange& chunk )
{
    auto* state = static_cast<ParallelForChunksState<ChunkFunctionT>*>( dispatchState );
    if ( !state )
    {
        return;
    }

    try
    {
        ( *state->fn )( chunk.chunkIndex, chunk.begin, chunk.end );
    }
    catch ( ... )
    {
        state->CaptureCurrentException();
    }
    state->fence.Signal();
}

bool RunWorkerSystemSelfTest( WorkerPool& pool, FILE* out );

} // namespace Threading
} // namespace SkullbonezCore
