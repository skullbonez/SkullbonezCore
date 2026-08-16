//
// File: SkullbonezTests/TestReplayCauseInspection.cpp
// Purpose:
//   Pin exact-frame eligibility for the replay planning causal inspection surface.
//
// Summary:
//   Recorded and predicted cause rows use different bounded timeline banks.
//   These tests prove each row kind either resolves its exact frame or emits the
//   stable expired-frame refusal without consulting transient solver detail.
//   The Planning transition tests also pin request coalescing, pause ownership,
//   Space aftermath, total-elapsed cubic easing, symmetric discrete frame
//   rounding, and saved-camera return policy without host owners.
//
// Invariants:
//   - Retained-window boundaries are inclusive at the oldest frame and exclusive at the live edge.
//   - Prediction rows require an exact published frame, including terrain-independent contact rows.
//   - Missing pipeline detail never disables transport for a retained frame.
//   - Forward and reverse transport are monotonic and land on the exact target.
//
// Related:
//   - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
//   - SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h"

#include <array>
#include <cstring>

using namespace SkullbonezCore::Runtime;

namespace
{
ReplayRecorderStats RetainedSolverWindow()
{
    ReplayRecorderStats stats;
    stats.enabled = true;
    stats.nextFrameIndex = 90u;
    stats.sampleCount = 10u;
    stats.sampleCapacity = 10u;
    return stats;
}
} // namespace

TEST_CASE( "Replay cause inspection: recorded row kinds keep exact retained frame eligibility" )
{
    constexpr std::array kinds = { RunReplayCauseTreeRowKind::Body, RunReplayCauseTreeRowKind::Manifold,
                                   RunReplayCauseTreeRowKind::SolverRow };
    const ReplayRecorderStats stats = RetainedSolverWindow();

    for ( RunReplayCauseTreeRowKind kind : kinds )
    {
        RunReplayCauseTreeRow row;
        row.kind = kind;
        row.firstFrame = 80u;
        row.terrain = kind != RunReplayCauseTreeRowKind::Body;
        row.pipelineIndex = -1;

        const ReplayCauseSeekResult result = EvaluateReplayCauseSeek( row, stats, {} );
        CHECK( result.CanTransport() );
        CHECK( result.availability == ReplayCauseSeekAvailability::Available );
        CHECK( result.source == ReplayCauseSeekSource::SolverHistory );
        CHECK( result.frame == 80u );
        CHECK( std::strcmp( result.Feedback(), "" ) == 0 );
    }
}

TEST_CASE( "Replay cause inspection: expired recorded rows refuse instead of clamping" )
{
    const ReplayRecorderStats stats = RetainedSolverWindow();
    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::SolverRow;
    row.firstFrame = 79u;

    ReplayCauseSeekResult result = EvaluateReplayCauseSeek( row, stats, {} );
    CHECK_FALSE( result.CanTransport() );
    CHECK( result.frame == 79u );
    CHECK( std::strcmp( result.Feedback(), "Replay frame expired" ) == 0 );

    row.firstFrame = 90u;
    result = EvaluateReplayCauseSeek( row, stats, {} );
    CHECK_FALSE( result.CanTransport() );
    CHECK( result.frame == 90u );
    CHECK( std::strcmp( result.Feedback(), "Replay frame expired" ) == 0 );
}

TEST_CASE( "Replay cause inspection: prediction row kinds require an exact published frame" )
{
    std::array<RunReplayPredictionFrame, 3> frames;
    frames[0].frameIndex = 40u;
    frames[1].frameIndex = 41u;
    frames[2].frameIndex = 42u;

    constexpr std::array kinds = { RunReplayCauseTreeRowKind::Body, RunReplayCauseTreeRowKind::PredictionContact,
                                   RunReplayCauseTreeRowKind::PredictionMotion };

    for ( RunReplayCauseTreeRowKind kind : kinds )
    {
        RunReplayCauseTreeRow row;
        row.kind = kind;
        row.firstFrame = 41u;
        row.prediction = true;

        ReplayCauseSeekResult result = EvaluateReplayCauseSeek( row, {}, frames );
        CHECK( result.CanTransport() );
        CHECK( result.source == ReplayCauseSeekSource::Prediction );
        CHECK( result.frame == 41u );

        row.firstFrame = 43u;
        result = EvaluateReplayCauseSeek( row, {}, frames );
        CHECK_FALSE( result.CanTransport() );
        CHECK( result.frame == 43u );
        CHECK( std::strcmp( result.Feedback(), "Replay frame expired" ) == 0 );
    }
}

TEST_CASE( "Replay cause inspection: newest selection coalesces behind one in-flight restore" )
{
    ReplayCauseInspection inspection;
    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 80u;

    REQUIRE( inspection.Select( 1, seek, 89u, false, 10.0 ) );
    inspection.Advance( 10.5 );
    ReplayCauseTransportRequest first;
    REQUIRE( inspection.TakeTransportRequest( first ) );
    CHECK( first.sourceFrame == 89u );
    CHECK( first.targetFrame == 83u );

    seek.frame = 85u;
    REQUIRE( inspection.Select( 2, seek, 84u, true, 10.5 ) );
    inspection.Advance( 12.0 );
    ReplayCauseTransportRequest blocked;
    CHECK_FALSE( inspection.TakeTransportRequest( blocked ) );

    inspection.CompleteTransport( first.generation, true );
    const ReplayCauseInspectionView waiting = inspection.View();
    CHECK( waiting.mode == ReplayCauseInspectionMode::Transporting );
    CHECK( waiting.transportPending );
    CHECK( waiting.selectedRow == 2 );

    ReplayCauseTransportRequest newest;
    REQUIRE( inspection.TakeTransportRequest( newest ) );
    CHECK( newest.generation > first.generation );
    CHECK( newest.sourceFrame == 84u );
    CHECK( newest.targetFrame == 85u );

    inspection.CompleteTransport( first.generation, false );
    CHECK( inspection.View().mode == ReplayCauseInspectionMode::Transporting );
    inspection.CompleteTransport( newest.generation, true );
    CHECK( inspection.View().mode == ReplayCauseInspectionMode::DetailPaused );
    CHECK( inspection.View().detailVisible );
}

TEST_CASE( "Replay cause inspection: pause ownership survives pre-pause, Space, failure, and return" )
{
    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.frame = 12u;

    SUBCASE( "inspection-owned pause is released to Space aftermath" )
    {
        ReplayCauseInspection inspection;
        REQUIRE( inspection.Select( 0, seek, 20u, false, 1.0 ) );
        inspection.Advance( 2.5 );
        ReplayCauseTransportRequest request;
        REQUIRE( inspection.TakeTransportRequest( request ) );
        inspection.CompleteTransport( request.generation, true );

        bool releasePause = false;
        REQUIRE( inspection.BeginAftermath( releasePause ) );
        CHECK( releasePause );
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::AftermathFollow );
        CHECK_FALSE( inspection.View().detailVisible );

        // Direct retargeting from aftermath reacquires only the pause released
        // above and keeps the same bounded transition owner.
        ReplayCauseSeekResult retarget = seek;
        retarget.frame = 15u;
        REQUIRE( inspection.Select( 1, retarget, 13u, false, 3.0 ) );
        inspection.Advance( 4.5 );
        CHECK( inspection.View().ownsPause );
        ReplayCauseTransportRequest retargetRequest;
        REQUIRE( inspection.TakeTransportRequest( retargetRequest ) );
        inspection.CompleteTransport( retargetRequest.generation, true );

        const ReplayCauseExitAction exit = inspection.BeginReturn();
        CHECK( exit.apply );
        CHECK( exit.releasePause );
        inspection.CompleteReturn();
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Inactive );
    }

    SUBCASE( "click or scrub return from aftermath does not release an external pause" )
    {
        ReplayCauseInspection inspection;
        REQUIRE( inspection.Select( 0, seek, 20u, true, 1.0 ) );
        inspection.Advance( 2.5 );
        ReplayCauseTransportRequest request;
        REQUIRE( inspection.TakeTransportRequest( request ) );
        inspection.CompleteTransport( request.generation, true );

        bool releasePause = true;
        REQUIRE( inspection.BeginAftermath( releasePause ) );
        CHECK_FALSE( releasePause );
        const ReplayCauseExitAction exit = inspection.BeginReturn();
        CHECK( exit.apply );
        CHECK_FALSE( exit.releasePause );
    }

    SUBCASE( "operator-owned pre-pause is never released by inspection failure" )
    {
        ReplayCauseInspection inspection;
        REQUIRE( inspection.Select( 0, seek, 20u, true, 1.0 ) );
        inspection.Advance( 2.5 );
        ReplayCauseTransportRequest request;
        REQUIRE( inspection.TakeTransportRequest( request ) );
        inspection.CompleteTransport( request.generation, false );
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Returning );
        CHECK_FALSE( inspection.View().ownsPause );

        inspection.CompleteReturn();
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Inactive );
    }
}

TEST_CASE( "Replay cause inspection: cancellation invalidates an interrupted transport completion" )
{
    ReplayCauseInspection inspection;
    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::Prediction;
    seek.frame = 45u;
    REQUIRE( inspection.Select( 3, seek, 40u, false, 1.0 ) );
    inspection.Advance( 1.5 );

    ReplayCauseTransportRequest request;
    REQUIRE( inspection.TakeTransportRequest( request ) );
    const ReplayCauseExitAction exit = inspection.BeginReturn();
    REQUIRE( exit.apply );
    CHECK( exit.releasePause );

    inspection.CompleteTransport( request.generation, true );
    CHECK( inspection.View().mode == ReplayCauseInspectionMode::Returning );
    CHECK_FALSE( inspection.View().detailVisible );
}

TEST_CASE( "Replay cause inspection: elapsed curve is cadence independent and completes exactly" )
{
    constexpr std::array<double, 4> cadences = { 1.0 / 30.0, 1.0 / 60.0, 1.0 / 120.0, 0.007 };
    const float reference = EvaluateReplayCauseTransitionProgress( 0.75 );

    for ( double cadence : cadences )
    {
        double elapsed = 0.0;

        while ( elapsed + cadence < 0.75 )
        {
            elapsed += cadence;
        }

        // The owner samples total wall-clock elapsed, so render cadence does
        // not enter the curve evaluation even when the last interval is partial.
        elapsed = 0.75;
        CHECK( EvaluateReplayCauseTransitionProgress( elapsed ) == doctest::Approx( reference ) );
    }

    CHECK( EvaluateReplayCauseTransitionProgress( 0.0 ) == 0.0f );
    CHECK( EvaluateReplayCauseTransitionProgress( 1.5 ) == 1.0f );
    CHECK( EvaluateReplayCauseTransitionProgress( 4.0 ) == 1.0f );
}

TEST_CASE( "Replay cause inspection: forward and reverse frame rounding stay monotonic" )
{
    constexpr std::array<float, 7> progress = { 0.0f, 0.01f, 0.24f, 0.5f, 0.76f, 0.99f, 1.0f };
    ReplayFrameIndex previousForward = 10u;
    ReplayFrameIndex previousReverse = 20u;

    for ( float sample : progress )
    {
        const ReplayFrameIndex forward = EvaluateReplayCauseTransitionFrame( 10u, 20u, sample );
        const ReplayFrameIndex reverse = EvaluateReplayCauseTransitionFrame( 20u, 10u, sample );
        CHECK( forward >= previousForward );
        CHECK( reverse <= previousReverse );
        previousForward = forward;
        previousReverse = reverse;
    }

    CHECK( previousForward == 20u );
    CHECK( previousReverse == 10u );
}
