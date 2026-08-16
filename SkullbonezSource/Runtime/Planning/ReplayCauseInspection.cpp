/*
File: ReplayCauseInspection.cpp
Purpose:
  Resolves causal-row transport/detail eligibility and advances the inspection transition.

Summary:
  Recorded rows are valid only inside the solver recorder's current bounded
  window. Prediction rows require an exact frame match in the published frame
  bank; neither path guesses, clamps, or reconstructs a missing frame. The
  solver-detail projection scans only an explicitly stamped exact-frame source,
  groups every row in the selected manifold, and joins its retained pipeline
  stages without allocating or retaining source borrows. Before restore can
  retire those borrows, Planning copies the event-frame body poses and every
  bounded contact frame into a feature-neutral Rendering value, and copies the
  joined contact and pipeline records into fixed arrays. One shared layout
  projection fixes the detail viewport at four complete rows and owns bounded
  wheel scrolling. The transition owner derives a fixed cubic curve from total wall-clock elapsed,
  coalesces its discrete frame requests, rejects stale completion tokens, and
  publishes pause/return actions for App to apply through concrete owners.

Glossary:
  In-flight generation: Exact transport token currently awaiting Replay restore
    completion; a newer selected generation may wait behind it.
  Detail join: Exact-frame association of one selected manifold's persistent
    contact rows with its retained solver pipeline records.
  Aftermath: Live follow mode entered from paused detail by Space.

Invariants:
  - Recorder statistics describe a contiguous half-open frame range.
  - Prediction matching is exact because publication may replace the frame bank.
  - Detail joins fail closed when the selected contact, identity, or source-frame
    stamp disagrees, while transport remains independently usable.
  - Contact presentation fails closed when either body pose or the complete
    bounded patch cannot be proven from the same solver frame.
  - Solver-detail publication fails closed before exposing partial copied spans;
    its scroll offset always clamps to the four-row viewport.
  - The panel and manifold packet share one visibility edge and are cleared
    together before any retarget, aftermath, return, failure, or scene reset.
  - Only the current generation may reveal detail or complete a return.
  - Forward and reverse transport round symmetrically and reach the exact target
    only at eased progress 1, independent of render cadence.
  - Planning publishes pause actions but never mutates Replay or camera owners directly.

Related:
  - SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - SkullbonezSource/Physics/PhysicsDebugData.h
  - SkullbonezSource/Rendering/ContactManifoldPresentation.h
*/
#include "ReplayCauseInspection.h"

#include "../../Core/Profiler.h"
#include "../Prediction/ReplayPredictionView.h"
#include "../Replay/ReplayRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr const char* REPLAY_FRAME_EXPIRED_FEEDBACK = "Replay frame expired";
constexpr const char* SOLVER_DETAIL_UNAVAILABLE_FEEDBACK = "Solver detail not available";
constexpr double REPLAY_CAUSE_TRANSITION_SECONDS = 1.5;

bool BodyPairMatches( int candidateA, int candidateB, int bodyA, int bodyB, bool terrain ) noexcept
{
    if ( terrain )
    {
        return candidateA == bodyA && candidateB < 0;
    }

    return ( candidateA == bodyA && candidateB == bodyB ) || ( candidateA == bodyB && candidateB == bodyA );
}

bool ContactPairMatches( const Physics::PhysicsSolverPersistentContactSample& contact, int bodyA, int bodyB,
                         bool terrain ) noexcept
{
    return contact.isTerrain == terrain && BodyPairMatches( contact.bodyA, contact.bodyB, bodyA, bodyB, terrain );
}

bool IsSolverDetailPipelineStage( Physics::PhysicsPipelineStage stage ) noexcept
{
    switch ( stage )
    {
    case Physics::PhysicsPipelineStage::ManifoldRow:
    case Physics::PhysicsPipelineStage::WarmStart:
    case Physics::PhysicsPipelineStage::SolverIteration:
    case Physics::PhysicsPipelineStage::VelocityWriteback:
    case Physics::PhysicsPipelineStage::PositionCorrection:
    case Physics::PhysicsPipelineStage::CacheStore:
        return true;
    default:
        return false;
    }
}

bool PipelineRecordMatches( const ReplayCauseSolverDetailResult& result,
                            const Physics::PhysicsPipelineRecord& record ) noexcept
{
    if ( !IsSolverDetailPipelineStage( record.stage ) )
    {
        return false;
    }

    if ( record.stage == Physics::PhysicsPipelineStage::VelocityWriteback )
    {
        return record.bodyA == result.bodyA || ( !result.terrain && record.bodyA == result.bodyB );
    }

    if ( !BodyPairMatches( record.bodyA, record.bodyB, result.bodyA, result.bodyB, result.terrain ) )
    {
        return false;
    }

    for ( const Physics::PhysicsSolverPersistentContactSample& contact : result.sourceContacts )
    {
        if ( ContactPairMatches( contact, result.bodyA, result.bodyB, result.terrain ) &&
             contact.featureId == record.featureId )
        {
            return true;
        }
    }

    return false;
}

bool PointInside( const UI::UIRect& rect, int x, int y ) noexcept
{
    const float pointX = static_cast<float>( x );
    const float pointY = static_cast<float>( y );
    return pointX >= rect.x && pointX <= rect.x + rect.w && pointY >= rect.y && pointY <= rect.y + rect.h;
}
} // namespace

int ReplayCauseSolverDetailIterationCount( const ReplayCauseInspectionView& inspection, std::size_t contactRow ) noexcept
{
    if ( contactRow >= inspection.solverDetailContacts.size() )
    {
        return 0;
    }

    const uint32_t featureId = inspection.solverDetailContacts[contactRow].featureId;
    int count = 0;

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.stage == Physics::PhysicsPipelineStage::SolverIteration && record.featureId == featureId )
        {
            ++count;
        }
    }

    return count;
}

ReplayCauseSolverPanelRowText BuildReplayCauseSolverPanelRowText( const ReplayCauseInspectionView& inspection,
                                                                  int rowIndex ) noexcept
{
    ReplayCauseSolverPanelRowText text;

    if ( rowIndex < 0 || static_cast<std::size_t>( rowIndex ) >= inspection.solverDetailContacts.size() )
    {
        return text;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        contact = inspection.solverDetailContacts[static_cast<std::size_t>( rowIndex )];
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    const bool hasPresentationPoint = static_cast<std::size_t>( rowIndex ) < inspection.contactPresentation.pointCount;

    if ( hasPresentationPoint )
    {
        point = inspection.contactPresentation.points[static_cast<std::size_t>( rowIndex )].point;
    }

    float previousNormalImpulse = 0.0f;
    bool hasPreviousNormalImpulse = false;

    for ( const Physics::PhysicsPipelineRecord& record : inspection.solverDetailPipelineRecords )
    {
        if ( record.featureId != contact.featureId )
        {
            continue;
        }

        if ( record.stage == Physics::PhysicsPipelineStage::ManifoldRow && !hasPresentationPoint )
        {
            point = record.point;
        }
        else if ( record.stage == Physics::PhysicsPipelineStage::WarmStart && !hasPreviousNormalImpulse )
        {
            previousNormalImpulse = record.scalarB;
            hasPreviousNormalImpulse = true;
        }
    }

    sprintf_s( text.headline, sizeof( text.headline ), "ROW %d  FEATURE %u  BODIES %d / %d  POINT (%.4f, %.4f, %.4f)",
               rowIndex, contact.featureId, contact.bodyA, contact.bodyB, point.x, point.y, point.z );
    sprintf_s( text.basis, sizeof( text.basis ), "n (%.4f %.4f %.4f)  t1 (%.4f %.4f %.4f)  t2 (%.4f %.4f %.4f)",
               contact.normal.x, contact.normal.y, contact.normal.z, contact.tangent1.x, contact.tangent1.y,
               contact.tangent1.z, contact.tangent2.x, contact.tangent2.y, contact.tangent2.z );
    sprintf_s( text.geometry, sizeof( text.geometry ), "rA (%.4f %.4f %.4f)  rB (%.4f %.4f %.4f)  penetration %.5f",
               contact.rA.x, contact.rA.y, contact.rA.z, contact.rB.x, contact.rB.y, contact.rB.z, contact.penetration );
    sprintf_s( text.masses, sizeof( text.masses ),
               "normalMass %.5f  tangentMass (%.5f, %.5f)  bias %.5f  frictionLimit %.5f", contact.normalMass,
               contact.tangentMass1, contact.tangentMass2, contact.bias, contact.frictionLimit );
    sprintf_s( text.impulses, sizeof( text.impulses ),
               "accN %.5f  accT1 %.5f  accT2 %.5f  warm-start %s  previous normal impulse %.5f", contact.accN, contact.accT1,
               contact.accT2, contact.warmStarted ? "YES" : "NO", previousNormalImpulse );
    return text;
}


bool ShouldBeginReplayCauseAftermath( const ReplayCauseInspectionView& inspection, bool spaceDown ) noexcept
{
    return spaceDown && inspection.mode == ReplayCauseInspectionMode::DetailPaused;
}


bool ShouldBeginReplayCauseReturn( const ReplayCauseInspectionView& inspection, bool nonSelectionClick,
                                   bool scrubExit ) noexcept
{
    return inspection.mode != ReplayCauseInspectionMode::Inactive &&
           ( nonSelectionClick || scrubExit || inspection.mode == ReplayCauseInspectionMode::Returning );
}

ReplayCauseSolverPanelLayout BuildReplayCauseSolverPanelLayout( const ReplayCauseInspectionView& inspection,
                                                                const RunReplayCauseTreeState& causeTree, int screenWidth,
                                                                int screenHeight ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PanelLayout" );
    ReplayCauseSolverPanelLayout layout;
    int maximumIterations = 0;

    for ( std::size_t row = 0; row < inspection.solverDetailContacts.size(); ++row )
    {
        maximumIterations = (std::max)( maximumIterations, ReplayCauseSolverDetailIterationCount( inspection, row ) );
    }

    const int iterationLines = ( maximumIterations + REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE - 1 ) /
                               REPLAY_CAUSE_SOLVER_PANEL_ITERATIONS_PER_LINE;
    layout.rowHeight = REPLAY_CAUSE_SOLVER_PANEL_BASE_ROW_HEIGHT +
                       static_cast<float>( iterationLines ) * REPLAY_CAUSE_SOLVER_PANEL_ITERATION_LINE_HEIGHT;

    const float margin = 8.0f;
    const float gap = 10.0f;
    const float panelWidth = (std::min)( REPLAY_CAUSE_SOLVER_PANEL_WIDTH,
                                         static_cast<float>( (std::max)( 1, screenWidth ) ) - margin * 2.0f );
    const float panelHeight = REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + REPLAY_CAUSE_SOLVER_PANEL_GUIDE_HEIGHT +
                              layout.rowHeight * static_cast<float>( REPLAY_CAUSE_SOLVER_PANEL_VISIBLE_ROWS ) + 12.0f;
    const float leftX = static_cast<float>( causeTree.x ) - gap - panelWidth;
    const float rightX = static_cast<float>( causeTree.x + causeTree.width ) + gap;
    float panelX = leftX >= margin ? leftX : rightX;

    if ( panelX + panelWidth > static_cast<float>( screenWidth ) - margin )
    {
        panelX = (std::max)( margin, static_cast<float>( screenWidth ) - margin - panelWidth );
    }

    const float maximumY = (std::max)( margin, static_cast<float>( screenHeight ) - margin - panelHeight );
    const float panelY = std::clamp( static_cast<float>( causeTree.y ), margin, maximumY );
    layout.panel = { panelX, panelY, panelWidth, panelHeight };
    layout.content = { panelX + 8.0f,
                       panelY + REPLAY_CAUSE_SOLVER_PANEL_TITLE_HEIGHT + REPLAY_CAUSE_SOLVER_PANEL_GUIDE_HEIGHT,
                       panelWidth - 16.0f, layout.rowHeight * static_cast<float>( REPLAY_CAUSE_SOLVER_PANEL_VISIBLE_ROWS ) };
    return layout;
}

float EvaluateReplayCauseTransitionProgress( double elapsedSeconds ) noexcept
{
    const double u = std::clamp( elapsedSeconds / REPLAY_CAUSE_TRANSITION_SECONDS, 0.0, 1.0 );
    const double remaining = 1.0 - u;
    return static_cast<float>( 1.0 - remaining * remaining * remaining );
}

ReplayFrameIndex EvaluateReplayCauseTransitionFrame( ReplayFrameIndex sourceFrame, ReplayFrameIndex targetFrame,
                                                     float easedProgress ) noexcept
{
    const double progress = std::clamp( static_cast<double>( easedProgress ), 0.0, 1.0 );

    if ( progress >= 1.0 )
    {
        return targetFrame;
    }

    if ( sourceFrame <= targetFrame )
    {
        const ReplayFrameIndex distance = targetFrame - sourceFrame;
        return sourceFrame + static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
    }

    const ReplayFrameIndex distance = sourceFrame - targetFrame;
    return sourceFrame - static_cast<ReplayFrameIndex>( static_cast<double>( distance ) * progress );
}

bool ReplayCauseSeekResult::CanTransport() const noexcept
{
    return availability == ReplayCauseSeekAvailability::Available;
}

const char* ReplayCauseSeekResult::Feedback() const noexcept
{
    return CanTransport() ? "" : REPLAY_FRAME_EXPIRED_FEEDBACK;
}

bool ReplayCauseSolverDetailResult::HasDetail() const noexcept
{
    return availability == ReplayCauseSolverDetailAvailability::Available;
}

const Physics::PhysicsSolverPersistentContactSample*
ReplayCauseSolverDetailResult::ContactRowAt( std::size_t detailRow ) const noexcept
{
    if ( !HasDetail() )
    {
        return nullptr;
    }

    for ( const Physics::PhysicsSolverPersistentContactSample& contact : sourceContacts )
    {
        if ( !ContactPairMatches( contact, bodyA, bodyB, terrain ) )
        {
            continue;
        }

        if ( detailRow == 0u )
        {
            return &contact;
        }

        --detailRow;
    }

    return nullptr;
}

const Physics::PhysicsPipelineRecord*
ReplayCauseSolverDetailResult::PipelineRecordAt( std::size_t detailRecord ) const noexcept
{
    if ( !HasDetail() )
    {
        return nullptr;
    }

    for ( const Physics::PhysicsPipelineRecord& record : sourcePipelineRecords )
    {
        if ( !PipelineRecordMatches( *this, record ) )
        {
            continue;
        }

        if ( detailRecord == 0u )
        {
            return &record;
        }

        --detailRecord;
    }

    return nullptr;
}

ReplayCauseSolverDetailResult EvaluateReplayCauseSolverDetail( const RunReplayCauseTreeRow& row,
                                                               const ReplayCauseSeekResult& seek,
                                                               const ReplayCauseSolverDetailSource& source ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/SolverDetailLookup" );
    ReplayCauseSolverDetailResult result;
    result.frame = row.firstFrame;

    if ( !seek.CanTransport() )
    {
        result.availability = ReplayCauseSolverDetailAvailability::ReplayFrameExpired;
        return result;
    }

    // Hazard: the currently visible or nearest retained diagnostics can look
    // structurally identical. Only the explicit frame stamp licenses a join.
    if ( seek.frame != row.firstFrame || seek.source != ReplayCauseSeekSource::SolverHistory || row.prediction ||
         source.frame != row.firstFrame ||
         ( row.kind != RunReplayCauseTreeRowKind::Manifold && row.kind != RunReplayCauseTreeRowKind::SolverRow ) ||
         row.contactIndex < 0 || static_cast<std::size_t>( row.contactIndex ) >= source.contacts.size() )
    {
        return result;
    }

    const Physics::PhysicsSolverPersistentContactSample&
        anchor = source.contacts[static_cast<std::size_t>( row.contactIndex )];
    const bool anchorTerrain = anchor.isTerrain || anchor.bodyB < 0;
    const bool focusedBodyMatches = row.modelRow.value == anchor.bodyA || row.modelRow.value == anchor.bodyB;
    const int anchorOtherBody = row.modelRow.value == anchor.bodyA ? anchor.bodyB : anchor.bodyA;

    if ( !focusedBodyMatches || row.featureId < 0 || static_cast<uint32_t>( row.featureId ) != anchor.featureId ||
         row.terrain != anchorTerrain ||
         ( anchorTerrain ? row.counterpartModelRow.value >= 0 : row.counterpartModelRow.value != anchorOtherBody ) )
    {
        return result;
    }

    result.sourceContacts = source.contacts;
    result.sourcePipelineRecords = source.pipelineRecords;
    result.bodyA = anchor.bodyA;
    result.bodyB = anchor.bodyB;
    result.terrain = anchorTerrain;

    for ( const Physics::PhysicsSolverPersistentContactSample& contact : result.sourceContacts )
    {
        if ( ContactPairMatches( contact, result.bodyA, result.bodyB, result.terrain ) )
        {
            ++result.contactRowCount;
        }
    }

    if ( result.contactRowCount == 0u )
    {
        return ReplayCauseSolverDetailResult { .frame = row.firstFrame };
    }

    for ( const Physics::PhysicsPipelineRecord& record : result.sourcePipelineRecords )
    {
        if ( PipelineRecordMatches( result, record ) )
        {
            ++result.pipelineRecordCount;
        }
    }

    result.availability = ReplayCauseSolverDetailAvailability::Available;
    return result;
}

Rendering::ContactManifoldPresentation BuildReplayCauseContactPresentation( const ReplayCauseSolverDetailResult& detail,
                                                                            const ReplaySolverFrameSample& sample ) noexcept
{
    PROFILE_SCOPED( "Frame/Replay/CauseInspection/ManifoldPresentation" );
    Rendering::ContactManifoldPresentation presentation;

    if ( !detail.HasDetail() || sample.frameIndex != detail.frame || detail.contactRowCount == 0u )
    {
        return presentation;
    }

    auto findBody = [&]( int modelRow ) -> const ReplaySolverBodySample*
    {
        const auto found = std::find_if( sample.bodies.begin(), sample.bodies.end(),
                                         [&]( const ReplaySolverBodySample& body )
                                         { return body.modelRow.value == modelRow; } );
        return found == sample.bodies.end() ? nullptr : &*found;
    };

    auto publishBody = [&]( int modelRow, std::size_t presentationIndex ) -> bool
    {
        const ReplaySolverBodySample* body = findBody( modelRow );

        if ( !body )
        {
            return false;
        }

        Rendering::ContactBodyPosePresentation& pose = presentation.bodies[presentationIndex];
        pose.position = body->position;
        pose.orientation = Math::Orientation::Quaternion( body->orientation[0], body->orientation[1], body->orientation[2],
                                                          body->orientation[3] );
        pose.valid = true;
        ++presentation.bodyCount;
        return true;
    };

    if ( !publishBody( detail.bodyA, 0u ) || ( !detail.terrain && !publishBody( detail.bodyB, 1u ) ) )
    {
        return Rendering::ContactManifoldPresentation {};
    }

    // Invariant: Rendering receives a truthful bounded prefix. The explicit
    // truncation bit prevents eight presented points from claiming a larger
    // retained patch was complete.
    const std::size_t presentedContactCount = (std::min)( detail.contactRowCount,
                                                          Rendering::CONTACT_MANIFOLD_PRESENTATION_POINT_CAPACITY );
    presentation.truncated = detail.contactRowCount > presentedContactCount;

    for ( std::size_t contactIndex = 0; contactIndex < presentedContactCount; ++contactIndex )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = detail.ContactRowAt( contactIndex );

        if ( !contact )
        {
            return Rendering::ContactManifoldPresentation {};
        }

        const Physics::PhysicsPipelineRecord* manifoldRecord = nullptr;

        // Prefer the original narrowphase point when that bounded record still
        // exists. A missing record permits only the surviving solver row's own
        // pose-plus-arm point; discarded candidates are never reconstructed.
        for ( std::size_t pipelineIndex = 0; pipelineIndex < detail.pipelineRecordCount; ++pipelineIndex )
        {
            const Physics::PhysicsPipelineRecord* candidate = detail.PipelineRecordAt( pipelineIndex );

            if ( candidate && candidate->stage == Physics::PhysicsPipelineStage::ManifoldRow &&
                 candidate->featureId == contact->featureId )
            {
                manifoldRecord = candidate;
                break;
            }
        }

        Rendering::ContactPointPresentation& point = presentation.points[contactIndex];
        const ReplaySolverBodySample* contactBodyA = findBody( contact->bodyA );

        if ( !manifoldRecord && !contactBodyA )
        {
            return Rendering::ContactManifoldPresentation {};
        }

        point.point = manifoldRecord ? manifoldRecord->point : contactBodyA->position + contact->rA;
        point.normal = manifoldRecord ? manifoldRecord->normal
                                      : ( contact->isTerrain ? contact->terrainNormal : contact->normal );
        point.tangent1 = contact->tangent1;
        point.tangent2 = contact->tangent2;
        point.penetration = manifoldRecord ? manifoldRecord->scalarA : contact->penetration;
        point.exactSourcePoint = manifoldRecord != nullptr;
        ++presentation.pointCount;
    }

    return presentation;
}

ReplayCauseSeekResult EvaluateReplayCauseSeek( const RunReplayCauseTreeRow& row, const ReplayRecorderStats& solverStats,
                                               std::span<const RunReplayPredictionFrame> predictionFrames ) noexcept
{
    ReplayCauseSeekResult result;
    result.frame = row.firstFrame;
    result.source = row.prediction ? ReplayCauseSeekSource::Prediction : ReplayCauseSeekSource::SolverHistory;

    if ( row.prediction )
    {
        const auto match = std::find_if( predictionFrames.begin(), predictionFrames.end(),
                                         [&]( const auto& frame ) { return frame.frameIndex == row.firstFrame; } );

        if ( match != predictionFrames.end() )
        {
            result.availability = ReplayCauseSeekAvailability::Available;
        }

        return result;
    }

    const ReplayFrameIndex retainedCount = static_cast<ReplayFrameIndex>( solverStats.sampleCount );
    const ReplayFrameIndex oldestFrame = solverStats.nextFrameIndex > retainedCount
                                             ? solverStats.nextFrameIndex - retainedCount
                                             : 0;

    if ( solverStats.sampleCount > 0 && row.firstFrame >= oldestFrame && row.firstFrame < solverStats.nextFrameIndex )
    {
        result.availability = ReplayCauseSeekAvailability::Available;
    }

    return result;
}

bool ReplayCauseInspection::Select( int rowIndex, const ReplayCauseSeekResult& seek, ReplayFrameIndex presentedFrame,
                                    bool simulationAlreadyPaused, double nowSeconds ) noexcept
{
    if ( rowIndex < 0 || !seek.CanTransport() )
    {
        return false;
    }

    // Invariant: zero is reserved for "no request" at the App boundary. A
    // wrap restarts at one so even a process-lifetime session keeps that seam.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive )
    {
        m_state.ownsPause = !simulationAlreadyPaused;
    }
    else
    {
        // Direct retargeting from aftermath reacquires only the pause that this
        // inspection itself caused. A pre-existing operator pause stays external.
        m_state.ownsPause = m_state.ownsPause || !simulationAlreadyPaused;
    }

    m_state.mode = ReplayCauseInspectionMode::Transporting;
    m_state.sourceFrame = presentedFrame;
    m_state.targetFrame = seek.frame;
    m_state.presentedFrame = presentedFrame;
    m_state.seekSource = seek.source;
    m_state.selectedRow = rowIndex;
    ClearFocusedSurface();
    m_state.transportPending = false;
    m_state.easedProgress = 0.0f;
    m_startedAtSeconds = nowSeconds;
    m_pendingFrame = presentedFrame;
    return true;
}

void ReplayCauseInspection::Advance( double nowSeconds ) noexcept
{
    if ( m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return;
    }

    // Invariant: total elapsed time, not prior progress, owns the curve. A
    // backwards host clock clamps to the source rather than reversing a turn.
    m_state.easedProgress = EvaluateReplayCauseTransitionProgress( nowSeconds - m_startedAtSeconds );
    const ReplayFrameIndex requested = EvaluateReplayCauseTransitionFrame( m_state.sourceFrame, m_state.targetFrame,
                                                                           m_state.easedProgress );

    if ( requested != m_state.presentedFrame && ( !m_state.transportInFlight || requested != m_inFlightFrame ) )
    {
        // Coalescing replaces an obsolete not-yet-issued intermediate frame.
        m_pendingFrame = requested;
        m_state.transportPending = true;
    }

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportInFlight &&
         !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::TakeTransportRequest( ReplayCauseTransportRequest& outRequest ) noexcept
{
    if ( !m_state.transportPending || m_state.transportInFlight || m_state.mode != ReplayCauseInspectionMode::Transporting )
    {
        return false;
    }

    outRequest.generation = m_state.generation;
    outRequest.sourceFrame = m_state.sourceFrame;
    outRequest.targetFrame = m_pendingFrame;
    outRequest.source = m_state.seekSource;
    m_state.transportPending = false;
    m_state.transportInFlight = true;
    m_inFlightGeneration = outRequest.generation;
    m_inFlightFrame = outRequest.targetFrame;
    m_state.transportFrame = outRequest.targetFrame;
    return true;
}

void ReplayCauseInspection::PublishSolverDetail( uint64_t generation, const ReplayCauseSolverDetailResult& detail,
                                                 const Rendering::ContactManifoldPresentation& contactPresentation ) noexcept
{
    if ( generation == 0u || generation != m_state.generation || detail.frame != m_state.targetFrame )
    {
        return;
    }

    // Lifetime: copy every bounded row before the restore retires its source
    // sample. The published spans point only into this Planning owner.
    m_state.solverDetailAvailability = detail.availability;
    m_state.solverDetailFeedback = detail.Feedback();
    m_state.solverDetailContactRowCount = 0u;
    m_state.solverDetailPipelineRecordCount = 0u;
    m_state.solverDetailContacts = {};
    m_state.solverDetailPipelineRecords = {};
    m_state.solverDetailFirstRow = 0;
    m_state.contactPresentation = {};

    if ( !detail.HasDetail() || detail.contactRowCount > m_solverDetailContacts.size() ||
         detail.pipelineRecordCount > m_solverDetailPipelineRecords.size() )
    {
        return;
    }

    for ( std::size_t row = 0; row < detail.contactRowCount; ++row )
    {
        const Physics::PhysicsSolverPersistentContactSample* contact = detail.ContactRowAt( row );

        if ( !contact )
        {
            m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
            m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
            return;
        }

        m_solverDetailContacts[row] = *contact;
    }

    for ( std::size_t recordIndex = 0; recordIndex < detail.pipelineRecordCount; ++recordIndex )
    {
        const Physics::PhysicsPipelineRecord* record = detail.PipelineRecordAt( recordIndex );

        if ( !record )
        {
            m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
            m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
            return;
        }

        m_solverDetailPipelineRecords[recordIndex] = *record;
    }

    m_state.solverDetailContactRowCount = detail.contactRowCount;
    m_state.solverDetailPipelineRecordCount = detail.pipelineRecordCount;
    m_state.solverDetailContacts = std::span<const Physics::PhysicsSolverPersistentContactSample>( m_solverDetailContacts
                                                                                                       .data(),
                                                                                                   detail.contactRowCount );
    m_state
        .solverDetailPipelineRecords = std::span<const Physics::PhysicsPipelineRecord>( m_solverDetailPipelineRecords.data(),
                                                                                        detail.pipelineRecordCount );
    m_state.contactPresentation = contactPresentation;
}

void ReplayCauseInspection::CompleteTransport( uint64_t generation, bool succeeded ) noexcept
{
    if ( generation == 0 || generation != m_inFlightGeneration )
    {
        return;
    }

    m_state.transportInFlight = false;
    m_inFlightGeneration = 0;

    // Hazard: a newer row may have been selected while this restore was in
    // flight. Its pending generation owns the next mutation; the old completion
    // must neither expose detail nor unwind the inspection pause.
    if ( generation != m_state.generation )
    {
        return;
    }

    if ( !succeeded )
    {
        m_state.mode = ReplayCauseInspectionMode::Returning;
        ClearFocusedSurface();
        return;
    }

    m_state.presentedFrame = m_inFlightFrame;

    if ( m_state.easedProgress >= 1.0f && m_state.presentedFrame == m_state.targetFrame && !m_state.transportPending )
    {
        m_state.mode = ReplayCauseInspectionMode::DetailPaused;
        m_state.detailVisible = true;
    }
}

bool ReplayCauseInspection::BeginAftermath( bool& outReleasePause ) noexcept
{
    outReleasePause = false;

    if ( m_state.mode != ReplayCauseInspectionMode::DetailPaused )
    {
        return false;
    }

    m_state.mode = ReplayCauseInspectionMode::AftermathFollow;
    ClearFocusedSurface();
    outReleasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    return true;
}

ReplayCauseExitAction ReplayCauseInspection::BeginReturn() noexcept
{
    ReplayCauseExitAction action;

    if ( m_state.mode == ReplayCauseInspectionMode::Inactive || m_state.returnIssued )
    {
        return action;
    }

    action.apply = true;
    action.releasePause = m_state.ownsPause;
    m_state.ownsPause = false;
    ClearFocusedSurface();
    m_state.transportPending = false;
    m_state.mode = ReplayCauseInspectionMode::Returning;
    m_state.returnIssued = true;

    // Invalidate a synchronous completion that arrives after cancellation.
    ++m_state.generation;

    if ( m_state.generation == 0 )
    {
        m_state.generation = 1;
    }

    return action;
}

void ReplayCauseInspection::CompleteReturn() noexcept
{
    if ( m_state.mode == ReplayCauseInspectionMode::Returning && !m_state.transportInFlight )
    {
        Reset();
    }
}

bool ReplayCauseInspection::TickSolverDetailPanelInput( const RunReplayCauseTreeState& causeTree, int mouseX, int mouseY,
                                                        bool hasClientPosition, bool pointerBlocked, int wheelDelta,
                                                        int screenWidth, int screenHeight ) noexcept
{
    if ( !m_state.detailVisible || !hasClientPosition || pointerBlocked || screenWidth <= 0 || screenHeight <= 0 )
    {
        return false;
    }

    PROFILE_SCOPED( "Frame/Replay/CauseInspection/PanelInput" );
    const ReplayCauseSolverPanelLayout layout = BuildReplayCauseSolverPanelLayout( m_state, causeTree, screenWidth,
                                                                                   screenHeight );

    if ( !PointInside( layout.panel, mouseX, mouseY ) )
    {
        return false;
    }

    if ( wheelDelta != 0 && !m_state.solverDetailContacts.empty() )
    {
        const int direction = wheelDelta > 0 ? -1 : 1;
        const int wheelSteps = (std::max)( 1, std::abs( wheelDelta ) / 120 );
        const int maximumFirstRow = (std::max)( 0, static_cast<int>( m_state.solverDetailContacts.size() ) -
                                                       REPLAY_CAUSE_SOLVER_PANEL_VISIBLE_ROWS );
        m_state.solverDetailFirstRow = std::clamp( m_state.solverDetailFirstRow + direction * wheelSteps, 0,
                                                   maximumFirstRow );
    }

    return true;
}

void ReplayCauseInspection::Reset() noexcept
{
    m_state = ReplayCauseInspectionView {};
    m_startedAtSeconds = 0.0;
    m_pendingFrame = 0;
    m_inFlightFrame = 0;
    m_inFlightGeneration = 0;
}

void ReplayCauseInspection::ClearFocusedSurface() noexcept
{
    // Lifetime: fixed backing arrays remain allocated, but no stale row is
    // reachable once its synchronous spans and paired Rendering packet clear.
    m_state.solverDetailAvailability = ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable;
    m_state.solverDetailContactRowCount = 0u;
    m_state.solverDetailPipelineRecordCount = 0u;
    m_state.solverDetailContacts = {};
    m_state.solverDetailPipelineRecords = {};
    m_state.solverDetailFeedback = SOLVER_DETAIL_UNAVAILABLE_FEEDBACK;
    m_state.solverDetailFirstRow = 0;
    m_state.contactPresentation = {};
    m_state.detailVisible = false;
}

ReplayCauseInspectionView ReplayCauseInspection::View() const noexcept
{
    return m_state;
}
} // namespace SkullbonezCore::Runtime
