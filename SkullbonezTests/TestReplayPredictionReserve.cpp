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
#include <vector>

TEST_CASE( "Replay prediction frame payload reserve fills new horizon slots after a smaller scene load" )
{
    using SkullbonezCore::Runtime::ReplayPredictionReserveOperations::ReserveReplayPredictionFramePayloadVectors;

    struct PredictionFrame
    {
        std::vector<int> bodies;
    };

    std::vector<PredictionFrame> frames( 4u );
    frames[0].bodies.reserve( 4u );
    frames[1].bodies.reserve( 4u );

    // The first two rows carry as many aggregate elements as the four-row
    // request, but the new horizon rows still require their own backing.
    REQUIRE( frames[0].bodies.capacity() + frames[1].bodies.capacity() >= 8u );
    REQUIRE( frames[2].bodies.capacity() == 0u );
    REQUIRE( frames[3].bodies.capacity() == 0u );

    REQUIRE( ReserveReplayPredictionFramePayloadVectors( frames, frames.size(), 2u, 0,
                                                          "unit.prediction.frame_payload_holes",
                                                          &PredictionFrame::bodies ) );

    for ( const PredictionFrame& frame : frames )
    {
        CHECK( frame.bodies.capacity() >= 2u );
    }
}

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
