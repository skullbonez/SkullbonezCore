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
    ReplayRuntime prediction/trajectory storage.
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
  - SkullbonezSource/Runtime/Allocation/RuntimeReserveAllocator.h
*/
#pragma once

#include "../Allocation/RuntimeReserveAllocator.h"

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
    ReplayRetainedOwnershipRule{ ReplayRetainedDataOwner::PresentationRecorder,
                                 "ReplayPresentationSample",
                                 "ReplayRecorder",
                                 true,
                                 true },
    ReplayRetainedOwnershipRule{ ReplayRetainedDataOwner::SolverRecorder,
                                 "ReplaySolverFrameSample",
                                 "ReplaySolverRecorder",
                                 true,
                                 true },
    ReplayRetainedOwnershipRule{ ReplayRetainedDataOwner::PredictionPrefix,
                                 "RunReplayPredictionFrame",
                                 "ReplayRuntime prediction and trajectory owners",
                                 true,
                                 false },
    ReplayRetainedOwnershipRule{ ReplayRetainedDataOwner::V2Artifact,
                                 "ReplayV2Document",
                                 "ReplayV2Artifact cold I/O",
                                 false,
                                 true } };

enum class ReplayGrowthExhaustionRule : uint8_t
{
    FatalRetainedState,
    CancelPredictionBuild
};

struct ReplayGrowthOwnerPolicy
{
    const char* ownerName;
    Runtime::Allocation::RuntimeReservePhase phase;
    int hardBytes;
    uint64_t measuredHighWaterBytes;
    ReplayGrowthExhaustionRule exhaustion;
};

inline constexpr const char* REPLAY_RECORDER_SAMPLE_RESERVE_OWNER = "replay_recorder_samples";
// Guarded 60-frame capture measured 6,206,626 bytes. Thirty-two MiB preserves
// more than 5x headroom for denser retained scenes.
inline constexpr int REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES = 32 * 1024 * 1024;
inline constexpr const char* REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER = "replay_solver_snapshot";
// Measured high-water is 1,437,696 bytes. Eight MiB leaves more than 5x
// headroom while deleting the unmeasured 64 MiB ceiling.
inline constexpr int REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES = 8 * 1024 * 1024;
inline constexpr const char* REPLAY_PREDICTION_RESERVE_OWNER = "replay_prediction_working_set";
inline constexpr int REPLAY_PREDICTION_RESERVE_HARD_BYTES = 256 * 1024 * 1024;

inline constexpr std::array<ReplayGrowthOwnerPolicy, 3> REPLAY_GROWTH_OWNER_POLICIES = {
    ReplayGrowthOwnerPolicy{ REPLAY_RECORDER_SAMPLE_RESERVE_OWNER,
                             Runtime::Allocation::RuntimeReservePhase::Replay,
                             REPLAY_RECORDER_SAMPLE_RESERVE_HARD_BYTES,
                             6206626u,
                             ReplayGrowthExhaustionRule::FatalRetainedState },
    ReplayGrowthOwnerPolicy{ REPLAY_SOLVER_SNAPSHOT_RESERVE_OWNER,
                             Runtime::Allocation::RuntimeReservePhase::Replay,
                             REPLAY_SOLVER_SNAPSHOT_RESERVE_HARD_BYTES,
                             1437696u,
                             ReplayGrowthExhaustionRule::FatalRetainedState },
    ReplayGrowthOwnerPolicy{ REPLAY_PREDICTION_RESERVE_OWNER,
                             Runtime::Allocation::RuntimeReservePhase::Replay,
                             REPLAY_PREDICTION_RESERVE_HARD_BYTES,
                             211376304u,
                             ReplayGrowthExhaustionRule::CancelPredictionBuild } };

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
