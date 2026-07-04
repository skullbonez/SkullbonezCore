/*
File: SkullbonezSource/Core/MainMemoryStats.h
Purpose:
  Defines POD snapshots for main-memory diagnostics.

Mental model:
  These structs are neutral data contracts. Runtime diagnostics fills the
  process-level snapshot, while replay, game-object, and UI code use the same
  PODs without depending on each other's owners.

Glossary:
  POD (Plain Old Data): Simple value type with no ownership or behavior.
  Task Manager metric: Named process-memory field used as the top-level total.
  Reconciled total: Tracked engine bytes plus unattributed bytes, adjusted for
    any tracked overshoot so it matches the sampled process metric.

Invariants:
  - Byte fields are CPU-visible main memory only.
  - GPU default heaps are not included in these totals.

Related:
  - SkullbonezSource/Runtime/RuntimeDiagnostics.h
  - Agentic/Plans/main-memory-profiling-plan.md
*/
#pragma once

#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Basics
{
struct MainMemoryProcessStats
{
    bool available = false;                                 // False when the OS process-memory query failed.
    char taskManagerMetricName[32] = "private_working_set"; // Named process metric used as the top-level total.
    uint64_t taskManagerBytes = 0;                          // Top-level process memory shown in the profiler/dump.
    uint64_t workingSetBytes = 0;                           // Resident process working set.
    uint64_t privateWorkingSetBytes = 0;                    // Resident pages private to this process.
    uint64_t privateCommitBytes = 0;                        // Private committed bytes reported by PROCESS_MEMORY_COUNTERS_EX.
    uint64_t pagefileUsageBytes = 0;                        // Pagefile-backed process usage for correlation with tools.
};

struct MainMemoryReplayStats
{
    uint64_t presentationBytes = 0;
    uint64_t solverBytes = 0;
    uint64_t eventsBytes = 0;
    uint64_t loadedReplayBytes = 0;
    uint64_t predictionBytes = 0;
    uint64_t pathAndCauseBytes = 0;
    uint64_t renderScratchBytes = 0;
    uint64_t totalBytes = 0;
    std::size_t presentationSamples = 0;
    std::size_t solverSamples = 0;
    std::size_t eventSamples = 0;
    std::size_t loadedReplaySamples = 0;
    std::size_t predictionFrames = 0;
    std::size_t pathNodes = 0;
    std::size_t causeRows = 0;
    std::size_t ghostRequests = 0;
};

struct MainMemoryGameObjectStats
{
    uint64_t modelVectorBytes = 0;                          // Dynamic GameModel vector capacity.
    uint64_t physicsStoreBytes = 0;                         // Physics body-store vector capacity.
    uint64_t colliderStoreBytes = 0;                        // Collider-store vector capacity.
    uint64_t renderStoreBytes = 0;                          // Render-instance vector capacity.
    uint64_t physicsWorldBytes = 0;                         // PhysicsWorld fixed state plus retained dynamic solver memory.
    uint64_t debugAndBroadphaseBytes = 0;                   // Informational subset already included in physicsWorldBytes.
    uint64_t totalBytes = 0;
    std::size_t modelCount = 0;
    std::size_t modelCapacity = 0;
    std::size_t bodyStoreCapacity = 0;
    std::size_t colliderStoreCapacity = 0;
    std::size_t renderStoreCapacity = 0;
};

struct MainMemoryStats
{
    MainMemoryProcessStats process;
    MainMemoryReplayStats replay;
    MainMemoryGameObjectStats gameObjects;
    uint64_t otherTrackedBytes = 0;
    uint64_t trackedEngineBytes = 0;
    uint64_t unattributedProcessBytes = 0;
    uint64_t trackedOvershootBytes = 0;
    uint64_t reconciledTotalBytes = 0;
    uint64_t reconciliationDeltaBytes = 0;
    double sampleTimeSeconds = 0.0;
};
} // namespace Basics
} // namespace SkullbonezCore
