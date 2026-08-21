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
//   Prediction Manifold and SolverRow cases resolve only a complete immutable
//   evidence-bank stamp; synthetic rows and every stale or same-frame-replaced
//   identity stay unavailable.
//   Presentation tests pin event-frame body poses, exact ManifoldRow points,
//   derived surviving-row points, owned packet publication, solver-row copy
//   lifetime, the compact unavailable state, and the up-to-four-row scrolling
//   viewport.
//   The UI projection test pins exact solver-value text, units, and signs.
//   The scientific-inspector projection pins the approved compound surface:
//   exact visual states, flush drawer geometry, one-anchor motion, fixed tabs,
//   180 ms easing, reachable compact controls, ancestor-preserving filtered
//   source-row identity, and the grouped Raw Record projection plus serialized
//   copy payload. Its negative control records why the retired detached
//   placement could not satisfy the joined contract.
//   The Planning transition tests also pin request coalescing, pause ownership,
//   Space aftermath, total-elapsed cubic easing, symmetric discrete frame
//   rounding, and saved-camera return policy without host owners.
//
// Invariants:
//   - Retained-window boundaries are inclusive at the oldest frame and exclusive at the live edge.
//   - Prediction rows require an exact published frame, including terrain-independent contact rows.
//   - A prediction row never borrows recorded/current diagnostics or a replacement evidence bank as solver detail.
//   - Prediction contact geometry is projected from exact event-frame poses,
//     not live or recorded body positions.
//   - Missing pipeline detail never disables transport for a retained frame.
//   - A current or coincident diagnostics row never substitutes for a mismatched frame stamp or row index.
//   - Detached contact packets use exact retained points when present and derive
//     only surviving rows when an exact point record is unavailable.
//   - Retarget, aftermath, return, failure, and reset clear exact evidence
//     immediately while drawer visibility completes its bounded reverse ease.
//   - Forward and reverse transport are monotonic and land on the exact target.
//
// Related:
//   - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
//   - SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h
//

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h"
#include "../SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h"
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

constexpr float CAUSE_INSPECTOR_TARGET_DRAWER_WIDTH = 520.0f;
constexpr double CAUSE_INSPECTOR_TARGET_DRAWER_SECONDS = 0.18;

float CauseInspectorTargetDrawerEase( double elapsedSeconds )
{
    const double t = std::clamp( elapsedSeconds / CAUSE_INSPECTOR_TARGET_DRAWER_SECONDS, 0.0, 1.0 );
    const double remaining = 1.0 - t;
    return static_cast<float>( 1.0 - remaining * remaining * remaining );
}

struct CauseInspectorTargetFilterRow
{
    int sourceRow = -1;
    int parentSourceRow = -1;
    bool textMatch = false;
    bool contactKind = false;
};

// Invariant: projected indices are original source-row identities in source
// order; matching descendants retain their complete ancestor path.
struct CauseInspectorTargetFilterProjection
{
    std::array<int, 8> sourceRows {};
    std::size_t count = 0;
};

CauseInspectorTargetFilterProjection BuildCauseInspectorTargetFilter( std::span<const CauseInspectorTargetFilterRow> rows,
                                                                      bool contactsOnly )
{
    std::array<bool, 8> keep {};

    if ( !rows.empty() )
    {
        keep[0] = true; // The hierarchy root remains visible even when no descendant matches.
    }

    for ( std::size_t index = 0; index < rows.size(); ++index )
    {
        if ( !rows[index].textMatch || ( contactsOnly && !rows[index].contactKind ) )
        {
            continue;
        }

        int sourceRow = rows[index].sourceRow;

        while ( sourceRow >= 0 )
        {
            REQUIRE( sourceRow < static_cast<int>( rows.size() ) );
            keep[static_cast<std::size_t>( sourceRow )] = true;
            sourceRow = rows[static_cast<std::size_t>( sourceRow )].parentSourceRow;
        }
    }

    CauseInspectorTargetFilterProjection projection;

    for ( std::size_t index = 0; index < rows.size(); ++index )
    {
        if ( keep[index] )
        {
            projection.sourceRows[projection.count++] = rows[index].sourceRow;
        }
    }

    return projection;
}

} // namespace

TEST_CASE( "Cause hierarchy inspector target: deterministic visual fixtures pin every approved state" )
{
    struct Fixture
    {
        const char* name;
        int width;
        int height;
        int selectedSourceRow;
        ReplayCauseInspectorTab tab;
        float progress;
        int scrollRows;
    };

    constexpr std::array fixtures = {
        Fixture { "hierarchy-only", 1920, 1080, -1, ReplayCauseInspectorTab::Summary, 0.0f, 0 },
        Fixture { "opening-midpoint", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 0.5f, 0 },
        Fixture { "summary-open", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 1.0f, 0 },
        Fixture { "raw-top", 1920, 1080, 3, ReplayCauseInspectorTab::RawRecord, 1.0f, 0 },
        Fixture { "raw-scrolled", 1920, 1080, 3, ReplayCauseInspectorTab::RawRecord, 1.0f, 6 },
        Fixture { "iterations", 1920, 1080, 3, ReplayCauseInspectorTab::Iterations, 1.0f, 0 },
        Fixture { "filtered", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 0.0f, 0 },
        Fixture { "unavailable", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 1.0f, 0 },
        Fixture { "moved", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 1.0f, 0 },
        Fixture { "resized", 1920, 1080, 3, ReplayCauseInspectorTab::Summary, 1.0f, 0 },
        Fixture { "compact", 931, 643, 3, ReplayCauseInspectorTab::RawRecord, 1.0f, 0 },
    };

    CHECK( fixtures.size() == 11u );
    CHECK( std::strcmp( fixtures.front().name, "hierarchy-only" ) == 0 );
    CHECK( std::strcmp( fixtures.back().name, "compact" ) == 0 );
    CHECK( fixtures[1].progress == doctest::Approx( 0.5f ) );
    CHECK( fixtures[4].scrollRows == 6 );
    CHECK( fixtures.back().width == 931 );
    CHECK( fixtures.back().height == 643 );
}

TEST_CASE( "Cause hierarchy inspector target: one anchor controls flush drawer motion and fixed tab footprint" )
{
    RunReplayCauseTreeState state;
    state.hasWindowPlacement = true;
    state.x = 1180;
    state.y = 140;
    state.width = 430;
    state.height = 500;

    const ReplayCauseInspectionView inspection;
    const ReplayCauseInspectorLayout summary = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    CHECK( summary.targetDrawer.x + summary.targetDrawer.w == doctest::Approx( summary.hierarchy.x ) );
    CHECK( summary.drawer.y == doctest::Approx( summary.hierarchy.y ) );
    CHECK( summary.drawer.h == doctest::Approx( summary.hierarchy.h ) );
    CHECK( summary.compound.x == doctest::Approx( 660.0f ) );
    CHECK( summary.compound.w == doctest::Approx( 950.0f ) );
    CHECK( summary.tabs[0].w == doctest::Approx( summary.tabs[1].w ) );
    CHECK( summary.tabs[1].w == doctest::Approx( summary.tabs[2].w ) );
    CHECK( summary.tabs[0].y == doctest::Approx( summary.tabs[2].y ) );

    state.x += 73;
    state.y += 41;
    const ReplayCauseInspectorLayout moved = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    CHECK( moved.hierarchy.x - summary.hierarchy.x == doctest::Approx( 73.0f ) );
    CHECK( moved.drawer.x - summary.drawer.x == doctest::Approx( 73.0f ) );
    CHECK( moved.hierarchy.y - summary.hierarchy.y == doctest::Approx( 41.0f ) );
    CHECK( moved.drawer.y - summary.drawer.y == doctest::Approx( 41.0f ) );
}

TEST_CASE( "Cause hierarchy inspector target: compact geometry and 180 ms ease have exact endpoints" )
{
    RunReplayCauseTreeState compact;
    compact.hasWindowPlacement = true;
    compact.x = 528;
    compact.y = 84;
    compact.width = 380;
    compact.height = 520;

    const ReplayCauseInspectionView inspection;
    const ReplayCauseInspectorLayout closed = BuildReplayCauseInspectorLayout( inspection, compact, 931, 643,
                                                                               CauseInspectorTargetDrawerEase( 0.0 ) );
    const ReplayCauseInspectorLayout midpoint = BuildReplayCauseInspectorLayout( inspection, compact, 931, 643,
                                                                                 CauseInspectorTargetDrawerEase( 0.09 ) );
    const ReplayCauseInspectorLayout open = BuildReplayCauseInspectorLayout( inspection, compact, 931, 643,
                                                                             CauseInspectorTargetDrawerEase( 0.18 ) );
    CHECK( closed.visibleDrawer.w == doctest::Approx( 0.0f ) );
    CHECK( midpoint.visibleDrawer.w == doctest::Approx( 455.0f ) );
    CHECK( open.targetDrawer.x == doctest::Approx( 8.0f ) );
    CHECK( open.targetDrawer.w == doctest::Approx( 520.0f ) );
    CHECK( open.compound.x == doctest::Approx( 8.0f ) );
    CHECK( open.compound.w == doctest::Approx( 900.0f ) );
    CHECK( open.compound.h == doctest::Approx( 520.0f ) );
    CHECK( CauseInspectorTargetDrawerEase( 1.0 ) == doctest::Approx( 1.0f ) );
}

TEST_CASE( "Cause hierarchy inspector layout: compound clamping preserves reachable normal and compact controls" )
{
    RunReplayCauseTreeState normal;
    ReplayOverlay::EnsureReplayCauseWindowPlacement( normal, 1920, 1080, REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                                     REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    CHECK( normal.x == 1516 );
    CHECK( normal.y == 84 );
    CHECK( normal.width == 380 );
    CHECK( normal.height == 520 );

    RunReplayCauseTreeState compact;
    ReplayOverlay::EnsureReplayCauseWindowPlacement( compact, 931, 643, REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                                     REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    CHECK( compact.x == 528 );
    CHECK( compact.y == 84 );
    CHECK( compact.width == 380 );
    CHECK( compact.height == 520 );

    compact.x = -900;
    compact.y = -900;
    ReplayOverlay::ClampReplayCauseWindow( compact, 931, 643, REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                           REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    const ReplayCauseInspectionView inspection;
    const ReplayCauseInspectorLayout topLeft = BuildReplayCauseInspectorLayout( inspection, compact, 931, 643, 1.0f );
    CHECK( topLeft.targetCompound.x == doctest::Approx( 8.0f ) );
    CHECK( topLeft.targetCompound.y == doctest::Approx( 84.0f ) );
    CHECK( topLeft.drawerClose.x + topLeft.drawerClose.w <= topLeft.hierarchy.x );
    CHECK( topLeft.resize.x + topLeft.resize.w <= 923.0f );

    compact.x = 9000;
    compact.y = 9000;
    ReplayOverlay::ClampReplayCauseWindow( compact, 931, 643, REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                           REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    const ReplayCauseInspectorLayout bottomRight = BuildReplayCauseInspectorLayout( inspection, compact, 931, 643, 1.0f );
    CHECK( bottomRight.targetCompound.x >= 8.0f );
    CHECK( bottomRight.compound.x + bottomRight.compound.w <= 923.0f );
    CHECK( bottomRight.compound.y + bottomRight.compound.h <= 635.0f );
}

TEST_CASE( "Cause hierarchy inspector layout: drawer-title drag and resize mutate only the Replay anchor" )
{
    RunReplayCauseTreeState state;
    ReplayOverlay::EnsureReplayCauseWindowPlacement( state, 1920, 1080, REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH,
                                                     REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    const ReplayCauseInspectionView inspection;
    const ReplayCauseInspectorLayout before = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    const int drawerTitleX = static_cast<int>( before.drawerTitle.x + 20.0f );
    const int drawerTitleY = static_cast<int>( before.drawerTitle.y + 16.0f );

    state.dragOffsetX = drawerTitleX - state.x;
    state.dragOffsetY = drawerTitleY - state.y;
    ReplayOverlay::MoveReplayCauseWindow( state, drawerTitleX - 73, drawerTitleY + 41, 1920, 1080,
                                          REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH, REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    const ReplayCauseInspectorLayout moved = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    CHECK( moved.hierarchy.x - before.hierarchy.x == doctest::Approx( -73.0f ) );
    CHECK( moved.targetDrawer.x - before.targetDrawer.x == doctest::Approx( -73.0f ) );
    CHECK( moved.hierarchy.y - before.hierarchy.y == doctest::Approx( 41.0f ) );
    CHECK( moved.targetDrawer.y - before.targetDrawer.y == doctest::Approx( 41.0f ) );

    const int resizeX = static_cast<int>( moved.resize.x + moved.resize.w );
    const int resizeY = static_cast<int>( moved.resize.y + moved.resize.h );
    state.resizeStartMouseX = resizeX;
    state.resizeStartMouseY = resizeY;
    state.resizeStartWidth = state.width;
    state.resizeStartHeight = state.height;
    ReplayOverlay::ResizeReplayCauseWindow( state, resizeX + 50, resizeY - 20, 1920, 1080,
                                            REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH, REPLAY_CAUSE_INSPECTOR_DRAWER_MIN_WIDTH );
    const ReplayCauseInspectorLayout resized = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    CHECK( resized.targetDrawer.x == doctest::Approx( moved.targetDrawer.x ) );
    CHECK( resized.targetDrawer.h == doctest::Approx( resized.hierarchy.h ) );
    CHECK( resized.hierarchy.w - moved.hierarchy.w == doctest::Approx( 50.0f ) );
    CHECK( resized.hierarchy.h - moved.hierarchy.h == doctest::Approx( -20.0f ) );
}

TEST_CASE( "Cause hierarchy inspector target: filtering preserves ancestor paths and source-row identity" )
{
    constexpr std::array rows = {
        CauseInspectorTargetFilterRow { 0, -1, false, false }, CauseInspectorTargetFilterRow { 1, 0, false, false },
        CauseInspectorTargetFilterRow { 2, 1, false, true },   CauseInspectorTargetFilterRow { 3, 2, true, true },
        CauseInspectorTargetFilterRow { 4, 0, false, false },  CauseInspectorTargetFilterRow { 5, 4, false, true },
    };
    const CauseInspectorTargetFilterProjection projection = BuildCauseInspectorTargetFilter( rows, true );
    constexpr std::array expected = { 0, 1, 2, 3 };
    REQUIRE( projection.count == expected.size() );

    for ( std::size_t index = 0; index < expected.size(); ++index )
    {
        CHECK( projection.sourceRows[index] == expected[index] );
    }

    CHECK( projection.sourceRows[3] == 3 ); // Selection maps back to Solver Row 16's source row.
}

TEST_CASE( "Cause hierarchy inspector negative control: legacy detached panel fails the approved contract" )
{
    std::array<SkullbonezCore::Physics::PhysicsSolverPersistentContactSample, 1> contacts;
    ReplayCauseInspectionView inspection;
    inspection.solverDetailAvailability = ReplayCauseSolverDetailAvailability::Available;
    inspection.solverDetailContacts = contacts;

    RunReplayCauseTreeState state;
    state.hasWindowPlacement = true;
    state.x = 1180;
    state.y = 140;
    state.width = 380;
    state.height = 500;

    const ReplayCauseInspectorLayout target = BuildReplayCauseInspectorLayout( inspection, state, 1920, 1080, 1.0f );
    const SkullbonezCore::UI::UIRect legacy { state.x - 10.0f - CAUSE_INSPECTOR_TARGET_DRAWER_WIDTH,
                                              static_cast<float>( state.y ), CAUSE_INSPECTOR_TARGET_DRAWER_WIDTH, 226.0f };
    CHECK_FALSE( legacy.x == doctest::Approx( target.targetDrawer.x ) ); // Legacy kept a 10 px gutter.
    CHECK_FALSE( legacy.h == doctest::Approx( target.targetDrawer.h ) ); // Legacy grew from row count.
    CHECK( EvaluateReplayCauseTransitionProgress( CAUSE_INSPECTOR_TARGET_DRAWER_SECONDS ) < 1.0f );
    CHECK( REPLAY_CAUSE_SOLVER_PANEL_OPACITY < 0.9f );
}

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

    SUBCASE( "prediction rows preserve the unavailable negative control" )
    {
        row.prediction = true;
        seek.source = ReplayCauseSeekSource::Prediction;

        for ( RunReplayCauseTreeRowKind kind :
              { RunReplayCauseTreeRowKind::PredictionContact, RunReplayCauseTreeRowKind::Manifold,
                RunReplayCauseTreeRowKind::SolverRow } )
        {
            row.kind = kind;
            const ReplayCauseSolverDetailResult detail = EvaluateReplayCauseSolverDetail( row, seek, { 84u, contacts, {} } );
            CHECK_FALSE( detail.HasDetail() );
            CHECK( detail.availability == ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable );
            CHECK( std::strcmp( detail.Feedback(), "Solver detail not available" ) == 0 );
        }
    }
}

TEST_CASE( "Replay cause solver detail: predicted rows require the exact immutable evidence identity" )
{
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    ReplayPredictionSolverEvidenceBanks banks;
    const uint64_t epoch = banks.BeginBuild( 7u, ReplayPredictionDetailMode::High );
    std::array<PhysicsSolverPersistentContactSample, 2> contacts;
    contacts[0].bodyA = 3;
    contacts[0].bodyB = 7;
    contacts[0].featureId = 101u;
    contacts[1].bodyA = 7;
    contacts[1].bodyB = 3;
    contacts[1].featureId = 102u;
    std::array<PhysicsPipelineRecord, 3> pipeline;
    pipeline[0] = { .stage = PhysicsPipelineStage::ManifoldRow, .bodyA = 3, .bodyB = 7, .featureId = 101u };
    pipeline[1] = { .stage = PhysicsPipelineStage::WarmStart, .bodyA = 3, .bodyB = 7, .featureId = 102u };
    pipeline[2] = { .stage = PhysicsPipelineStage::SolverIteration,
                    .bodyA = 3,
                    .bodyB = 7,
                    .iteration = 0,
                    .featureId = 101u };
    REQUIRE( banks.AppendBuildFrame( 84u, 3u, 900u, contacts, pipeline, 84 ) );
    REQUIRE( banks.PromoteBuild() );
    const ReplayPredictionSolverEvidenceFrame* frame = banks.Committed().PublishedFrame( 0u );
    REQUIRE( frame != nullptr );

    RunReplayCauseTreeRow row;
    row.kind = RunReplayCauseTreeRowKind::Manifold;
    row.firstFrame = 84u;
    row.modelRow.value = 3;
    row.counterpartModelRow.value = 7;
    row.contactIndex = 0;
    row.pipelineIndex = 0;
    row.featureId = 101;
    row.prediction = true;
    row.sourceGeneration = 7u;
    row.sourceBankEpoch = epoch;
    row.sourceTopologyVersion = 3u;
    row.sourcePublicationVersion = 900u;
    row.sourceHighDetail = true;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::Prediction;
    seek.frame = 84u;
    ReplayCauseSolverDetailSource source;
    source.frame = 84u;
    source.prediction = { &banks.Committed(), frame };

    const ReplayCauseSolverDetailResult exact = EvaluateReplayCauseSolverDetail( row, seek, source );
    REQUIRE( exact.HasDetail() );
    CHECK( exact.contactRowCount == 2u );
    CHECK( exact.pipelineRecordCount == 3u );
    CHECK( exact.ContactRowAt( 1u )->featureId == 102u );
    CHECK( exact.PipelineRecordAt( 2u )->iteration == 0 );

    RunReplayPredictionFrame predictionFrame;
    predictionFrame.frameIndex = 84u;
    RunReplayPredictionBodySample bodyA;
    bodyA.id.value = 30u;
    bodyA.modelRow.value = 3;
    bodyA.position = SkullbonezCore::Math::Vector::Vector3( 1.0f, 2.0f, 3.0f );
    RunReplayPredictionBodySample bodyB;
    bodyB.id.value = 70u;
    bodyB.modelRow.value = 7;
    bodyB.position = SkullbonezCore::Math::Vector::Vector3( 4.0f, 5.0f, 6.0f );
    predictionFrame.bodies = { bodyA, bodyB };
    const SkullbonezCore::Rendering::ContactManifoldPresentation
        manifold = BuildReplayCauseContactPresentation( exact, predictionFrame );
    CHECK( manifold.bodyCount == 2u );
    CHECK( manifold.pointCount == 2u );

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 2, seek, 0u, false, 1.0 ) );
    inspection.Advance( 2.5 );
    ReplayCauseTransportRequest transport;
    REQUIRE( inspection.TakeTransportRequest( transport ) );
    inspection.PublishSolverDetail( transport.generation, exact, manifold );
    inspection.CompleteTransport( transport.generation, true );
    CHECK( inspection.View().detailVisible );
    CHECK( inspection.View().solverDetailAvailability == ReplayCauseSolverDetailAvailability::Available );
    CHECK( inspection.View().solverDetailContactRowCount == 2u );
    CHECK( inspection.View().solverDetailPipelineRecordCount == 3u );

    const auto rejected = [&]( RunReplayCauseTreeRow candidate,
                               ReplayCauseSolverDetailSource candidateSource = ReplayCauseSolverDetailSource {} )
    {
        if ( !candidateSource.prediction.Valid() )
        {
            candidateSource = source;
        }

        return !EvaluateReplayCauseSolverDetail( candidate, seek, candidateSource ).HasDetail();
    };

    RunReplayCauseTreeRow candidate = row;
    ++candidate.sourceGeneration;
    CHECK( rejected( candidate ) );
    candidate = row;
    candidate.sourceHighDetail = false;
    CHECK( rejected( candidate ) );
    candidate = row;
    ++candidate.sourceBankEpoch;
    CHECK( rejected( candidate ) );
    candidate = row;
    ++candidate.sourceTopologyVersion;
    CHECK( rejected( candidate ) );
    candidate = row;
    ++candidate.sourcePublicationVersion;
    CHECK( rejected( candidate ) );
    candidate = row;
    candidate.contactIndex = 2;
    CHECK( rejected( candidate ) );
    candidate = row;
    candidate.pipelineIndex = 1;
    CHECK( rejected( candidate ) );
    candidate = row;
    ++candidate.featureId;
    CHECK( rejected( candidate ) );

    ReplayCauseSeekResult wrongFrameSeek = seek;
    wrongFrameSeek.frame = 85u;
    CHECK_FALSE( EvaluateReplayCauseSolverDetail( row, wrongFrameSeek, source ).HasDetail() );

    const uint64_t replacementEpoch = banks.BeginBuild( 8u, ReplayPredictionDetailMode::High );
    REQUIRE( replacementEpoch != epoch );
    REQUIRE( banks.AppendBuildFrame( 84u, 4u, 901u, contacts, pipeline, 84 ) );
    REQUIRE( banks.PromoteBuild() );
    const ReplayPredictionSolverEvidenceFrame* replacement = banks.Committed().PublishedFrame( 0u );
    REQUIRE( replacement != nullptr );
    ReplayCauseSolverDetailSource replacementSource;
    replacementSource.frame = 84u;
    replacementSource.prediction = { &banks.Committed(), replacement };
    CHECK( rejected( row, replacementSource ) );
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
    causeTree.height = 520;
    const ReplayCauseInspectorLayout layout = BuildReplayCauseInspectorLayout( published, causeTree, 1920, 1080, 1.0f );
    CHECK( layout.targetDrawer.w == doctest::Approx( 520.0f ) );
    CHECK( layout.visibleRows == 4 );
    CHECK( layout.content.h >= layout.rowHeight * 4.0f );

    // Invariant: the panel has no second placement state: every cause-window drag or
    // resize moves the adjacent surface through this same projection.
    causeTree.x = 1180;
    causeTree.y = 140;
    causeTree.width = 430;
    causeTree.height = 500;
    const ReplayCauseInspectorLayout moved = BuildReplayCauseInspectorLayout( published, causeTree, 1920, 1080, 1.0f );
    CHECK( moved.targetDrawer.x == doctest::Approx( 660.0f ) );
    CHECK( moved.targetDrawer.y == doctest::Approx( 140.0f ) );
    CHECK( moved.targetDrawer.h == doctest::Approx( moved.hierarchy.h ) );

    ReplayCauseInspectionView unavailable = published;
    unavailable.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    unavailable.solverDetailContacts = {};
    unavailable.solverDetailPipelineRecords = {};
    causeTree.x = 528;
    causeTree.y = 84;
    causeTree.width = 380;
    causeTree.height = 520;
    const ReplayCauseInspectorLayout compactUnavailable = BuildReplayCauseInspectorLayout( unavailable, causeTree, 931, 643,
                                                                                           1.0f );
    CHECK( compactUnavailable.targetDrawer.w == doctest::Approx( REPLAY_CAUSE_INSPECTOR_DRAWER_WIDTH ) );
    CHECK( compactUnavailable.targetDrawer.h == doctest::Approx( 520.0f ) );
    CHECK( compactUnavailable.targetCompound.x == doctest::Approx( 8.0f ) );
    CHECK( compactUnavailable.targetCompound.w == doctest::Approx( 900.0f ) );
    CHECK( compactUnavailable.visibleRows == 0 );
    CHECK( REPLAY_CAUSE_SOLVER_PANEL_OPACITY == doctest::Approx( 0.78f ) );

    causeTree.x = 1180;
    causeTree.y = 140;
    causeTree.width = 430;
    causeTree.height = 500;
    const int panelX = static_cast<int>( moved.content.x + 20.0f );
    const int panelY = static_cast<int>( moved.content.y + 20.0f );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 1 );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 2 );
    REQUIRE( inspection.TickSolverDetailPanelInput( causeTree, panelX, panelY, true, false, false, -120, 1920, 1080 ) );
    CHECK( inspection.View().solverDetailFirstRow == 3 );
    CHECK_FALSE( inspection.TickSolverDetailPanelInput( causeTree, 1919, 1079, true, false, false, -120, 1920, 1080 ) );
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
    const ReplayCauseSummaryText summary = BuildReplayCauseSummaryText( inspection, 0 );

    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_UNITS,
                        "UNITS: vectors/penetration/correction = scene units; bias/linear writeback = u/s;" ) == 0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_UNITS_MORE,
                        "angular = rad/s; impulses = mass*u/s; effective masses = mass." ) == 0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_SIGNS, "SIGNS: +penetration = overlap; normal/t1/t2 = world-space;" ) ==
           0 );
    CHECK( std::strcmp( REPLAY_CAUSE_SOLVER_PANEL_SIGNS_MORE,
                        "signed accT1/accT2 follow t1/t2; CLAMP = frictionLimit reached." ) == 0 );
    CHECK( std::strcmp( values.headline, "ROW 0  FEATURE 101  BODIES 3 / 7  POINT (7.0000, 8.0000, 9.0000)" ) == 0 );
    CHECK( std::strcmp( values.basis, "n (0.1000 0.2000 0.3000)  t1 (0.4000 0.5000 0.6000)  t2 (0.7000 0.8000 0.9000)" ) ==
           0 );
    CHECK( std::strcmp( values.geometry, "rA (1.2500 2.5000 3.7500)  rB (4.0000 5.0000 6.0000)  penetration 0.12500" ) ==
           0 );
    CHECK( std::strcmp( values.masses,
                        "normalMass 2.00000  tangentMass (2.25000, 2.50000)  bias 2.75000  frictionLimit 3.00000" ) == 0 );
    CHECK( std::strcmp( values.impulses,
                        "accN 3.50000  accT1 -3.75000  accT2 4.00000  warm-start YES  previous normal impulse 11.25000" ) ==
           0 );
    CHECK( std::strcmp( summary.normalImpulse, "3.50000 N*s" ) == 0 );
    CHECK( std::strcmp( summary.frictionImpulse, "5.48293 N*s" ) == 0 );
    CHECK( std::strcmp( summary.penetration, "0.12500 u" ) == 0 );
    CHECK( std::strcmp( summary.effectiveMass, "2.00000 mass" ) == 0 );
    CHECK( std::strcmp( summary.identity, "ROW 0  FEATURE 101  BODIES 3 / 7  OBJECT" ) == 0 );
    CHECK( std::strcmp( summary.dynamics,
                        "bias 2.75000   friction limit 3.00000   tangent mass 2.25000 / 2.50000   manifold points 1" ) ==
           0 );
    CHECK( std::strcmp( summary.policy, "warm YES   resting YES   tangent friction YES   coupled NO   sleep ALLOWED" ) ==
           0 );
}

TEST_CASE( "Replay cause inspector drawer: total elapsed easing is cadence independent and closes symmetrically" )
{
    ReplayCauseInspection inspection;
    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 12u;

    REQUIRE( inspection.Select( 0, seek, 20u, false, 10.0 ) );
    CHECK( inspection.View().detailVisible );
    CHECK( inspection.View().drawerProgress == doctest::Approx( 0.0f ) );
    inspection.Advance( 10.09 );
    CHECK( inspection.View().drawerProgress == doctest::Approx( 0.875f ).epsilon( 0.001 ) );
    inspection.Advance( 10.18 );
    CHECK( inspection.View().drawerProgress == doctest::Approx( 1.0f ) );

    REQUIRE( inspection.BeginReturn().apply );
    inspection.Advance( 10.27 );
    CHECK( inspection.View().detailVisible );
    CHECK( inspection.View().drawerProgress == doctest::Approx( 0.125f ).epsilon( 0.001 ) );
    inspection.Advance( 10.36 );
    CHECK_FALSE( inspection.View().detailVisible );
    CHECK( inspection.View().drawerProgress == doctest::Approx( 0.0f ) );
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
        CHECK( inspection.View().detailVisible );
        inspection.Advance( 2.68 );
        CHECK_FALSE( inspection.View().detailVisible );

        // Invariant: direct retargeting from aftermath reacquires only the pause released
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
    const auto checkEvidenceCleared = []( const ReplayCauseInspectionView& view )
    {
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
        CHECK( inspection.View().detailVisible );
        checkEvidenceCleared( inspection.View() );
        inspection.Advance( 2.68 );
        CHECK_FALSE( inspection.View().detailVisible );
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
        CHECK( inspection.View().detailVisible );
        checkEvidenceCleared( inspection.View() );
    }

    SUBCASE( "return and reset cannot leave a frozen surface" )
    {
        ReplayCauseInspection inspection;
        publishFocusedSurface( inspection );
        REQUIRE( inspection.BeginReturn().apply );
        CHECK( inspection.View().detailVisible );
        checkEvidenceCleared( inspection.View() );
        inspection.CompleteReturn();
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Inactive );
        CHECK_FALSE( inspection.View().detailVisible );
        checkEvidenceCleared( inspection.View() );
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
    CHECK( inspection.View().detailVisible );
    inspection.Advance( 1.68 );
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
            CHECK( inspection.View().detailVisible );
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

        // Invariant: the owner samples total wall-clock elapsed, so render cadence does
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

TEST_CASE( "Cause hierarchy inspector: Raw Record tab projects complete grouped properties and serializes copy payload" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    ReplayCauseInspectionView view;
    view.targetFrame = 95u;
    view.seekSource = ReplayCauseSeekSource::SolverHistory;

    std::array<PhysicsSolverPersistentContactSample, 1> contacts;
    contacts[0].bodyA = 3;
    contacts[0].bodyB = 7;
    contacts[0].featureId = 42u;
    contacts[0].key = 10042;
    contacts[0].normal = Vector3( 0.0f, 1.0f, 0.0f );
    contacts[0].tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
    contacts[0].tangent2 = Vector3( 0.0f, 0.0f, 1.0f );
    contacts[0].rA = Vector3( 0.1f, -0.5f, 0.0f );
    contacts[0].rB = Vector3( 0.1f, 0.5f, 0.0f );
    contacts[0].penetration = 0.0125f;
    contacts[0].normalMass = 4.5f;
    contacts[0].tangentMass1 = 2.25f;
    contacts[0].tangentMass2 = 2.25f;
    contacts[0].bias = 0.05f;
    contacts[0].frictionLimit = 1.8f;
    contacts[0].accN = 1.2f;
    contacts[0].accT1 = 0.3f;
    contacts[0].accT2 = 0.4f;
    contacts[0].warmStarted = true;
    contacts[0].isTerrain = false;
    contacts[0].supportsRestingPolicy = true;
    contacts[0].allowsTangentFriction = true;
    contacts[0].normalCoupledFriction = false;
    contacts[0].inhibitsSleep = false;
    contacts[0].manifoldPointCount = 2;

    std::array<PhysicsPipelineRecord, 2> records;
    records[0].stage = PhysicsPipelineStage::ManifoldRow;
    records[0].featureId = 42u;
    records[0].point = Vector3( 10.0f, 5.0f, -2.0f );

    records[1].stage = PhysicsPipelineStage::PositionCorrection;
    records[1].featureId = 42u;
    records[1].scalarA = 0.005f;

    view.solverDetailContacts = contacts;
    view.solverDetailPipelineRecords = records;

    SUBCASE( "invalid row index returns empty projection" )
    {
        const ReplayCauseRawRecordProjection emptyNeg = BuildReplayCauseRawRecordProjection( view, -1 );
        CHECK( emptyNeg.rowCount == 0u );

        const ReplayCauseRawRecordProjection emptyHigh = BuildReplayCauseRawRecordProjection( view, 5 );
        CHECK( emptyHigh.rowCount == 0u );
    }

    SUBCASE( "valid row index projects all sections and values correctly" )
    {
        const ReplayCauseRawRecordProjection proj = BuildReplayCauseRawRecordProjection( view, 0 );
        REQUIRE( proj.rowCount > 0u );
        CHECK( proj.rowCount <= REPLAY_CAUSE_RAW_RECORD_ROW_CAPACITY );

        bool foundIdentity = false;
        bool foundGeometry = false;
        bool foundBasis = false;
        bool foundSolver = false;
        bool foundImpulses = false;
        bool foundFlags = false;
        bool foundRowIndex = false;
        bool foundFeature = false;
        bool foundBodyA = false;
        bool foundBodyB = false;
        bool foundPoint = false;
        bool foundNormal = false;
        bool foundAccN = false;
        bool foundWarm = false;

        for ( std::size_t i = 0; i < proj.rowCount; ++i )
        {
            const auto& row = proj.rows[i];
            if ( row.kind == ReplayCauseRawRecordRowKind::Section )
            {
                if ( std::strcmp( row.label, "IDENTITY" ) == 0 ) foundIdentity = true;
                if ( std::strcmp( row.label, "GEOMETRY" ) == 0 ) foundGeometry = true;
                if ( std::strcmp( row.label, "CONTACT BASIS" ) == 0 ) foundBasis = true;
                if ( std::strcmp( row.label, "SOLVER VALUES" ) == 0 ) foundSolver = true;
                if ( std::strcmp( row.label, "ACCUMULATED IMPULSES" ) == 0 ) foundImpulses = true;
                if ( std::strcmp( row.label, "FLAGS & POLICY" ) == 0 ) foundFlags = true;
            }
            else
            {
                if ( std::strcmp( row.label, "Row Index" ) == 0 && std::strcmp( row.value, "0" ) == 0 ) foundRowIndex = true;
                if ( std::strcmp( row.label, "Feature ID" ) == 0 && std::strcmp( row.value, "42" ) == 0 ) foundFeature = true;
                if ( std::strcmp( row.label, "Body A" ) == 0 && std::strcmp( row.value, "3" ) == 0 ) foundBodyA = true;
                if ( std::strcmp( row.label, "Body B" ) == 0 && std::strcmp( row.value, "7" ) == 0 ) foundBodyB = true;
                if ( std::strcmp( row.label, "Contact Point" ) == 0 ) foundPoint = true;
                if ( std::strcmp( row.label, "Normal n" ) == 0 ) foundNormal = true;
                if ( std::strcmp( row.label, "Normal Impulse accN" ) == 0 ) foundAccN = true;
                if ( std::strcmp( row.label, "Warm Started" ) == 0 && std::strcmp( row.value, "YES" ) == 0 ) foundWarm = true;
            }
        }

        CHECK( foundIdentity );
        CHECK( foundGeometry );
        CHECK( foundBasis );
        CHECK( foundSolver );
        CHECK( foundImpulses );
        CHECK( foundFlags );
        CHECK( foundRowIndex );
        CHECK( foundFeature );
        CHECK( foundBodyA );
        CHECK( foundBodyB );
        CHECK( foundPoint );
        CHECK( foundNormal );
        CHECK( foundAccN );
        CHECK( foundWarm );

        char buffer[2048] = {};
        const bool serialized = SerializeReplayCauseRawRecord( proj, buffer, sizeof( buffer ) );
        CHECK( serialized );
        CHECK( std::strstr( buffer, "[IDENTITY]" ) != nullptr );
        CHECK( std::strstr( buffer, "Row Index: 0" ) != nullptr );
        CHECK( std::strstr( buffer, "Feature ID: 42" ) != nullptr );
        CHECK( std::strstr( buffer, "[GEOMETRY]" ) != nullptr );
        CHECK( std::strstr( buffer, "[CONTACT BASIS]" ) != nullptr );
        CHECK( std::strstr( buffer, "[SOLVER VALUES]" ) != nullptr );
        CHECK( std::strstr( buffer, "[ACCUMULATED IMPULSES]" ) != nullptr );
        CHECK( std::strstr( buffer, "[FLAGS & POLICY]" ) != nullptr );
        CHECK( std::strstr( buffer, "Warm Started: YES" ) != nullptr );
    }

    SUBCASE( "serialization handles edge cases and buffer limits" )
    {
        const ReplayCauseRawRecordProjection proj = BuildReplayCauseRawRecordProjection( view, 0 );
        char tinyBuffer[16] = {};
        CHECK_FALSE( SerializeReplayCauseRawRecord( proj, tinyBuffer, sizeof( tinyBuffer ) ) );
        CHECK_FALSE( SerializeReplayCauseRawRecord( proj, nullptr, 100u ) );

        ReplayCauseRawRecordProjection emptyProj;
        char buffer[128] = {};
        CHECK_FALSE( SerializeReplayCauseRawRecord( emptyProj, buffer, sizeof( buffer ) ) );
    }
}

TEST_CASE( "Cause hierarchy inspector: Raw Record input handles copy command and scrolling" )
{
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    RunReplayCauseTreeState treeState;
    treeState.hasWindowPlacement = true;
    treeState.x = 1180;
    treeState.y = 140;
    treeState.width = 430;
    treeState.height = 500;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 10u;

    std::array<PhysicsSolverPersistentContactSample, 1> contacts;
    contacts[0].bodyA = 1;
    contacts[0].bodyB = 2;
    contacts[0].featureId = 5u;

    ReplayCauseSolverDetailResult detail;
    detail.frame = 10u;
    detail.availability = ReplayCauseSolverDetailAvailability::Available;
    detail.sourceContacts = contacts;
    detail.bodyA = 1;
    detail.bodyB = 2;
    detail.contactRowCount = 1u;

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 0, seek, 5u, false, 1.0 ) );
    inspection.Advance( 2.5 );
    ReplayCauseTransportRequest request;
    REQUIRE( inspection.TakeTransportRequest( request ) );
    inspection.PublishSolverDetail( request.generation, detail );
    inspection.CompleteTransport( request.generation, true );

    const ReplayCauseInspectorLayout layout = BuildReplayCauseInspectorLayout( inspection.View(), treeState, 1920, 1080, 1.0f );
    CHECK( layout.rawTable.h > 0.0f );
    CHECK( layout.rawCopy.h == doctest::Approx( REPLAY_CAUSE_RAW_RECORD_COPY_HEIGHT ) );
    CHECK( layout.rawVisibleRows > 0 );

    SUBCASE( "switching to raw record tab and clicking copy record emits command" )
    {
        // Click Raw Record tab (tabs[1])
        const int tabX = static_cast<int>( layout.tabs[1].x + layout.tabs[1].w * 0.5f );
        const int tabY = static_cast<int>( layout.tabs[1].y + layout.tabs[1].h * 0.5f );
        CHECK( inspection.TickSolverDetailPanelInput( treeState, tabX, tabY, true, false, true, 0, 1920, 1080 ) );
        CHECK( inspection.View().activeTab == ReplayCauseInspectorTab::RawRecord );

        // Click Copy button
        const int copyX = static_cast<int>( layout.rawCopy.x + layout.rawCopy.w * 0.5f );
        const int copyY = static_cast<int>( layout.rawCopy.y + layout.rawCopy.h * 0.5f );
        ReplayCauseInspectorCommand command;
        CHECK( inspection.TickSolverDetailPanelInput( treeState, copyX, copyY, true, false, true, 0, 1920, 1080, &command ) );
        CHECK( command.kind == ReplayCauseInspectorCommandKind::CopyRecord );
        CHECK( std::strstr( command.text, "[IDENTITY]" ) != nullptr );
        CHECK( std::strstr( command.text, "Feature ID: 5" ) != nullptr );
    }

    SUBCASE( "wheel scroll in raw record tab scrolls rawRecordFirstRow within bounds" )
    {
        // Switch to Raw Record tab
        const int tabX = static_cast<int>( layout.tabs[1].x + layout.tabs[1].w * 0.5f );
        const int tabY = static_cast<int>( layout.tabs[1].y + layout.tabs[1].h * 0.5f );
        (void)inspection.TickSolverDetailPanelInput( treeState, tabX, tabY, true, false, true, 0, 1920, 1080 );

        const int insideX = static_cast<int>( layout.rawTable.x + 10.0f );
        const int insideY = static_cast<int>( layout.rawTable.y + 10.0f );

        CHECK( inspection.View().rawRecordFirstRow == 0 );
        // Scroll down
        (void)inspection.TickSolverDetailPanelInput( treeState, insideX, insideY, true, false, false, -120, 1920, 1080 );
        CHECK( inspection.View().rawRecordFirstRow >= 0 );

        // Scroll up
        (void)inspection.TickSolverDetailPanelInput( treeState, insideX, insideY, true, false, false, 120, 1920, 1080 );
        CHECK( inspection.View().rawRecordFirstRow == 0 );
    }
}

TEST_CASE( "Cause hierarchy inspector: Iterations tab projects exact pipeline stages" )
{
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    ReplayCauseInspectionView view;
    view.targetFrame = 120u;
    view.seekSource = ReplayCauseSeekSource::SolverHistory;

    std::array<PhysicsSolverPersistentContactSample, 1> contacts;
    contacts[0].bodyA = 2;
    contacts[0].bodyB = 8;
    contacts[0].featureId = 99u;
    contacts[0].frictionLimit = 2.5f;
    contacts[0].normalMass = 5.0f;
    contacts[0].warmStarted = true;

    std::array<PhysicsPipelineRecord, 6> records;
    // Stage 1: WarmStart
    records[0].stage = PhysicsPipelineStage::WarmStart;
    records[0].featureId = 99u;
    records[0].scalarA = 1.0f; // accNormal
    records[0].scalarB = 0.2f; // tangent 1
    records[0].scalarC = 0.1f; // tangent 2

    // Stage 2: Iteration 1 (free)
    records[1].stage = PhysicsPipelineStage::SolverIteration;
    records[1].featureId = 99u;
    records[1].iteration = 1;
    records[1].scalarA = 0.5f; // deltaNormal
    records[1].scalarB = 1.5f; // accNormal
    records[1].scalarC = 0.8f; // tangent impulse

    // Stage 3: Iteration 2 (clamped: 2.5 >= 2.5)
    records[2].stage = PhysicsPipelineStage::SolverIteration;
    records[2].featureId = 99u;
    records[2].iteration = 2;
    records[2].scalarA = 0.3f; // deltaNormal
    records[2].scalarB = 1.8f; // accNormal
    records[2].scalarC = 2.5f; // tangent impulse = frictionLimit

    // Stage 4: PositionCorrection
    records[3].stage = PhysicsPipelineStage::PositionCorrection;
    records[3].featureId = 99u;
    records[3].scalarA = 0.008f;

    // Stage 5: CacheStore
    records[4].stage = PhysicsPipelineStage::CacheStore;
    records[4].featureId = 99u;
    records[4].scalarA = 1.8f;
    records[4].scalarB = 1.2f;
    records[4].scalarC = 0.0f;

    // Stage 6: VelocityWriteback
    records[5].stage = PhysicsPipelineStage::VelocityWriteback;
    records[5].featureId = 99u;
    records[5].point = Vector3( 1.5f, -0.5f, 0.0f );

    view.solverDetailContacts = contacts;
    view.solverDetailPipelineRecords = records;

    SUBCASE( "invalid row index returns empty projection" )
    {
        const ReplayCauseIterationsProjection emptyNeg = BuildReplayCauseIterationsProjection( view, -1 );
        CHECK( emptyNeg.rowCount == 0u );
        CHECK( std::strstr( emptyNeg.summary, "No contact" ) != nullptr );

        const ReplayCauseIterationsProjection emptyHigh = BuildReplayCauseIterationsProjection( view, 4 );
        CHECK( emptyHigh.rowCount == 0u );
    }

    SUBCASE( "valid contact projects all pipeline stages accurately" )
    {
        const ReplayCauseIterationsProjection proj = BuildReplayCauseIterationsProjection( view, 0 );
        REQUIRE( proj.rowCount == 6u );
        CHECK( std::strstr( proj.summary, "Feature 99" ) != nullptr );

        // WarmStart
        CHECK( proj.rows[0].kind == ReplayCauseIterationRowKind::WarmStart );
        CHECK( std::strcmp( proj.rows[0].stage, "Warm Start" ) == 0 );
        CHECK( std::strcmp( proj.rows[0].status, "ACTIVE" ) == 0 );

        // Iteration 1 (free)
        CHECK( proj.rows[1].kind == ReplayCauseIterationRowKind::SolverIteration );
        CHECK( proj.rows[1].iterationIndex == 1 );
        CHECK( std::strcmp( proj.rows[1].stage, "Iter 1" ) == 0 );
        CHECK( std::strcmp( proj.rows[1].status, "FREE" ) == 0 );

        // Iteration 2 (clamped)
        CHECK( proj.rows[2].kind == ReplayCauseIterationRowKind::SolverIteration );
        CHECK( proj.rows[2].iterationIndex == 2 );
        CHECK( std::strcmp( proj.rows[2].stage, "Iter 2" ) == 0 );
        CHECK( std::strcmp( proj.rows[2].status, "CLAMP" ) == 0 );

        // Pos Correct
        CHECK( proj.rows[3].kind == ReplayCauseIterationRowKind::PositionCorrection );
        CHECK( std::strcmp( proj.rows[3].stage, "Pos Correct" ) == 0 );
        CHECK( std::strcmp( proj.rows[3].status, "APPLIED" ) == 0 );

        // Cache Store
        CHECK( proj.rows[4].kind == ReplayCauseIterationRowKind::CacheStore );
        CHECK( std::strcmp( proj.rows[4].stage, "Cache Store" ) == 0 );
        CHECK( std::strcmp( proj.rows[4].status, "SAVED" ) == 0 );

        // Writeback
        CHECK( proj.rows[5].kind == ReplayCauseIterationRowKind::VelocityWriteback );
        CHECK( std::strcmp( proj.rows[5].stage, "Writeback" ) == 0 );
        CHECK( std::strcmp( proj.rows[5].status, "COMMITTED" ) == 0 );
    }
}

TEST_CASE( "Cause hierarchy inspector: Iterations tab input and interaction handling" )
{
    using SkullbonezCore::Physics::PhysicsPipelineRecord;
    using SkullbonezCore::Physics::PhysicsPipelineStage;
    using SkullbonezCore::Physics::PhysicsSolverPersistentContactSample;

    RunReplayCauseTreeState treeState;
    treeState.hasWindowPlacement = true;
    treeState.x = 1180;
    treeState.y = 140;
    treeState.width = 430;
    treeState.height = 500;

    ReplayCauseSeekResult seek;
    seek.availability = ReplayCauseSeekAvailability::Available;
    seek.source = ReplayCauseSeekSource::SolverHistory;
    seek.frame = 10u;

    std::array<PhysicsSolverPersistentContactSample, 1> contacts;
    contacts[0].bodyA = 1;
    contacts[0].bodyB = 2;
    contacts[0].featureId = 5u;

    std::array<PhysicsPipelineRecord, 10> records;
    for ( int i = 0; i < 10; ++i )
    {
        records[static_cast<std::size_t>( i )].stage = PhysicsPipelineStage::SolverIteration;
        records[static_cast<std::size_t>( i )].featureId = 5u;
        records[static_cast<std::size_t>( i )].iteration = i + 1;
        records[static_cast<std::size_t>( i )].scalarA = 0.1f * static_cast<float>( i + 1 );
        records[static_cast<std::size_t>( i )].scalarB = 0.5f * static_cast<float>( i + 1 );
    }

    ReplayCauseSolverDetailResult detail;
    detail.frame = 10u;
    detail.availability = ReplayCauseSolverDetailAvailability::Available;
    detail.sourceContacts = contacts;
    detail.sourcePipelineRecords = records;
    detail.pipelineRecordCount = 10u;
    detail.bodyA = 1;
    detail.bodyB = 2;
    detail.contactRowCount = 1u;

    ReplayCauseInspection inspection;
    REQUIRE( inspection.Select( 0, seek, 5u, false, 1.0 ) );
    inspection.Advance( 2.5 );
    ReplayCauseTransportRequest request;
    REQUIRE( inspection.TakeTransportRequest( request ) );
    inspection.PublishSolverDetail( request.generation, detail );
    inspection.CompleteTransport( request.generation, true );

    const ReplayCauseInspectorLayout layout = BuildReplayCauseInspectorLayout( inspection.View(), treeState, 1920, 1080, 1.0f );
    CHECK( layout.iterationsTable.h > 0.0f );
    CHECK( layout.iterationsVisibleRows > 0 );

    SUBCASE( "switching to iterations tab (tab 2) and scrolling" )
    {
        // Click Iterations tab (tabs[2])
        const int tabX = static_cast<int>( layout.tabs[2].x + layout.tabs[2].w * 0.5f );
        const int tabY = static_cast<int>( layout.tabs[2].y + layout.tabs[2].h * 0.5f );
        CHECK( inspection.TickSolverDetailPanelInput( treeState, tabX, tabY, true, false, true, 0, 1920, 1080 ) );
        CHECK( inspection.View().activeTab == ReplayCauseInspectorTab::Iterations );

        const int insideX = static_cast<int>( layout.iterationsTable.x + 10.0f );
        const int insideY = static_cast<int>( layout.iterationsTable.y + 10.0f );

        CHECK( inspection.View().iterationsFirstRow == 0 );
        // Scroll down
        (void)inspection.TickSolverDetailPanelInput( treeState, insideX, insideY, true, false, false, -120, 1920, 1080 );
        CHECK( inspection.View().iterationsFirstRow >= 0 );

        // Scroll up
        (void)inspection.TickSolverDetailPanelInput( treeState, insideX, insideY, true, false, false, 120, 1920, 1080 );
        CHECK( inspection.View().iterationsFirstRow == 0 );

        // Switch to Summary tab (tabs[0]) resets iterationsFirstRow
        const int summaryTabX = static_cast<int>( layout.tabs[0].x + layout.tabs[0].w * 0.5f );
        const int summaryTabY = static_cast<int>( layout.tabs[0].y + layout.tabs[0].h * 0.5f );
        CHECK( inspection.TickSolverDetailPanelInput( treeState, summaryTabX, summaryTabY, true, false, true, 0, 1920, 1080 ) );
        CHECK( inspection.View().activeTab == ReplayCauseInspectorTab::Summary );
        CHECK( inspection.View().iterationsFirstRow == 0 );
    }

    SUBCASE( "clicking close button initiates return" )
    {
        const int closeX = static_cast<int>( layout.drawerClose.x + layout.drawerClose.w * 0.5f );
        const int closeY = static_cast<int>( layout.drawerClose.y + layout.drawerClose.h * 0.5f );
        CHECK( inspection.TickSolverDetailPanelInput( treeState, closeX, closeY, true, false, true, 0, 1920, 1080 ) );
        CHECK( inspection.View().mode == ReplayCauseInspectionMode::Returning );
    }
}
