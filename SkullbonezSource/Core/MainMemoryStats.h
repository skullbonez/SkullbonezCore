/*
File: SkullbonezSource/Core/MainMemoryStats.h
Purpose:
  Defines POD snapshots for main-memory diagnostics.

Summary:
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
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore
{
namespace Core
{
// Concept: replay diagnostics use enum-indexed POD arrays so memory dumps and UI
// frames can copy counters and byte categories without allocating or depending
// on replay owner types.
enum class MainMemoryReplayTrajectoryLane : std::size_t
{
    PastRoot,
    FutureRoot,
    FutureChildIncoming,
    FutureChildOutgoing,
    RetainedTrail,
    BaselineRoot,
    CausalMarker,
    AuxiliaryTrail,
    Count
};

inline constexpr std::size_t MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT =
    static_cast<std::size_t>( MainMemoryReplayTrajectoryLane::Count );

enum class MainMemoryReplayBudgetPass : std::size_t
{
    PredictionBegin,
    PredictionStep,
    PredictionBuildTree,
    RetainedRefresh,
    Count
};

inline constexpr std::size_t MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT =
    static_cast<std::size_t>( MainMemoryReplayBudgetPass::Count );

enum class MainMemoryReplayRebuildCause : std::size_t
{
    Dirty,
    AutomaticRefresh,
    Count
};

inline constexpr std::size_t MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT =
    static_cast<std::size_t>( MainMemoryReplayRebuildCause::Count );

enum class MainMemoryReplayByteCategory : std::size_t
{
    PresentationOwner,
    PresentationSampleRecords,
    PresentationCheckpoints,
    PresentationScratch,
    PresentationBodies,
    SolverOwner,
    SolverSampleRecords,
    SolverCheckpoints,
    SolverScratch,
    SolverBodies,
    SolverWorldState,
    SolverLauncherVisuals,
    EventsOwner,
    Events,
    LoadedOwner,
    LoadedSampleRecords,
    LoadedBodies,
    PredictionOwner,
    PredictionEngine,
    PredictionWorldState,
    PredictionBodyState,
    PredictionFrameRecords,
    PredictionFrameBodies,
    PredictionDebugContacts,
    PredictionFutureTree,
    PathOwner,
    PathTargets,
    PathFutureNodes,
    PathCauseRows,
    RenderGhostRequests,
    RenderFocusMask,
    RenderLauncherBackup,
    TrajectoryStore,
    Count
};

inline constexpr std::size_t MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT =
    static_cast<std::size_t>( MainMemoryReplayByteCategory::Count );

struct MainMemoryReplayCategoryBytes
{
    uint64_t bytes[MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT] = {};
};

inline uint64_t MainMemoryReplayCategoryByte( const MainMemoryReplayCategoryBytes& categories,
                                              MainMemoryReplayByteCategory category )
{
    const std::size_t categoryIndex = static_cast<std::size_t>( category );
    return categoryIndex < MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT ? categories.bytes[categoryIndex] : 0;
}

inline void MainMemoryAddReplayCategoryBytes( MainMemoryReplayCategoryBytes& categories,
                                              MainMemoryReplayByteCategory category,
                                              uint64_t bytes )
{
    const std::size_t categoryIndex = static_cast<std::size_t>( category );
    if ( categoryIndex < MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT )
    {
        categories.bytes[categoryIndex] += bytes;
    }
}

inline uint64_t MainMemoryReplayCategoryRangeBytes( const MainMemoryReplayCategoryBytes& categories,
                                                    MainMemoryReplayByteCategory first,
                                                    MainMemoryReplayByteCategory end )
{
    const std::size_t firstIndex = static_cast<std::size_t>( first );
    const std::size_t endIndex = static_cast<std::size_t>( end );
    uint64_t total = 0;
    for ( std::size_t i = firstIndex; i < endIndex && i < MAIN_MEMORY_REPLAY_BYTE_CATEGORY_COUNT; ++i )
    {
        total += categories.bytes[i];
    }
    return total;
}

struct MainMemoryReplayTrajectoryStats
{
    uint64_t storeBytes = 0;                                // Current TrajectoryStore allocation; 0 until the store lands.
    uint64_t recordCount = 0;                               // Live TrajectoryStore record count visible to replay tooling.
    uint64_t pointCount = 0;                                // Total stored trajectory points, including unpublished build slack.
    uint64_t publishedPointCount = 0;                       // Points currently exposed through record published prefixes.
    uint64_t versionChurn = 0;                              // Number of allocated record versions since the store was reset.
    uint32_t maxRecordVersion = 0;                          // Highest version still resident in the store.
    uint64_t emittedSegments[MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT] = {};
    uint64_t droppedSegments[MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT] = {};
    uint64_t budgetExpiries[MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT] = {};
    uint64_t rebuildCauses[MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT] = {};
};

struct MainMemoryReplayTrajectorySubmissionStats
{
    bool hasGeometry = false;                               // True when the tracer submitted replay ribbon vertices this frame.
    uint64_t vertexHash = 0;                                // FNV hash of the exact submitted replay ribbon vertex byte stream.
    uint64_t vertexBytes = 0;                               // Submitted replay ribbon byte count for the frame.
    uint32_t vertexCount = 0;                               // Submitted replay ribbon vertex count for the frame.
    uint32_t segmentCount = 0;                              // Source replay ribbon segment count expanded into vertices.
};

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
    // One fixed row per ReplayRetainedMemory growth policy. Measured high-water
    // is the evidence used to set the cap; allocator high-water is this run.
    struct GrowthOwner
    {
        const char* ownerName = nullptr;
        uint64_t measuredHighWaterBytes = 0;
        uint64_t allocatorHighWaterBytes = 0;
        uint64_t replayGrowths = 0;
        uint64_t failedGrowths = 0;
        int hardBytes = 0;
        int reportedHighWaterCapacity = 0;                  // Owner capacity units; replay byte owners report bytes.
        int lastGrowthFrame = -1;
        bool registered = false;
    };

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
    // Replay policy fields report the requested knobs and the resolved recorder
    // windows that were actually applied by ReplayRuntime.
    int memoryPreset = 0;                                   // 0=lossless look, 1=balanced, 2=compact.
    int requestedRetentionSeconds = 0;
    int requestedBudgetMiB = 0;
    int presentationRetentionSeconds = 0;
    int solverRetentionSeconds = 0;
    bool memoryBudgetClamped = false;
    bool solverWindowReduced = false;
    std::array<GrowthOwner, 3> growthOwners;                // Same stable order as REPLAY_GROWTH_OWNER_POLICIES.
    MainMemoryReplayCategoryBytes categoryBytes;
    MainMemoryReplayTrajectoryStats trajectory;
};

struct MainMemoryGameObjectStats
{
    uint64_t modelVectorBytes = 0;                          // Dynamic legacy object record vector capacity.
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
} // namespace Core
} // namespace SkullbonezCore
