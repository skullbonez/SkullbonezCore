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
//   lifetime, the compact unavailable state, and the up-to-four-row scrolling
//   viewport.
//   The UI projection test pins exact solver-value text, units, and signs.
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
//   - Retarget, aftermath, return, failure, and reset clear the panel and
//     manifold packet through one paired visibility edge.
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
        CHECK( std::strcmp( detail.Feedback(), "Solver detail not available" ) == 0 );
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
    CHECK( layout.panel.w == doctest::Approx( 520.0f ) );
    CHECK( layout.visibleRows == 4 );
    CHECK( layout.content.h == doctest::Approx( layout.rowHeight * 4.0f ) );

    // The panel has no second placement state: every cause-window drag or
    // resize moves the adjacent surface through this same projection.
    causeTree.x = 120;
    causeTree.y = 180;
    causeTree.width = 460;
    const ReplayCauseSolverPanelLayout moved = BuildReplayCauseSolverPanelLayout( published, causeTree, 1920, 1080 );
    CHECK( moved.panel.x == doctest::Approx( 590.0f ) );
    CHECK( moved.panel.y == doctest::Approx( 180.0f ) );

    ReplayCauseInspectionView unavailable = published;
    unavailable.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    unavailable.solverDetailContacts = {};
    unavailable.solverDetailPipelineRecords = {};
    causeTree.x = 1114;
    causeTree.y = 28;
    causeTree.width = 380;
    const ReplayCauseSolverPanelLayout compactUnavailable =
        BuildReplayCauseSolverPanelLayout( unavailable, causeTree, 1125, 541 );
    CHECK( compactUnavailable.panel.w == doctest::Approx( REPLAY_CAUSE_SOLVER_PANEL_WIDTH ) );
    CHECK( compactUnavailable.panel.h < 200.0f );
    CHECK( compactUnavailable.panel.w < 1125.0f * 0.5f );
    CHECK( compactUnavailable.content.h == doctest::Approx( REPLAY_CAUSE_SOLVER_PANEL_EMPTY_HEIGHT ) );
    CHECK( compactUnavailable.visibleRows == 0 );
    CHECK( REPLAY_CAUSE_SOLVER_PANEL_OPACITY == doctest::Approx( 0.78f ) );

    causeTree.x = 120;
    causeTree.y = 180;
    causeTree.width = 460;
    const int panelX = static_cast<int>( moved.panel.x + 20.0f );
    const int panelY = static_cast<int>( moved.panel.y + 20.0f );
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
    using SkullbonezCore::Rendering::ContactManifoldPresentation;

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

    const ContactManifoldPresentation presentation = BuildReplayCauseContactPresentation( detail, sample );
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

TEST_CASE( "Contact manifold presentation: capacity publishes a truthful truncated prefix" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    std::array<PhysicsSolverPersistentContactSample, 9> contacts;
    for ( std::size_t index = 0; index < contacts.size(); ++index )
    {
        contacts[index].bodyA = 3;
        contacts[index].bodyB = 7;
        contacts[index].featureId = static_cast<uint32_t>( 100u + index );
        contacts[index].rA = Vector3( static_cast<float>( index ), 0.0f, 0.0f );
        contacts[index].normal = Vector3( 0.0f, 1.0f, 0.0f );
    }

    ReplayCauseSolverDetailResult detail;
    detail.frame = 84u;
    detail.availability = ReplayCauseSolverDetailAvailability::Available;
    detail.sourceContacts = contacts;
    detail.bodyA = 3;
    detail.bodyB = 7;
    detail.contactRowCount = contacts.size();

    ReplaySolverFrameSample sample;
    sample.frameIndex = 84u;
    sample.bodies.resize( 2u );
    sample.bodies[0].modelRow.value = 3;
    sample.bodies[0].position = Vector3( 10.0f, 0.0f, 0.0f );
    sample.bodies[1].modelRow.value = 7;
    sample.bodies[1].position = Vector3( 20.0f, 0.0f, 0.0f );

    const auto presentation = BuildReplayCauseContactPresentation( detail, sample );
    CHECK( presentation.pointCount == SkullbonezCore::Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY );
    CHECK( presentation.truncated );
    CHECK( presentation.points[7].point.x == doctest::Approx( 17.0f ) );

    detail.contactRowCount = SkullbonezCore::Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY;
    CHECK_FALSE( BuildReplayCauseContactPresentation( detail, sample ).truncated );
}

TEST_CASE( "Replay solver panel: value mapping includes exact values units and sign conventions" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;
    PhysicsSolverPersistentContactSample contact;
    contact.bodyA = 3;
    contact.bodyB = 7;
    contact.featureId = 101u;
    contact.normal = Vector3( 0.1f, 0.2f, 0.3f );
    contact.tangent1 = Vector3( 0.4f, 0.5f, 0.6f );
    contact.tangent2 = Vector3( 0.7f, 0.8f, 0.9f );
    contact.rA = Vector3( 1.25f, 2.5f, 3.75f );
    contact.rB = Vector3( 4.0f, 5.0f, 6.0f );
    contact.penetration = 0.125f;
    contact.normalMass = 2.0f;
    contact.tangentMass1 = 2.25f;
    contact.tangentMass2 = 2.5f;
    contact.bias = 2.75f;
    contact.frictionLimit = 3.0f;
    contact.accN = 3.5f;
    contact.accT1 = -3.75f;
    contact.accT2 = 4.0f;
    contact.warmStarted = true;
    const std::array contacts = { contact };
    std::array<SkullbonezCore::Physics::PhysicsPipelineRecord, 2> pipelineRecords;
    pipelineRecords[0].stage = SkullbonezCore::Physics::PhysicsPipelineStage::WarmStart;
    pipelineRecords[0].featureId = contact.featureId;
    pipelineRecords[0].scalarB = 11.25f;
    pipelineRecords[1] = pipelineRecords[0];
    pipelineRecords[1].scalarB = 99.0f;

    ReplayCauseInspectionView inspection;
    inspection.detailVisible = true;
    inspection.targetFrame = 84u;
    inspection.solverDetailAvailability = ReplayCauseSolverDetailAvailability::Available;
    inspection.solverDetailContacts = contacts;
    inspection.solverDetailPipelineRecords = pipelineRecords;
    inspection.contactPresentation.pointCount = 1u;
    inspection.contactPresentation.points[0].point = Vector3( 7.0f, 8.0f, 9.0f );
    const ReplayCauseSolverPanelRowText values = BuildReplayCauseSolverPanelRowText( inspection, 0 );

    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_UNITS,
                        "UNITS: vectors/penetration/correction = scene units; bias/linear writeback = u/s;" ) == 0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_UNITS_MORE,
                        "angular = rad/s; impulses = mass*u/s; effective masses = mass." ) == 0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_SIGNS,
                        "SIGNS: +penetration = overlap; normal/t1/t2 = world-space;" ) == 0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_SIGNS_MORE,
                        "signed accT1/accT2 follow t1/t2; CLAMP = frictionLimit reached." ) == 0 );
    CHECK( std::strcmp( values.headline, "ROW 0  FEATURE 101  BODIES 3 / 7  POINT (7.0000, 8.0000, 9.0000)" ) == 0 );
    CHECK( std::strcmp( values.basis,
                        "n (0.1000 0.2000 0.3000)  t1 (0.4000 0.5000 0.6000)  t2 (0.7000 0.8000 0.9000)" ) == 0 );
    CHECK( std::strcmp( values.geometry,
                        "rA (1.2500 2.5000 3.7500)  rB (4.0000 5.0000 6.0000)  penetration 0.12500" ) == 0 );
    CHECK( std::strcmp( values.masses,
                        "normalMass 2.00000  tangentMass (2.25000, 2.50000)  bias 2.75000  frictionLimit 3.00000" ) ==
           0 );
    CHECK( std::strcmp( values.impulses,
                        "accN 3.50000  accT1 -3.75000  accT2 4.00000  warm-start YES  previous normal impulse 11.25000" ) ==
           0 );
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

TEST_CASE( "Replay cause inspection: focused panel and manifold clear together across every lifecycle edge" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 12u;

    const auto publishFocusedSurface = [&]( ReplayCauseInspection& inspection )
    {
        std::array<PhysicsSolverPersistentContactSample, 1> contacts;
        contacts[0].bodyA = 1;
        contacts[0].bodyB = 2;
        contacts[0].featureId = 9u;
        ReplayCauseSolverDetailResult detail;
        detail.frame = 12u;
        detail.availability = ReplayCauseSolverDetailAvailability::Available;
        detail.sourceContacts = contacts;
        detail.bodyA = 1;
        detail.bodyB = 2;
        detail.contactRowCount = 1u;
        SkullbonezCore::Rendering::ContactManifoldPresentation presentation;
        presentation.pointCount = 1u;
        presentation.points[0].point = Vector3( 1.0f, 2.0f, 3.0f );

        REQUIRE( inspection.Select( 0, seek, 20u, false, 1.0 ) );
        inspection.Advance( 2.5 );
        ReplayCauseTransportRequest request;
        REQUIRE( inspection.TakeTransportRequest( request ) );
        inspection.PublishSolverDetail( request.generation, detail, presentation );
        inspection.CompleteTransport( request.generation, true );
        REQUIRE( inspection.View().detailVisible );
        REQUIRE( inspection.View().solverDetailContacts.size() == 1u );
        REQUIRE( inspection.View().contactPresentation.HasGeometry() );
    };
    const auto checkSurfaceCleared = []( const ReplayCauseInspectionView& view )
    {
        CHECK_FALSE( view.detailVisible );
        CHECK( view.solverDetailContacts.empty() );
        CHECK( view.solverDetailPipelineRecords.empty() );
        CHECK_FALSE( view.contactPresentation.HasGeometry() );
        CHECK( view.solverDetailFirstRow == 0 );
    };

    SUBCASE( "Space aftermath drops every visible value while camera follow state survives" )
    {
        ReplayCauseInspection inspection;
        publishFocusedSurface( inspection );
        bool releasePause = false;
        REQUIRE( inspection.BeginAftermath( releasePause ) );
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::AftermathFollow );
        checkSurfaceCleared( inspection.View() );
    }

    SUBCASE( "direct retarget hides the old surface before the new transport" )
    {
        ReplayCauseInspection inspection;
        publishFocusedSurface( inspection );
        ReplayCauseSeekResult retarget = seek;
        retarget.frame = 15u;
        REQUIRE( inspection.Select( 4, retarget, 12u, true, 3.0 ) );
        CHECK( inspection.View().selectedRow == 4 );
        CHECK( inspection.View().targetFrame == 15u );
        checkSurfaceCleared( inspection.View() );
    }

    SUBCASE( "return and reset cannot leave a frozen surface" )
    {
        ReplayCauseInspection inspection;
        publishFocusedSurface( inspection );
        REQUIRE( inspection.BeginReturn().apply );
        checkSurfaceCleared( inspection.View() );
        inspection.CompleteReturn();
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Inactive );
        checkSurfaceCleared( inspection.View() );
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

TEST_CASE( "Replay cause inspection: click or scrub return is accepted during entry and aftermath" )
{
    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.frame = 12u;

    SUBCASE( "entry" )
    {
        for ( bool scrubExit : { false, true } )
        {
            ReplayCauseInspection inspection;
            REQUIRE( inspection.Select( 0, seek, 20u, false, 1.0 ) );
            REQUIRE( ShouldBeginReplayCauseReturn( inspection.View(), !scrubExit, scrubExit ) );
            const ReplayCauseExitAction exit = inspection.BeginReturn();
            CHECK( exit.apply );
            CHECK( exit.releasePause );
            CHECK( inspection.View().mode == ReplayCauseInspectionMode::Returning );
        }
    }

    SUBCASE( "aftermath" )
    {
        for ( bool scrubExit : { false, true } )
        {
            ReplayCauseInspection inspection;
            REQUIRE( inspection.Select( 0, seek, 20u, false, 1.0 ) );
            inspection.Advance( 2.5 );
            ReplayCauseTransportRequest request;
            REQUIRE( inspection.TakeTransportRequest( request ) );
            inspection.CompleteTransport( request.generation, true );
            REQUIRE( ShouldBeginReplayCauseAftermath( inspection.View(), true ) );
            bool releasePause = false;
            REQUIRE( inspection.BeginAftermath( releasePause ) );
            CHECK_FALSE( inspection.View().detailVisible );
            REQUIRE( ShouldBeginReplayCauseReturn( inspection.View(), !scrubExit, scrubExit ) );
            const ReplayCauseExitAction exit = inspection.BeginReturn();
            CHECK( exit.apply );
            CHECK_FALSE( exit.releasePause );
            CHECK( inspection.View().mode == ReplayCauseInspectionMode::Returning );
        }
    }
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
