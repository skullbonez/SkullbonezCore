/*
File: SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
Purpose:
  Defines durable replay sample ownership and Replay-core runtime-growth contracts.

Summary:
  Replay retains recorded presentation and solver data and writes one cold
  artifact family. Prediction owns its separate working-set policy row.

Glossary:
  Prediction prefix: Coherent published future-frame prefix owned by
    ReplayPrediction and its TrajectoryStore.
  Artifact document: Cold-I/O v2 representation owned by ReplayV2Artifact.
  Exhaustion rule: Required behavior when a registered growth request exceeds
    its hard cap.

Invariants:
  - The policy table names the Replay-core and Physics snapshot owners.
  - Recorder and solver-snapshot denial is fatal because partial retained state
    would make scrub/restore nondeterministic.
  - Artifact load/save growth remains cold I/O and never registers as live
    replay growth.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - SkullbonezSource/Physics/PhysicsSolverSnapshot.h
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/PhysicsSolverSnapshot.h"
#include "ReplayCaptureLimits.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Runtime
{
enum class ReplayRetainedDataOwner : uint8_t
{
    PresentationRecorder,
    SolverRecorder,
    PredictionPrefix,
    V2Artifact
};

struct ReplayRetainedOwnershipRule
{
    ReplayRetainedDataOwner owner;
    const char* valueType;
    const char* lifetimeOwner;
    bool retainedAtRuntime;
    bool durableArtifact;
};

inline constexpr std::array<ReplayRetainedOwnershipRule, 4> REPLAY_RETAINED_OWNERSHIP_RULES =
    { ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::PresentationRecorder, "ReplayPresentationSample",
                                    "ReplayRecorder", true, true },
      ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::SolverRecorder, "ReplaySolverFrameSample",
                                    "ReplaySolverRecorder", true, true },
      ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::PredictionPrefix, "RunReplayPredictionFrame",
                                    "ReplayPrediction working set and trajectory storage", true, false },
      ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::V2Artifact, "ReplayV2Document", "ReplayV2Artifact cold I/O",
                                    false, true } };

enum class ReplayGrowthExhaustionRule : uint8_t
{
    FatalRetainedState,
    CancelPredictionBuild
};

struct ReplayGrowthOwnerPolicy
{
    const char* ownerName;
    SkullbonezCore::Core::Allocation::RuntimeReservePhase phase;
    int hardBytes;
    uint64_t measuredHighWaterBytes;
    ReplayGrowthExhaustionRule exhaustion;
};

inline constexpr const char* REPLAY_RECORDER_SAMPLE_RESERVE_OWNER = "replay_recorder_samples";

// The strict two-generation prediction probe measured 16,223,044 aggregate
// recorder bytes, while the ordinary 300-body generated demo legitimately
// exceeds 32 MiB before its first second of history is complete. Keep the
// process-wide ceiling aligned with the largest supported replay memory budget; individual
// vectors remain bounded by the scene/body and source-owner limits.
inline constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES = REPLAY_MEMORY_POLICY_MAX_BUDGET_MIB * 1024 * 1024;
inline constexpr std::array<ReplayGrowthOwnerPolicy, 2> REPLAY_CORE_GROWTH_OWNER_POLICIES =
    { ReplayGrowthOwnerPolicy { REPLAY_RECORDER_SAMPLE_RESERVE_OWNER,
                                SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                                REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES, 16223044u,
                                ReplayGrowthExhaustionRule::FatalRetainedState },
      ReplayGrowthOwnerPolicy { Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER,
                                SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                                Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES, 3401552u,
                                ReplayGrowthExhaustionRule::FatalRetainedState } };
} // namespace SkullbonezCore::Runtime
