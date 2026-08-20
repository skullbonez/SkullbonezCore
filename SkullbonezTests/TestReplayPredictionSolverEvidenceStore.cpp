/*
File: SkullbonezTests/TestReplayPredictionSolverEvidenceStore.cpp
Purpose:
  Proves segmented prediction evidence publication, promotion, and release.

Summary:
  These tests exercise the store without running Physics. Detached contact and
  pipeline values cross segment boundaries while a reader observes the sealed
  prefix, then paired banks prove cancellation, promotion, capacity coexistence,
  cap denial, and explicit release with historical peaks intact.

Invariants:
  - Only exact generation/mode/epoch/frame/publication identities resolve.
  - Published row addresses remain stable across later segment allocation.
  - Failed reserve or append operations never publish a partial frame.
  - Release reaches zero current capacity and is idempotent.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h
  - Agentic/Reference/engine-glossary.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h"

#include <array>
#include <atomic>
#include <limits>
#include <thread>

using namespace SkullbonezCore;
using namespace SkullbonezCore::Runtime;

namespace
{
Physics::PhysicsSolverPersistentContactSample ContactRow( uint32_t featureId )
{
    Physics::PhysicsSolverPersistentContactSample row;
    row.bodyA = 1;
    row.bodyB = 2;
    row.featureId = featureId;
    return row;
}

Physics::PhysicsPipelineRecord PipelineRow( uint32_t featureId )
{
    Physics::PhysicsPipelineRecord row;
    row.stage = Physics::PhysicsPipelineStage::SolverIteration;
    row.bodyA = 1;
    row.bodyB = 2;
    row.featureId = featureId;
    return row;
}
} // namespace

TEST_CASE( "Prediction evidence store: empty and Low banks allocate nothing" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    CHECK( banks.Build().PublishedFrameCount() == 0u );
    CHECK( banks.Committed().PublishedFrameCount() == 0u );
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == 0u );

    banks.BeginBuild( 1u, ReplayPredictionDetailMode::Low );
    CHECK_FALSE( banks.ReserveBuild( 1u, 1u, 1u, 10 ) );
    CHECK_FALSE( banks.AppendBuildFrame( 1u, 1u, 1u, {}, {}, 10 ) );
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == 0u );
}

TEST_CASE( "Prediction evidence store: sealed prefix and ranges survive concurrent segment growth" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    const uint64_t epoch = banks.BeginBuild( 7u, ReplayPredictionDetailMode::High );
    const std::array contacts = { ContactRow( 11u ), ContactRow( 12u ) };
    const std::array pipeline = { PipelineRow( 21u ), PipelineRow( 22u ) };
    REQUIRE( banks.AppendBuildFrame( 40u, 3u, 100u, contacts, pipeline, 40 ) );

    const ReplayPredictionSolverEvidenceFrame* first = banks.Build().PublishedFrame( 0u );
    REQUIRE( first != nullptr );
    CHECK( first->complete );
    CHECK( first->identity.bankEpoch == epoch );
    const Physics::PhysicsSolverPersistentContactSample* firstContact = banks.Build().Contact( first->contacts, 0u );
    const Physics::PhysicsPipelineRecord* firstPipeline = banks.Build().Pipeline( first->pipeline, 0u );
    REQUIRE( firstContact != nullptr );
    REQUIRE( firstPipeline != nullptr );

    std::atomic<bool> writerDone { false };
    std::atomic<bool> writerSucceeded { true };
    std::thread writer( [&]
                        {
                            for ( uint32_t index = 0u; index < 300u; ++index )
                            {
                                const std::array nextContact = { ContactRow( 1000u + index ) };
                                const std::array nextPipeline = { PipelineRow( 2000u + index ) };

                                if ( !banks.AppendBuildFrame( 41u + index, 3u, 101u + index, nextContact, nextPipeline,
                                                              static_cast<int>( 41u + index ) ) )
                                {
                                    writerSucceeded.store( false, std::memory_order_release );
                                    break;
                                }
                            }

                            writerDone.store( true, std::memory_order_release );
                        } );

    while ( !writerDone.load( std::memory_order_acquire ) )
    {
        const ReplayPredictionSolverEvidenceFrame* observed = banks.Build().PublishedFrame( 0u );
        REQUIRE( observed != nullptr );
        CHECK( banks.Build().Contact( observed->contacts, 0u ) == firstContact );
        CHECK( banks.Build().Pipeline( observed->pipeline, 0u ) == firstPipeline );
        CHECK( firstContact->featureId == 11u );
        CHECK( firstPipeline->featureId == 21u );
    }

    writer.join();
    REQUIRE( writerSucceeded.load( std::memory_order_acquire ) );
    CHECK( banks.Build().PublishedFrameCount() == 301u );
    CHECK( banks.Build().Contact( first->contacts, 2u ) == nullptr );
    CHECK( banks.Build().Pipeline( first->pipeline, 2u ) == nullptr );
}

TEST_CASE( "Prediction evidence store: initial object and terrain frames retain exact ordered ranges" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    banks.BeginBuild( 12u, ReplayPredictionDetailMode::High );

    // Invariant: an initial frame with no solver rows is still a complete
    // generation-stamped evidence frame, not an absent or partial publication.
    REQUIRE( banks.AppendBuildFrame( 0u, 1u, 10u, {}, {}, 0 ) );
    const ReplayPredictionSolverEvidenceFrame* initial = banks.Build().PublishedFrame( 0u );
    REQUIRE( initial != nullptr );
    CHECK( initial->complete );
    CHECK( initial->contacts.count == 0u );
    CHECK( initial->pipeline.count == 0u );

    std::array contacts = { ContactRow( 31u ), ContactRow( 32u ) };
    contacts[1].bodyB = -1;
    contacts[1].isTerrain = true;
    std::array pipeline = { PipelineRow( 41u ), PipelineRow( 42u ), PipelineRow( 43u ) };
    pipeline[0].stage = Physics::PhysicsPipelineStage::BroadphaseCandidate;
    pipeline[1].stage = Physics::PhysicsPipelineStage::ManifoldRow;
    pipeline[2].stage = Physics::PhysicsPipelineStage::SolverIteration;
    REQUIRE( banks.AppendBuildFrame( 1u, 2u, 11u, contacts, pipeline, 1 ) );

    const ReplayPredictionSolverEvidenceFrame* solved = banks.Build().PublishedFrame( 1u );
    REQUIRE( solved != nullptr );
    CHECK( solved->contacts.begin == 0u );
    CHECK( solved->contacts.count == contacts.size() );
    CHECK( solved->pipeline.begin == 0u );
    CHECK( solved->pipeline.count == pipeline.size() );
    CHECK( banks.Build().Contact( solved->contacts, 0u )->featureId == 31u );
    CHECK( banks.Build().Contact( solved->contacts, 1u )->isTerrain );
    CHECK( banks.Build().Pipeline( solved->pipeline, 0u )->stage == Physics::PhysicsPipelineStage::BroadphaseCandidate );
    CHECK( banks.Build().Pipeline( solved->pipeline, 1u )->stage == Physics::PhysicsPipelineStage::ManifoldRow );
    CHECK( banks.Build().Pipeline( solved->pipeline, 2u )->stage == Physics::PhysicsPipelineStage::SolverIteration );
}

TEST_CASE( "Prediction evidence store: exact identity separates same-frame replacements" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    const uint64_t epoch = banks.BeginBuild( 4u, ReplayPredictionDetailMode::High );
    const std::array firstRows = { ContactRow( 10u ) };
    const std::array replacementRows = { ContactRow( 20u ) };
    REQUIRE( banks.AppendBuildFrame( 88u, 9u, 1000u, firstRows, {}, 88 ) );
    REQUIRE( banks.AppendBuildFrame( 88u, 9u, 1001u, replacementRows, {}, 88 ) );

    const ReplayPredictionEvidenceIdentity oldIdentity = { 4u, ReplayPredictionDetailMode::High, epoch, 88u, 9u, 1000u };
    const ReplayPredictionEvidenceIdentity newIdentity = { 4u, ReplayPredictionDetailMode::High, epoch, 88u, 9u, 1001u };
    const ReplayPredictionSolverEvidenceFrame* oldFrame = banks.Build().FindPublishedFrame( oldIdentity );
    const ReplayPredictionSolverEvidenceFrame* newFrame = banks.Build().FindPublishedFrame( newIdentity );
    REQUIRE( oldFrame != nullptr );
    REQUIRE( newFrame != nullptr );
    CHECK( banks.Build().Contact( oldFrame->contacts, 0u )->featureId == 10u );
    CHECK( banks.Build().Contact( newFrame->contacts, 0u )->featureId == 20u );

    ReplayPredictionEvidenceIdentity stale = oldIdentity;
    stale.bankEpoch = epoch + 1u;
    CHECK( banks.Build().FindPublishedFrame( stale ) == nullptr );
}

TEST_CASE( "Prediction evidence store: promotion cancellation and release preserve bank invariants" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    banks.BeginBuild( 1u, ReplayPredictionDetailMode::High );
    const std::array contacts = { ContactRow( 1u ) };
    const std::array pipeline = { PipelineRow( 2u ) };
    REQUIRE( banks.AppendBuildFrame( 1u, 1u, 1u, contacts, pipeline, 1 ) );
    REQUIRE( banks.PromoteBuild() );
    CHECK( banks.Committed().Generation() == 1u );
    CHECK( banks.Committed().PublishedFrameCount() == 1u );

    banks.BeginBuild( 2u, ReplayPredictionDetailMode::High );
    REQUIRE( banks.AppendBuildFrame( 2u, 2u, 2u, contacts, pipeline, 2 ) );
    ReplayPredictionSolverEvidenceBanksMemoryStats coexist = banks.CollectMemoryStats();
    CHECK( coexist.build.publishedFrameCount == 1u );
    CHECK( coexist.committed.publishedFrameCount == 1u );
    CHECK( coexist.currentCapacityBytes > 0u );

    banks.CancelBuild();
    CHECK( banks.Build().PublishedFrameCount() == 0u );
    CHECK( banks.Committed().PublishedFrameCount() == 1u );
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == coexist.currentCapacityBytes );

    REQUIRE_FALSE( banks.PromoteBuild() );
    banks.BeginBuild( 3u, ReplayPredictionDetailMode::High );
    REQUIRE( banks.AppendBuildFrame( 3u, 3u, 3u, contacts, pipeline, 3 ) );
    REQUIRE( banks.PromoteBuild() );
    CHECK( banks.Committed().Generation() == 3u );

    const uint64_t peak = banks.CollectMemoryStats().lifetimePeakCapacityBytes;
    REQUIRE( peak > 0u );
    banks.ReleaseCapacity();
    ReplayPredictionSolverEvidenceBanksMemoryStats released = banks.CollectMemoryStats();
    CHECK( released.currentCapacityBytes == 0u );
    CHECK( released.currentContactCapacityBytes == 0u );
    CHECK( released.currentPipelineCapacityBytes == 0u );
    CHECK( released.currentFrameCapacityBytes == 0u );
    CHECK( released.lifetimePeakCapacityBytes == peak );
    CHECK( released.releaseCheckpointCount == 1u );
    CHECK( released.lastReleaseBeforeCapacityBytes == coexist.currentCapacityBytes );
    CHECK( released.lastReleaseAfterCapacityBytes == 0u );
    banks.ReleaseCapacity();
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == 0u );
    CHECK( banks.CollectMemoryStats().lifetimePeakCapacityBytes == peak );
    CHECK( banks.CollectMemoryStats().releaseCheckpointCount == 2u );
}

TEST_CASE( "Prediction evidence store: overflow and hard-cap requests fail without publication" )
{
    ReplayPredictionSolverEvidenceBanks banks;
    banks.BeginBuild( 9u, ReplayPredictionDetailMode::High );
    CHECK_FALSE( banks.ReserveBuild( ( std::numeric_limits<std::size_t>::max )(), 0u, 0u, 9 ) );
    CHECK_FALSE( banks.ReserveBuild( 1u, 0u, ( std::numeric_limits<std::size_t>::max )(), 9 ) );
    CHECK( banks.Build().PublishedFrameCount() == 0u );
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == 0u );

    const std::size_t overBankCapPipelineRows = static_cast<std::size_t>( REPLAY_PREDICTION_EVIDENCE_BANK_HARD_BYTES /
                                                                          sizeof( Physics::PhysicsPipelineRecord ) ) +
                                                REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY;
    CHECK_FALSE( banks.ReserveBuild( 1u, 0u, overBankCapPipelineRows, 9 ) );
    CHECK( banks.CollectMemoryStats().currentCapacityBytes == 0u );

    REQUIRE( banks.ReserveBuild( 1u, REPLAY_PREDICTION_EVIDENCE_CONTACT_SEGMENT_CAPACITY + 1u,
                                 REPLAY_PREDICTION_EVIDENCE_PIPELINE_SEGMENT_CAPACITY + 1u, 9 ) );
    const ReplayPredictionSolverEvidenceBanksMemoryStats reserved = banks.CollectMemoryStats();
    CHECK( reserved.currentContactCapacityBytes > 0u );
    CHECK( reserved.currentPipelineCapacityBytes > 0u );
    CHECK( reserved.build.contactCount == 0u );
    CHECK( reserved.build.pipelineCount == 0u );
    CHECK( reserved.build.publishedFrameCount == 0u );
}
