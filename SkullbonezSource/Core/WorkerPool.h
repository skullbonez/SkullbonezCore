/*
File: SkullbonezSource/Core/WorkerPool.h
Purpose:
  Declares the fixed worker pool and deterministic chunk helpers.

Mental model:
  Worker threads are created once at startup. Frame code can use ParallelFor
  for fork-join work, or ordered chunk collection for deterministic local
  output followed by main-thread merge in chunk order.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Fork-join: Pattern where the main thread splits work, workers run chunks, and
  the main thread waits before merging results.
  Deterministic merge: Main-thread combine step that consumes worker chunks in a
  stable order so validation output remains reproducible.

Related:
  - Agentic/Plans/worker-system-plan.md
  - Agentic/Plans/physics-shadow-worker-parallelization-plan.md
  - SkullbonezSource/Core/Fence.h
*/

#pragma once

#include "Fence.h"

#include <cstdio>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
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
    using Task = std::function<void()>;
    using IndexFunction = std::function<void( int )>;
    using ChunkFunction = std::function<void( int chunkIndex, int begin, int end )>;

    static WorkerPool& Instance();

    WorkerPool();
    ~WorkerPool();

    WorkerPool( const WorkerPool& ) = delete;
    WorkerPool& operator=( const WorkerPool& ) = delete;

    void Initialise( int requestedThreadCount );
    void Shutdown();

    void Submit( Task task );
    void ParallelFor( int begin,
                      int end,
                      const IndexFunction& fn,
                      int minParallelItems,
                      const char* workerMarkerPath,
                      uint32_t workerMarkerHash );
    void ParallelForChunks( const std::vector<WorkerChunkRange>& chunks, const ChunkFunction& fn );
    std::vector<WorkerChunkRange> MakeChunks( int begin, int end, int minParallelItems = 0 ) const;

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
        const std::vector<WorkerChunkRange> chunks = MakeChunks( begin, end, minParallelItems );
        chunkOutputs.clear();
        chunkOutputs.resize( chunks.size() );

        ParallelForChunks(
            chunks,
            [&]( int chunkIndex, int chunkBegin, int chunkEnd )
            { buildChunk( chunkIndex, chunkBegin, chunkEnd, chunkOutputs[static_cast<size_t>( chunkIndex )] ); } );

        for ( size_t chunkIndex = 0; chunkIndex < chunkOutputs.size(); ++chunkIndex )
        {
            mergeChunk( static_cast<int>( chunkIndex ), chunkOutputs[chunkIndex] );
        }
    }

  private:
    void WorkerLoop( int workerIndex );
    bool ShouldRunInline( int itemCount, int minParallelItems ) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    std::deque<Task> m_tasks;
    std::vector<std::thread> m_threads;
    bool m_stopping;
    int m_minParallelItems;
};

bool RunWorkerSystemSelfTest( FILE* out );

} // namespace Threading
} // namespace SkullbonezCore
