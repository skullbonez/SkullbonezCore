//
// File: SkullbonezTests/TestReplayCauseInspection.cpp
// Purpose:
//   Pin exact-frame eligibility for the replay planning causal inspection surface.
//
// Summary:
//   Recorded and predicted cause rows use different bounded timeline banks.
//   These tests prove each row kind either resolves its exact frame or emits the
//   stable expired-frame refusal without consulting transient solver detail.
//   Exact-frame detail tests then prove manifold-row grouping, stage joins, and
//   distinct expired-versus-unavailable outcomes without retaining source data.
//   Presentation tests pin event-frame body poses, exact ManifoldRow points,
//   derived surviving-row points, owned packet publication, solver-row copy
//   lifetime, and the fixed four-row scrolling viewport.
//   The Planning transition tests also pin request coalescing, pause ownership,
//   Space aftermath, total-elapsed cubic easing, symmetric discrete frame
//   rounding, and saved-camera return policy without host owners.
//
// Invariants:
//   - Retained-window boundaries are inclusive at the oldest frame and exclusive at the live edge.
//   - Prediction rows require an exact published frame, including terrain-independent contact rows.
//   - Missing pipeline detail never disables transport for a retained frame.
//   - A current or coincident diagnostics row never substitutes for a mismatched frame stamp or row index.
//   - Detached contact packets use exact retained points when present and derive
//     only surviving rows when an exact point record is unavailable.
//   - Forward and reverse transport are monotonic and land on the exact target.
//
// Related:
//   - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
//   - SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRecorder.h"

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

TEST_CASE( "Replay cause solver detail: exact frame publishes every row and matching pipeline stage" )
{
    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::Manifold;
    row.firstFrame = 84u;
    row.modelRow.value = 3;
    row.counterpartModelRow.value = 7;
    row.contactIndex = 1;
    row.featureId = 101;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 84u;

    std::array<SkullbonezCore::Physics::PhysicsSolverPersistentContactSample, 4> contacts;
    contacts[0].bodyA = 1;
    contacts[0].bodyB = 2;
    contacts[0].featureId = 10u;
    contacts[1].bodyA = 3;
    contacts[1].bodyB = 7;
    contacts[1].featureId = 101u;
    contacts[2].bodyA = 7;
    contacts[2].bodyB = 3;
    contacts[2].featureId = 102u;
    contacts[3].bodyA = 3;
    contacts[3].bodyB = 8;
    contacts[3].featureId = 103u;

    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    std::array<PhysicsPipelineRecord, 9> records;
    records[0] = { .stage = PhysicsPipelineStage::ManifoldRow, .bodyA = 3, .bodyB = 7, .featureId = 101u };
    records[1] = { .stage = PhysicsPipelineStage::WarmStart, .bodyA = 7, .bodyB = 3, .featureId = 102u };
    records[2] = { .stage = PhysicsPipelineStage::SolverIteration, .bodyA = 3, .bodyB = 7, .featureId = 101u };
    records[3] = { .stage = PhysicsPipelineStage::VelocityWriteback, .bodyA = 3 };
    records[4] = { .stage = PhysicsPipelineStage::VelocityWriteback, .bodyA = 7 };
    records[5] = { .stage = PhysicsPipelineStage::PositionCorrection, .bodyA = 3, .bodyB = 7, .featureId = 102u };
    records[6] = { .stage = PhysicsPipelineStage::CacheStore, .bodyA = 3, .bodyB = 7, .featureId = 101u };
    records[7] = { .stage = PhysicsPipelineStage::SolverIteration, .bodyA = 3, .bodyB = 8, .featureId = 103u };
    records[8] = { .stage = PhysicsPipelineStage::BroadphaseCandidate, .bodyA = 3, .bodyB = 7, .featureId = 101u };

    const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, records } );
    REQUIRE( detail.HasDetail() );
    CHECK( detail.availability == ReplayCauseSolverDetailAvailability::Available );
    CHECK( detail.contactRowCount == 2u );
    CHECK( detail.pipelineRecordCount == 7u );
    REQUIRE( detail.ContactRowAt( 0u ) );
    REQUIRE( detail.ContactRowAt( 1u ) );
    CHECK( detail.ContactRowAt( 0u )->featureId == 101u );
    CHECK( detail.ContactRowAt( 1u )->featureId == 102u );
    CHECK( detail.ContactRowAt( 2u ) == nullptr );
    CHECK( detail.PipelineRecordAt( 6u )->stage == PhysicsPipelineStage::CacheStore );
    CHECK( detail.PipelineRecordAt( 7u ) == nullptr );
    CHECK( std::strcmp( detail.Feedback(), "" ) == 0 );

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 4, seek, 88u, false, 1.0 ) );
    inspection.PublishSolverDetail( inspection.View().generation, detail );
    CHECK( inspection.View().solverDetailAvailability == ReplayCauseSolverDetailAvailability::Available );
    CHECK( inspection.View().solverDetailContactRowCount == 2u );
    CHECK( inspection.View().solverDetailPipelineRecordCount == 7u );
}

TEST_CASE( "Replay cause solver detail: unavailable states never substitute diagnostics" )
{
    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::SolverRow;
    row.firstFrame = 84u;
    row.modelRow.value = 3;
    row.counterpartModelRow.value = 7;
    row.contactIndex = 0;
    row.featureId = 101;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 84u;

    SkullbonezCore::Physics::PhysicsSolverPersistentContactSample contact;
    contact.bodyA = 3;
    contact.bodyB = 7;
    contact.featureId = 101u;
    const std::array contacts = { contact };

    SUBCASE( "absent selected row" )
    {
        const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, {}, {} } );
        CHECK_FALSE( detail.HasDetail() );
        CHECK( detail.availability == ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable );
        CHECK( std::strcmp( detail.Feedback(), "Solver detail not available" ) == 0 );
    }

    SUBCASE( "overwritten diagnostics carry a different frame stamp" )
    {
        const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 85u, contacts, {} } );
        CHECK_FALSE( detail.HasDetail() );
        CHECK( detail.availability == ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable );
        CHECK( detail.contactRowCount == 0u );
    }

    SUBCASE( "coincident row at the wrong index is not searched by feature" )
    {
        row.contactIndex = 1;
        const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, {} } );
        CHECK_FALSE( detail.HasDetail() );
    }

    SUBCASE( "expired transport remains distinct from retained detail absence" )
    {
        seek.availability = ReplayCauseSeekAvailability::ReplayFrameExpired;
        const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, {} } );
        CHECK( detail.availability == ReplayCauseSolverDetailAvailability::ReplayFrameExpired );
        CHECK( std::strcmp( detail.Feedback(), "Replay frame expired" ) == 0 );
    }

    SUBCASE( "prediction contact has no retained solver diagnostics" )
    {
        row.prediction = true;
        seek.source = ReplayCauseSeekSource::Prediction;
        const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, {} } );
        CHECK( detail.availability == ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable );
    }
}

TEST_CASE( "Replay cause solver panel: copied rows survive restore sources and scroll four at a time" )
{
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::Manifold;
    row.firstFrame = 84u;
    row.modelRow.value = 3;
    row.counterpartModelRow.value = 7;
    row.contactIndex = 0;
    row.featureId = 100;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 84u;

    std::array<PhysicsSolverPersistentContactSample, 6> contacts;
    std::array<PhysicsPipelineRecord, 6> records;

    for ( std::size_t index = 0; index < contacts.size(); ++index )
    {
        contacts[index].bodyA = 3;
        contacts[index].bodyB = 7;
        contacts[index].featureId = 100u + static_cast<uint32_t>( index );
        contacts[index].accN = 1.0f + static_cast<float>( index );
        records[index].stage = PhysicsPipelineStage::SolverIteration;
        records[index].bodyA = 3;
        records[index].bodyB = 7;
        records[index].featureId = contacts[index].featureId;
        records[index].iteration = 0;
        records[index].scalarA = 0.25f;
    }

    const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, records } );
    REQUIRE( detail.HasDetail() );
    REQUIRE( detail.contactRowCount == 6u );

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 2, seek, 88u, false, 1.0 ) );
    inspection.Advance( 2.5 );
    ReplayCauseTransportRequest request;
    REQUIRE( inspection.TakeTransportRequest( request ) );
    inspection.PublishSolverDetail( request.generation, detail );

    contacts[0].accN = 99.0f;
    records[0].scalarA = 88.0f;
    inspection.CompleteTransport( request.generation, true );
    const ReplayCauseInspectionView published = inspection.View();
    REQUIRE( published.detailVisible );
    REQUIRE( published.solverDetailContacts.size() == 6u );
    REQUIRE( published.solverDetailPipelineRecords.size() == 6u );
    CHECK( published.solverDetailContacts[0].accN == doctest::Approx( 1.0f ) );
    CHECK( published.solverDetailPipelineRecords[0].scalarA == doctest::Approx( 0.25f ) );
    CHECK( ReplayCauseSolverDetailIterationCount( published, 0u ) == 1 );

    RunReplayCauseTreeState causeTree;
    causeTree.hasWindowPlacement = true;
    causeTree.x = 1500;
    causeTree.y = 100;
    causeTree.width = 380;
    causeTree.height = 420;
    const ReplayCauseSolverPanelLayout layout = BuildReplayCauseSolverPanelLayout( published, causeTree, 1920, 1080 );
    CHECK( layout.content.h == doctest::Approx( layout.rowHeight * 4.0f ) );

    const int panelX = static_cast<int>( layout.panel.x + 20.0f );
    const int panelY = static_cast<int>( layout.panel.y + 20.0f );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 1 );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 2 );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 2 );
    CHECK_FALSE( inspection.TickSolverDetailPanelInput( causeTree, 1919, 1079, true, false, -120, 1920, 1080 ) );
}

TEST_CASE( "Replay cause manifold presentation: exact records and event poses form one owned packet" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::Manifold;
    row.firstFrame = 84u;
    row.modelRow.value = 3;
    row.counterpartModelRow.value = 7;
    row.contactIndex = 0;
    row.featureId = 101;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 84u;

    std::array<PhysicsSolverPersistentContactSample, 2> contacts;
    contacts[0].bodyA = 3;
    contacts[0].bodyB = 7;
    contacts[0].featureId = 101u;
    contacts[0].rA = Vector3( 0.5f, 0.0f, 0.0f );
    contacts[0].normal = Vector3( 0.0f, 1.0f, 0.0f );
    contacts[0].tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
    contacts[0].tangent2 = Vector3( 0.0f, 0.0f, 1.0f );
    contacts[0].penetration = 0.25f;
    contacts[0].manifoldPointCount = 2u;
    contacts[1] = contacts[0];
    contacts[1].bodyA = 7;
    contacts[1].bodyB = 3;
    contacts[1].featureId = 102u;
    contacts[1].rA = Vector3( -0.25f, 0.5f, 0.0f );
    contacts[1].penetration = 0.5f;

    PhysicsPipelineRecord exactPoint;
    exactPoint.stage = PhysicsPipelineStage::ManifoldRow;
    exactPoint.bodyA = 3;
    exactPoint.bodyB = 7;
    exactPoint.featureId = 101u;
    exactPoint.point = Vector3( 4.0f, 5.0f, 6.0f );
    exactPoint.normal = Vector3( 0.0f, 0.0f, 1.0f );
    exactPoint.scalarA = 0.125f;
    const std::array records = { exactPoint };

    const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, records } );
    REQUIRE( detail.HasDetail() );

    ReplaySolverFrameSample sample;
    sample.frameIndex = 84u;
    sample.bodies.resize( 2u );
    sample.bodies[0].modelRow.value = 3;
    sample.bodies[0].position = Vector3( 10.0f, 20.0f, 30.0f );
    sample.bodies[1].modelRow.value = 7;
    sample.bodies[1].position = Vector3( 40.0f, 50.0f, 60.0f );

    const SkullbonezCore::Rendering::ContactManifoldPresentation presentation =
        BuildReplayCauseContactPresentation( detail, sample );
    REQUIRE( presentation.HasGeometry() );
    CHECK( presentation.bodyCount == 2u );
    CHECK( presentation.pointCount == 2u );
    CHECK( presentation.bodies[0].position.x == doctest::Approx( 10.0f ) );
    CHECK( presentation.bodies[1].position.x == doctest::Approx( 40.0f ) );
    CHECK( presentation.points[0].exactSourcePoint );
    CHECK( presentation.points[0].point.x == doctest::Approx( 4.0f ) );
    CHECK( presentation.points[0].normal.z == doctest::Approx( 1.0f ) );
    CHECK( presentation.points[0].penetration == doctest::Approx( 0.125f ) );
    CHECK_FALSE( presentation.points[1].exactSourcePoint );
    CHECK( presentation.points[1].point.x == doctest::Approx( 39.75f ) );
    CHECK( presentation.points[1].point.y == doctest::Approx( 50.5f ) );

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 4, seek, 88u, false, 1.0 ) );
    inspection.PublishSolverDetail( inspection.View().generation, detail, presentation );
    CHECK( inspection.View().contactPresentation.pointCount == 2u );
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
