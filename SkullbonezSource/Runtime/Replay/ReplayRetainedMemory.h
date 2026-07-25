/*
File: SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h
Purpose:
  Defines durable replay sample ownership and the registered runtime-growth
  contracts shared by recording, solver snapshots, and prediction.

Summary:
  Replay retains three live data families and writes one cold artifact family.
  Each live growth owner has one name, replay-only phase, hard byte cap,
  measured high-water reference, counter source, and exhaustion rule.

Glossary:
  Presentation sample: Render-facing retained pose/style state owned by
    ReplayRecorder.
  Solver sample: Physics-facing retained state owned by ReplaySolverRecorder.
  Prediction prefix: Coherent published future-frame prefix owned by
    ReplayPrediction and its TrajectoryStore.
  Artifact document: Cold-I/O v2 representation owned by ReplayV2Artifact.
  Exhaustion rule: Required behavior when a registered growth request exceeds
    its hard cap.

Invariants:
  - The policy table names every RuntimeReserveAllocator owner registered for
    replay data growth.
  - Prediction denial cancels or truncates the current build without publishing
    an incoherent prefix.
  - Recorder and solver-snapshot denial is fatal because partial retained state
    would make scrub/restore nondeterministic.
  - Artifact load/save growth remains cold I/O and never registers as live
    replay growth.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRecorder.h
  - SkullbonezSource/Runtime/Replay/ReplayPredictionReserve.h
  - SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h
  - SkullbonezSource/Physics/PhysicsSolverSnapshot.h
  - SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h
*/
#pragma once

#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../../Physics/PhysicsSolverSnapshot.h"

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

inline constexpr std::array<ReplayRetainedOwnershipRule, 4> REPLAY_RETAINED_OWNERSHIP_RULES = {
    ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::PresentationRecorder,
                                  "ReplayPresentationSample",
                                  "ReplayRecorder",
                                  true,
                                  true },
    ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::SolverRecorder,
                                  "ReplaySolverFrameSample",
                                  "ReplaySolverRecorder",
                                  true,
                                  true },
    ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::PredictionPrefix,
                                  "RunReplayPredictionFrame",
                                  "ReplayPrediction working set and trajectory storage",
                                  true,
                                  false },
    ReplayRetainedOwnershipRule { ReplayRetainedDataOwner::V2Artifact,
                                  "ReplayV2Document",
                                  "ReplayV2Artifact cold I/O",
                                  false,
                                  true }
};

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
// The strict two-generation prediction probe measured 17,737,640 aggregate
// recorder bytes. Thirty-two MiB preserves 1.89x measured headroom.
inline constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES = 32 * 1024 * 1024;
inline constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";
// The same strict probe measured 110,979,828 prediction bytes. The 256 MiB cap
// preserves 2.42x headroom for larger retained path/cause-tree generations.
inline constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;

inline constexpr std::array<ReplayGrowthOwnerPolicy, 3> REPLAY_GROWTH_OWNER_POLICIES = {
    ReplayGrowthOwnerPolicy { REPLAY_RECORDER_SAMPLE_RESERVE_OWNER,
                              SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                              REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES,
                              17737640u,
                              ReplayGrowthExhaustionRule::FatalRetainedState },
    ReplayGrowthOwnerPolicy { Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER,
                              SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                              Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES,
                              2877186u,
                              ReplayGrowthExhaustionRule::FatalRetainedState },
    ReplayGrowthOwnerPolicy { REPLAY_PREDICTION_RESERVE_OWNER,
                              SkullbonezCore::Core::Allocation::RuntimeReservePhase::Replay,
                              REPLAY_PREDICTION_RESERVE_HARD_BYTES,
                              110979828u,
                              ReplayGrowthExhaustionRule::CancelPredictionBuild }
};

inline const ReplayGrowthOwnerPolicy* FindReplayGrowthOwnerPolicy( const char* ownerName ) noexcept
{
    if ( !ownerName )
    {
        return nullptr;
    }
    for ( const ReplayGrowthOwnerPolicy& policy : REPLAY_GROWTH_OWNER_POLICIES )
    {
        const char* lhs = policy.ownerName;
        const char* rhs = ownerName;
        while ( *lhs != '\0' && *lhs == *rhs )
        {
            ++lhs;
            ++rhs;
        }
        if ( *lhs == '\0' && *rhs == '\0' )
        {
            return &policy;
        }
    }
    return nullptr;
}
} // namespace SkullbonezCore::Runtime
