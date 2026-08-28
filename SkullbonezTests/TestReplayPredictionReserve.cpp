// Runtime-only checks for prediction allocations admitted by the shared replay reserve owner.

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPrediction.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h"

#include <cstdint>
#include <limits>
#include <memory>

TEST_CASE( "Replay prediction archive candidate grant covers object and constructor backing allocations" )
{
    using namespace SkullbonezCore::Core::Allocation;
    using SkullbonezCore::Runtime::ReplayPredictionSolverEvidenceBanks;
    using SkullbonezCore::Runtime::RunReplayPredictionState;
    using SkullbonezCore::Runtime::ReplayPredictionArchiveOperations::ReplayPredictionArchiveCandidateAllocationBudgetBytes;
    using SkullbonezCore::Runtime::ReplayPredictionReserveOperations::ReplayPredictionReserveOwner;
    using SkullbonezCore::Runtime::ReplayPredictionReserveOperations::RequestReplayPredictionReserveGrowth;

    RuntimeReserveAllocator::ResetCounters();
    const uint64_t allocationBytes = ReplayPredictionArchiveCandidateAllocationBudgetBytes();
    REQUIRE( allocationBytes <= static_cast<uint64_t>( ( std::numeric_limits<int>::max )() ) );
    RuntimeReserveGrowthResult result = {};
    REQUIRE( RequestReplayPredictionReserveGrowth( "unit.archive.candidate", -1, 0, static_cast<int>( allocationBytes ), 1,
                                                   result, allocationBytes ) );

    const RuntimeReserveOwnerHandle owner = ReplayPredictionReserveOwner();
    const uint64_t violationsBefore = RuntimeAllocationGuardViolationCount();
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Gameplay );
    bool predictionConstructed = false;
    bool evidenceConstructed = false;
    RuntimeReserveOwnerStatsView stats = {};
    RuntimeReserveOwnerStatsView releasedStats = {};
    uint64_t violationsAfter = 0u;
    {
        RuntimeAllocationScope replayPhase( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, result );
        auto prediction = std::make_unique<RunReplayPredictionState>();
        auto evidence = std::make_unique<ReplayPredictionSolverEvidenceBanks>();
        predictionConstructed = prediction != nullptr;
        evidenceConstructed = evidence != nullptr;
        (void)RuntimeReserveAllocator::CopyOwnerStats( owner, stats );
        violationsAfter = RuntimeAllocationGuardViolationCount();
    }
    SetRuntimeAllocationGuardMode( RuntimeAllocationGuardMode::Off );
    REQUIRE( RuntimeReserveAllocator::CopyOwnerStats( owner, releasedStats ) );
    REQUIRE( predictionConstructed );
    REQUIRE( evidenceConstructed );
    CHECK( stats.activeBytes <= allocationBytes );
    CHECK( stats.pendingReplayGrantBytes == allocationBytes - stats.activeBytes );
    CHECK( violationsAfter == violationsBefore );
    CHECK( releasedStats.activeBytes == 0u );
    CHECK( releasedStats.pendingReplayGrantBytes == 0u );
}
