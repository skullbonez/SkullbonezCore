/*
File: SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp
Purpose:
  Serializes bounded interaction-probe evidence for Automation-only launches.

Summary:
  Shared report-fact functions serve both live assertions and final JSON, so
  validation-sensitive calculations have one implementation.
  Runtime owners are borrowed synchronously through one report call. The
  writer computes report facts, verifies any durable replay artifact, writes
  JSON, then releases every borrow before returning.
  Prediction witnesses serialize complete bounded trajectory, future-node, and
  cause-row topology, exact evidence stamps, and solver anchors so root-policy
  or stale-bank regressions are reviewable without inferring structure from a
  screenshot. Cause-inspector reports serialize the same compound control
  rectangles consumed by drawing and pointer routing.

Glossary:
  Report fact: Derived validation value shared by live assertions and final JSON.
  RVIS (Replay Visual Instance State): Ordered visual packet rows stored beside
    replay samples for offline fidelity verification.

Invariants:
  - JSON field names and replay byte/order calculations are validation contracts.
  - Prediction reports and fidelity capture never read beyond the committed prefix.
  - Topology witness rows preserve lane, identity, ancestry, depth, and first-frame
    fields in publication order; reports do not synthesize missing relationships.
  - Exact predicted Manifold and SolverRow reports preserve the immutable-bank
    identity and contact/pipeline indices used by Planning inspection.
  - Runtime-owner references are never stored on the writer.
  - A report failure never replaces an earlier probe failure.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h
  - SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp
  - tools/validate_replay_visual_fidelity.bat
  - Agentic/Reference/engine-glossary.md
*/
#include "InteractionAutomationReportWriter.h"
#include "InteractionAutomationController.h"

#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../Editor/EditorTools.h"
#include "../Prediction/ReplayPrediction.h"
#include "../Prediction/ReplayPredictionArchive.h"
#include "../Planning/ReplayCauseInspection.h"

#include "../Replay/ReplayPresentation.h"
#include "../Replay/ReplayV2Artifact.h"
#include "../Camera/CameraControlState.h"
#include "../Tools/RuntimeFileWriter.h"
#include "../Scene/SceneSessionState.h"
#include "../Scene/SceneWorld.h"
#include "../Tools/RuntimeTools.h"

#include "../../Physics/PhysicsEngine.h"
#include "../../Core/ByteView.h"
#include "../../Rendering/RenderSceneSnapshot.h"
#include "../../UI/UI.h"

#pragma warning( push, 0 )
#include "../../../ThirdPtySource/nlohmann/json.hpp"
#pragma warning( pop )

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <sstream>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionArchiveOperations;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
using namespace SkullbonezCore::Runtime::ReplayVisualPacketFingerprintOperations;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
namespace Physics = SkullbonezCore::Physics;
namespace Rendering = SkullbonezCore::Rendering;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
using Json = nlohmann::ordered_json;
constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_OFFSET = 1469598103934665603ull;
constexpr uint64_t INTERACTION_PREDICTION_FINGERPRINT_PRIME = 1099511628211ull;

Json Vec3Json( const Vector3& value )
{
    return Json::array( { value.x, value.y, value.z } );
}

// Concept: enum spellings are part of the JSON witness schema. These helpers
// keep topology rows readable while preserving a stable closed mapping.
const char* ReplayTrajectoryLaneName( ReplayTrajectoryLane lane ) noexcept
{
    switch ( lane )
    {
    case ReplayTrajectoryLane::PastRoot:
        return "PastRoot";
    case ReplayTrajectoryLane::FutureRoot:
        return "FutureRoot";
    case ReplayTrajectoryLane::FutureChildIncoming:
        return "FutureChildIncoming";
    case ReplayTrajectoryLane::FutureChildOutgoing:
        return "FutureChildOutgoing";
    case ReplayTrajectoryLane::RetainedTrail:
        return "RetainedTrail";
    case ReplayTrajectoryLane::BaselineRoot:
        return "BaselineRoot";
    }

    return "Unknown";
}

const char* ReplayCauseTreeRowKindName( RunReplayCauseTreeRowKind kind ) noexcept
{
    switch ( kind )
    {
    case RunReplayCauseTreeRowKind::Body:
        return "Body";
    case RunReplayCauseTreeRowKind::Manifold:
        return "Manifold";
    case RunReplayCauseTreeRowKind::SolverRow:
        return "SolverRow";
    case RunReplayCauseTreeRowKind::PredictionContact:
        return "PredictionContact";
    case RunReplayCauseTreeRowKind::PredictionMotion:
        return "PredictionMotion";
    }

    return "Unknown";
}

const char* ReplayCauseSolverDetailAvailabilityName( ReplayCauseSolverDetailAvailability availability ) noexcept
{
    switch ( availability )
    {
    case ReplayCauseSolverDetailAvailability::Available:
        return "Available";
    case ReplayCauseSolverDetailAvailability::SolverDetailNotAvailable:
        return "SolverDetailNotAvailable";
    case ReplayCauseSolverDetailAvailability::ReplayFrameExpired:
        return "ReplayFrameExpired";
    }

    return "Unknown";
}

void HashPredictionByte( uint64_t& hash, uint8_t value )
{
    hash ^= static_cast<uint64_t>( value );
    hash *= INTERACTION_PREDICTION_FINGERPRINT_PRIME;
}

template <typename T> void HashPredictionScalar( uint64_t& hash, T value )
{
    for ( uint8_t byte : SkullbonezCore::Core::ObjectBytes( value ) )
    {
        HashPredictionByte( hash, byte );
    }
}

void HashPredictionFloat( uint64_t& hash, float value )
{
    uint32_t bits = 0;
    std::memcpy( &bits, &value, sizeof( bits ) );
    HashPredictionScalar( hash, bits );
}

void HashPredictionVector( uint64_t& hash, const Vector3& value )
{
    HashPredictionFloat( hash, value.x );
    HashPredictionFloat( hash, value.y );
    HashPredictionFloat( hash, value.z );
}

void HashPredictionContact( uint64_t& hash, const Physics::PhysicsDebugContact& contact )
{
    // Invariant: this is the single ordered schema for a captured contact row.
    // Aggregate and contact-only witnesses both call it so adding a diagnostic
    // field cannot silently make the two channels disagree about completeness.
    HashPredictionScalar( hash, contact.bodyA );
    HashPredictionScalar( hash, contact.bodyB );
    HashPredictionScalar( hash, contact.featureId );
    HashPredictionVector( hash, contact.point );
    HashPredictionVector( hash, contact.normal );
    HashPredictionVector( hash, contact.tangent1 );
    HashPredictionVector( hash, contact.tangent2 );
    HashPredictionFloat( hash, contact.penetration );
    HashPredictionFloat( hash, contact.normalImpulse );
    HashPredictionFloat( hash, contact.separationBias );
    HashPredictionFloat( hash, contact.preSolveNormalSpeed );
    HashPredictionFloat( hash, contact.preSolveClosingSpeed );
    HashPredictionFloat( hash, contact.preSolveSlipSpeed );
}

// Concept: these hashes isolate private-simulation results from presentation
// timing and evidence-retention policy. The frame index is the deterministic
// horizon clock; absolute simulationSeconds includes the process-local source
// timer and is reported separately instead of contaminating the value witness.
// Matching runs must produce the same body/contact rows even when their worker
// slices finish on different wall-clock frames.
// Invariant: one Build traversal updates the aggregate witness and every
// component witness from the same ordered frame/body/contact rows. The
// worker-count determinism tool compares every channel so a partial update
// cannot make the aggregate hash look trustworthy by itself.
class PredictionSimulationHashes
{
  public:
    static PredictionSimulationHashes Build( std::span<const RunReplayPredictionFrame> frames )
    {
        PredictionSimulationHashes hashes;
        HashPredictionScalar( hashes.m_all, frames.size() );
        HashPredictionScalar( hashes.m_frameIndex, frames.size() );
        HashPredictionScalar( hashes.m_pose, frames.size() );
        HashPredictionScalar( hashes.m_velocity, frames.size() );
        HashPredictionScalar( hashes.m_sleep, frames.size() );
        HashPredictionScalar( hashes.m_contacts, frames.size() );

        for ( const RunReplayPredictionFrame& frame : frames )
        {
            HashPredictionScalar( hashes.m_all, frame.frameIndex );
            HashPredictionFloat( hashes.m_all, frame.tornadoSystemElapsedSeconds );
            HashPredictionScalar( hashes.m_all, frame.bodies.size() );
            HashPredictionScalar( hashes.m_frameIndex, frame.frameIndex );
            HashPredictionScalar( hashes.m_pose, frame.frameIndex );
            HashPredictionScalar( hashes.m_pose, frame.bodies.size() );
            HashPredictionScalar( hashes.m_velocity, frame.frameIndex );
            HashPredictionScalar( hashes.m_velocity, frame.bodies.size() );
            HashPredictionScalar( hashes.m_sleep, frame.frameIndex );
            HashPredictionScalar( hashes.m_sleep, frame.bodies.size() );

            for ( const RunReplayPredictionBodySample& body : frame.bodies )
            {
                HashPredictionScalar( hashes.m_all, body.id.value );
                HashPredictionScalar( hashes.m_all, body.modelRow.value );
                HashPredictionVector( hashes.m_all, body.position );
                HashPredictionVector( hashes.m_all, body.linearVelocity );
                HashPredictionScalar( hashes.m_pose, body.id.value );
                HashPredictionScalar( hashes.m_pose, body.modelRow.value );
                HashPredictionVector( hashes.m_pose, body.position );
                HashPredictionScalar( hashes.m_velocity, body.id.value );
                HashPredictionScalar( hashes.m_velocity, body.modelRow.value );
                HashPredictionVector( hashes.m_velocity, body.linearVelocity );
                HashPredictionScalar( hashes.m_sleep, body.id.value );
                HashPredictionScalar( hashes.m_sleep, body.modelRow.value );
                float orientationX = 0.0f;
                float orientationY = 0.0f;
                float orientationZ = 0.0f;
                float orientationW = 1.0f;
                body.orientation.GetComponents( orientationX, orientationY, orientationZ, orientationW );
                HashPredictionFloat( hashes.m_all, orientationX );
                HashPredictionFloat( hashes.m_all, orientationY );
                HashPredictionFloat( hashes.m_all, orientationZ );
                HashPredictionFloat( hashes.m_all, orientationW );
                HashPredictionScalar( hashes.m_all, static_cast<uint8_t>( body.sleeping ) );
                HashPredictionFloat( hashes.m_pose, orientationX );
                HashPredictionFloat( hashes.m_pose, orientationY );
                HashPredictionFloat( hashes.m_pose, orientationZ );
                HashPredictionFloat( hashes.m_pose, orientationW );
                HashPredictionScalar( hashes.m_sleep, static_cast<uint8_t>( body.sleeping ) );
            }

            HashPredictionScalar( hashes.m_all, frame.debugContacts.size() );
            HashPredictionScalar( hashes.m_all, static_cast<uint8_t>( frame.contactsIncomplete ) );
            HashPredictionScalar( hashes.m_contacts, frame.frameIndex );
            HashPredictionScalar( hashes.m_contacts, frame.debugContacts.size() );
            HashPredictionScalar( hashes.m_contacts, static_cast<uint8_t>( frame.contactsIncomplete ) );

            for ( const Physics::PhysicsDebugContact& contact : frame.debugContacts )
            {
                HashPredictionContact( hashes.m_all, contact );
                HashPredictionContact( hashes.m_contacts, contact );
            }
        }

        return hashes;
    }

    uint64_t All() const noexcept
    {
        return m_all;
    }
    uint64_t FrameIndex() const noexcept
    {
        return m_frameIndex;
    }
    uint64_t Pose() const noexcept
    {
        return m_pose;
    }
    uint64_t Velocity() const noexcept
    {
        return m_velocity;
    }
    uint64_t Sleep() const noexcept
    {
        return m_sleep;
    }
    uint64_t Contacts() const noexcept
    {
        return m_contacts;
    }

  private:
    uint64_t m_all = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    uint64_t m_frameIndex = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    uint64_t m_pose = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    uint64_t m_velocity = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    uint64_t m_sleep = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
    uint64_t m_contacts = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;
};

ReplayCausalProofTick BuildReplayCausalProofTick( const ReplayVisualPacket& packet )
{
    ReplayCausalProofTick tick;
    tick.revealFrame = packet.header.revealFrame;
    tick.activeTopologyHash = INTERACTION_PREDICTION_FINGERPRINT_OFFSET;

    for ( const RunReplayPathTraceNode& node : packet.futureNodes )
    {
        if ( node.firstFrame > tick.revealFrame )
        {
            continue;
        }

        HashPredictionScalar( tick.activeTopologyHash, node.id.value );
        HashPredictionScalar( tick.activeTopologyHash, node.parentId.value );
        HashPredictionScalar( tick.activeTopologyHash, node.firstFrame );
        HashPredictionScalar( tick.activeTopologyHash, node.depth );
        HashPredictionScalar( tick.activeTopologyHash, static_cast<uint8_t>( node.contactDerived ) );
        ++tick.activeNodeCount;
    }

    for ( const ReplayTrajectoryRecord& record : packet.trajectoryRecords )
    {
        if ( record.key.lane == ReplayTrajectoryLane::PastRoot || record.firstFrame > tick.revealFrame )
        {
            continue;
        }

        const std::size_t publishedPointCount = (std::min)( record.publishedPointCount, record.points.size() );

        if ( publishedPointCount == 0 )
        {
            continue;
        }

        // The buffer oracle hashes every submitted point and vertex. This row
        // records stable eligibility/count transitions instead of internal
        // record publication order, which is not a presentation contract.
        ++tick.revealedRecordCount;
        tick.revealedPointCount += static_cast<uint32_t>( publishedPointCount );
        tick.revealedSegmentCount += static_cast<uint32_t>( publishedPointCount - 1 );
    }

    for ( const ReplayPredictionRetainedMarker& marker : packet.retainedMarkers )
    {
        tick.entryMarkerCount += marker.hasEntryPose ? 1u : 0u;
        tick.restMarkerCount += marker.hasRestPose ? 1u : 0u;
        tick.horizonMarkerCount += marker.hasHorizonPose ? 1u : 0u;
    }

    tick.ghostRequestCount = static_cast<uint32_t>( packet.ghostRequests.size() );
    return tick;
}

const RunReplayPredictionBodySample* FindPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                             Physics::PhysicsSceneObjectId id )
{
    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }

    return nullptr;
}

} // namespace

void SkullbonezCore::Runtime::InteractionAutomationRunStatus::Fail( const char* message )
{
    failed = true;

    if ( failure[0] == '\0' )
    {
        strcpy_s( failure, sizeof( failure ), message ? message : "interaction automation failed" );
    }
}

SkullbonezCore::Core::SbResult
SkullbonezCore::Runtime::InteractionAutomationRunStatus::Result( Core::SbDiagnosticStore& diagnostics ) const
{
    if ( !failed )
    {
        return SkullbonezCore::Core::SbResult::Success();
    }

    return diagnostics.Failure( "InteractionAutomation", failure[0] != '\0' ? failure : "interaction automation failed" );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::Configure( const char* reportPath )
{
    // Reconfiguration is a cold Automation operation. Clear the complete
    // previous report epoch while retaining the fixed store-bound tracer owner.
    m_written = false;
    m_actionReports.clear();
    m_assertionReports.clear();
    m_screenshots.clear();
    m_replayVisualFidelityTicks.clear();
    m_replayCausalProofTicks.clear();
    m_replayCausalTopology.clear();
    m_replayVisualTrajectoryDigests.clear();
    m_replayVisualPredictionArchive.clear();
    m_replayVisualPredictionDrawList.Clear();
    m_replayVisualPredictionDrawPacket = {};
    m_replayVisualPredictionDrawCameraEye = Math::Vector::ZERO_VECTOR;
    m_replayVisualPredictionDrawCameraUp = Math::Vector::ZERO_VECTOR;
    m_replayVisualPredictionDrawStreamId = 1;
    m_replayVisualPredictionDrawRevision = 0;
    m_replayVisualPredictionDrawCameraValid = false;
    m_replayVisualFidelityStartFrame = -1;
    m_replayVisualFidelityCaptureEnabled = false;
    m_replayVisualFidelityCaptureComplete = false;
    m_replayVisualFidelityTrajectoryHash = 0;
    m_replayVisualFidelityTrajectoryRecordCount = 0;
    m_replayVisualFidelityTrajectoryPointCount = 0;
    m_replayVisualFidelityTrajectoryCaptured = false;
    m_replayVisualOfflineProjectionComplete = false;
    m_editorSelectionCaptureFingerprints[0] = 0;
    m_editorSelectionCaptureFingerprints[1] = 0;
    m_editorSelectionCaptureValid[0] = false;
    m_editorSelectionCaptureValid[1] = false;

    strcpy_s( m_path, sizeof( m_path ),
              reportPath && reportPath[0] != '\0' ? reportPath : "TestOutput\\interaction\\interaction_report.json" );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReserveForActions( std::size_t actionCount )
{
    m_actionReports.reserve( actionCount + 8u );
    m_assertionReports.reserve( actionCount + 8u );
    m_screenshots.reserve( actionCount );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::AppendAction( int frame, const char* type,
                                                                               const char* target, const POINT* mouse,
                                                                               bool consumed, const char* detail )
{
    RunInteractionAutomationReportAction report;
    report.frame = frame;
    strcpy_s( report.type, sizeof( report.type ), type ? type : "unknown" );

    if ( target )
    {
        strcpy_s( report.target, sizeof( report.target ), target );
    }

    if ( mouse )
    {
        report.mouse = *mouse;
        report.hasMouse = true;
    }

    report.consumed = consumed;

    if ( detail )
    {
        strcpy_s( report.detail, sizeof( report.detail ), detail );
    }

    m_actionReports.push_back( report );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::AppendAssertion( const RunInteractionAutomationReportAssertion& assertion )
{
    m_assertionReports.push_back( assertion );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::AddScreenshot( const char* path )
{
    m_screenshots.emplace_back( path ? path : "" );
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::BeginReplayVisualCapture( std::size_t tickCapacity )
{
    m_replayVisualFidelityCaptureEnabled = true;
    m_replayVisualFidelityCaptureComplete = false;
    m_replayVisualFidelityStartFrame = -1;
    m_replayVisualFidelityTicks.clear();
    m_replayVisualFidelityTicks.reserve( tickCapacity );
    m_replayCausalProofTicks.clear();
    m_replayCausalProofTicks.reserve( tickCapacity );
    m_replayCausalTopology.clear();
    m_replayVisualFidelityTrajectoryHash = 0;
    m_replayVisualFidelityTrajectoryRecordCount = 0;
    m_replayVisualFidelityTrajectoryPointCount = 0;
    m_replayVisualFidelityTrajectoryCaptured = false;
    m_replayVisualOfflineProjectionComplete = false;
    m_replayVisualTrajectoryDigests.clear();
    m_replayVisualPredictionArchive.clear();
    m_replayVisualPredictionDrawList.Clear();
    m_replayVisualPredictionDrawPacket = {};
    m_replayVisualPredictionDrawCameraEye = Math::Vector::ZERO_VECTOR;
    m_replayVisualPredictionDrawCameraUp = Math::Vector::ZERO_VECTOR;
    m_replayVisualPredictionDrawStreamId = 1;
    m_replayVisualPredictionDrawRevision = 0;
    m_replayVisualPredictionDrawCameraValid = false;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::UpdateReplayVisualReveal( int sceneFrame, int fixedStartFrame, bool liveAdvanceHeld, bool revealReady, InteractionAutomationRunStatus& status,
                                                                                           ReplayFrameIndex& outRevealFrame, bool& outResetReveal ) noexcept
{
    outRevealFrame = 0;
    outResetReveal = false;

    if ( !m_replayVisualFidelityCaptureEnabled )
    {
        return false;
    }

    // Invariant: capture closure is one-way. The script may keep the process
    // alive for final assertions, but it must not issue another reveal intent.
    if ( m_replayVisualFidelityCaptureComplete )
    {
        return false;
    }

    if ( m_replayVisualFidelityStartFrame < 0 && sceneFrame == fixedStartFrame )
    {
        if ( !revealReady )
        {
            status.Fail( "replay visual fidelity prediction was not fully published before fixed reveal start" );
        }
        else
        {
            m_replayVisualFidelityStartFrame = sceneFrame;

            // BeginReplayVisualCapture already published the one reset intent.
            // The fixed start only advances the held reveal from that seed.
            outResetReveal = false;
        }
    }
    else if ( m_replayVisualFidelityStartFrame < 0 && sceneFrame > fixedStartFrame )
    {
        status.Fail( "replay visual fidelity missed its fixed reveal start frame" );
    }

    if ( m_replayVisualFidelityStartFrame < 0 )
    {
        return false;
    }

    // Invariant: liveAdvanceHeld means the authoritative scene is paused. Once
    // fixed reveal begins, losing that hold would advance a second wall fall
    // underneath the one captured prediction cascade.
    if ( !liveAdvanceHeld )
    {
        status.Fail( "replay visual fidelity probe entered a second live playback pass" );
    }

    outRevealFrame = static_cast<ReplayFrameIndex>( (std::max)( 0, sceneFrame - m_replayVisualFidelityStartFrame ) );
    return true;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::CaptureReplayVisualFrame( int sceneFrame, const ReplayAutomationView& replay, InteractionAutomationRunStatus& status )
{
    if ( !m_replayVisualFidelityCaptureEnabled || m_replayVisualFidelityCaptureComplete )
    {
        return true;
    }

    if ( replay.prediction.build.generationBeginCount > 1u )
    {
        status.Fail( "replay visual fidelity attempted a duplicate prediction generation" );
        m_replayVisualFidelityCaptureEnabled = false;
        return false;
    }

    if ( m_replayVisualFidelityStartFrame < 0 ||
         m_replayVisualFidelityTicks.size() >= replay.prediction.CommittedFrameCount() )
    {
        return true;
    }

    const Core::MainMemoryReplayTrajectorySubmissionStats& submission = replay.visualPacket.submission;
    const ReplayVisualPacket& packet = replay.visualPacket;
    const ReplayVisualPacketFingerprint
        packetFingerprint = BuildReplayVisualPacketFingerprint( packet, m_replayVisualTrajectoryDigests );

    const ReplayVisualPacketBufferFacts bufferFacts = BuildReplayVisualPacketBufferFacts( packet );
    ReplayVisualFidelityReportTick tick;
    tick.sceneFrame = sceneFrame;
    tick.revealFrame = replay.prediction.revealClock.presentedFrame;
    tick.sourceFrame = packet.header.sourceFrame;
    tick.semanticHash = packetFingerprint.semanticHash;
    tick.headerStateHash = packetFingerprint.headerStateHash;
    tick.trajectoryStateHash = packetFingerprint.trajectoryStateHash;
    tick.topologyStateHash = packetFingerprint.topologyStateHash;
    tick.markerStateHash = packetFingerprint.markerStateHash;
    tick.ghostStateHash = packetFingerprint.ghostStateHash;
    tick.visualStateHash = packetFingerprint.visualStateHash;
    tick.exactPacketHash = packetFingerprint.exactHash;
    tick.schemaVersion = packet.header.schemaVersion;
    tick.targetId = packet.header.targetId.value;
    tick.branchId = packet.header.branchId;
    tick.eventCursor = packet.header.eventCursor;
    tick.topologyVersion = packet.header.topologyVersion;
    tick.publishedFrameCount = packet.header.publishedFrameCount;
    tick.predictionEnabled = packet.header.predictionEnabled;
    tick.predictionBuilding = packet.header.predictionBuilding;
    tick.predictionComplete = packet.header.predictionComplete;
    tick.cameraEye = packet.header.cameraEye;
    tick.cameraUp = packet.header.cameraUp;
    tick.trajectoryRecordCount = static_cast<uint32_t>( packet.trajectoryRecords.size() );
    tick.futureNodeCount = packet.header.futureNodeCount;
    tick.retainedMarkerCount = static_cast<uint32_t>( packet.retainedMarkers.size() );
    tick.ghostRequestCount = packet.header.ghostRequestCount;
    tick.replayReserveGrowthEvents = packet.header.replayReserveGrowthEvents;
    tick.hasGeometry = bufferFacts.hasGeometry;
    tick.combinedLineHash = bufferFacts.combinedLineHash;
    tick.combinedLineBytes = bufferFacts.combinedLineBytes;

    // Retained and frame-local line lanes are physically separate so stable
    // prediction frames never concatenate CPU buffers. Report their logical
    // combined vertex count from the renderer-bound byte facts instead.
    tick.combinedLineVertexCount = static_cast<uint32_t>( bufferFacts.combinedLineBytes / ( sizeof( float ) * 6u ) );

    for ( uint64_t dropped : packet.trajectoryDiagnostics.droppedSegments )
    {
        tick.droppedSegmentCount += dropped;
    }

    if ( packet.header.futureNodeCount != packet.futureNodes.size() ||
         packet.header.ghostRequestCount != packet.ghostRequests.size() )
    {
        status.Fail( "replay visual packet header/span count mismatch" );
    }

    if ( const char* mismatch = FindReplayVisualPacketSubmissionSpanMismatch( packet ) )
    {
        char message[256] = {};

        sprintf_s( message, sizeof( message ), "replay visual packet/submission mismatch: %s", mismatch );
        status.Fail( message );
    }

    // Invariant: one reveal generation yields one dense sequence of evidence
    // rows. A reset or skipped frame fails even if later buffers converge.
    const uint64_t expectedRevealFrame = static_cast<uint64_t>( m_replayVisualFidelityTicks.size() );

    if ( tick.revealFrame != expectedRevealFrame )
    {
        char message[256] = {};

        sprintf_s( message, sizeof( message ), "replay visual reveal restarted or skipped: expected=%llu actual=%llu",
                   static_cast<unsigned long long>( expectedRevealFrame ),
                   static_cast<unsigned long long>( tick.revealFrame ) );

        status.Fail( message );
    }

    tick.ordinaryLineHash = bufferFacts.ordinaryLineHash;
    tick.priorityLineHash = bufferFacts.priorityLineHash;
    tick.priorityLineCanonicalHash = submission.priorityLineCanonicalHash;
    tick.ordinaryRibbonHash = bufferFacts.ordinaryRibbonHash;
    tick.priorityRibbonHash = bufferFacts.priorityRibbonHash;
    tick.priorityRibbonCanonicalHash = submission.priorityRibbonCanonicalHash;
    tick.vertexHash = bufferFacts.expandedVertexHash;
    tick.ordinaryVertexHash = bufferFacts.ordinaryExpandedVertexHash;
    tick.ordinaryLineBytes = bufferFacts.ordinaryLineBytes;
    tick.priorityLineBytes = bufferFacts.priorityLineBytes;
    tick.ordinaryRibbonBytes = bufferFacts.ordinaryRibbonBytes;
    tick.priorityRibbonBytes = bufferFacts.priorityRibbonBytes;
    tick.vertexBytes = bufferFacts.expandedVertexBytes;
    tick.ordinaryVertexBytes = bufferFacts.ordinaryExpandedVertexBytes;
    tick.ordinaryLineVertexCount = submission.ordinaryLineVertexCount;
    tick.priorityLineVertexCount = submission.priorityLineVertexCount;
    tick.ordinaryRibbonSegmentCount = submission.ordinaryRibbonSegmentCount;
    tick.priorityRibbonSegmentCount = submission.priorityRibbonSegmentCount;
    tick.vertexCount = submission.vertexCount;
    tick.ordinaryVertexCount = submission.ordinaryVertexCount;
    tick.segmentCount = submission.segmentCount;
    m_replayVisualFidelityTicks.push_back( tick );
    m_replayCausalProofTicks.push_back( BuildReplayCausalProofTick( packet ) );

    const std::span<const RunReplayPredictionFrame> predictionFrames = replay.prediction.CommittedFrames();

    if ( m_replayVisualFidelityTicks.size() == predictionFrames.size() && !predictionFrames.empty() )
    {
        const PredictionTrajectoryFingerprint revealFingerprint = BuildPredictionTrajectoryFingerprint( replay );
        m_replayVisualFidelityTrajectoryHash = revealFingerprint.hash;
        m_replayVisualFidelityTrajectoryRecordCount = revealFingerprint.recordCount;
        m_replayVisualFidelityTrajectoryPointCount = revealFingerprint.pointCount;
        m_replayVisualFidelityTrajectoryCaptured = revealFingerprint.Ready();

        if ( !BuildReplayPredictionArchive( replay.path, replay.prediction, replay.predictionDetailMode,
                                            replay.predictionEvidence, m_replayVisualPredictionArchive ) )
        {
            status.Fail( "replay visual fidelity probe could not freeze prediction presentation state" );
        }

        m_replayCausalTopology.reserve( packet.futureNodes.size() );

        for ( const RunReplayPathTraceNode& node : packet.futureNodes )
        {
            m_replayCausalTopology.push_back( ReplayCausalTopologyNodeReport { node.id.value, node.parentId.value,
                                                                               node.firstFrame, node.depth,
                                                                               node.contactDerived } );
        }

        m_replayVisualFidelityCaptureComplete = true;
    }

    return true;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayVisualCaptureEnabled() const noexcept
{
    return m_replayVisualFidelityCaptureEnabled;
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::ResetEditorSelectionCaptures() noexcept
{
    for ( int slot = 0; slot < 2; ++slot )
    {
        m_editorSelectionCaptureFingerprints[slot] = 0;
        m_editorSelectionCaptureValid[slot] = false;
    }
}

void SkullbonezCore::Runtime::InteractionAutomationReportWriter::CaptureEditorSelection( int slot, uint64_t fingerprint,
                                                                                         bool valid ) noexcept
{
    if ( slot < 0 || slot >= 2 )
    {
        return;
    }

    m_editorSelectionCaptureFingerprints[slot] = fingerprint;
    m_editorSelectionCaptureValid[slot] = valid;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::TryEditorSelectionCapture( int slot, uint64_t& outFingerprint ) const noexcept
{
    if ( slot < 0 || slot >= 2 || !m_editorSelectionCaptureValid[slot] )
    {
        return false;
    }

    outFingerprint = m_editorSelectionCaptureFingerprints[slot];
    return true;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::VerifyReplayVisualOfflineProjection( InteractionAutomationRunStatus& status, RuntimeTools& runtimeTools, SceneWorld& world,
                                                                                                      const ReplaySolverFrameSample* latestSolverSample )
{
    if ( m_replayVisualOfflineProjectionComplete )
    {
        return true;
    }

    if ( m_replayVisualPredictionArchive.empty() || m_replayVisualFidelityTicks.empty() )
    {
        status.Fail( "replay visual offline projection has no frozen prediction or RVIS rows" );
        return false;
    }

    // These owners contain bounded prediction and presentation banks. The
    // Automation diagnostics scope keeps their one cold instance off the
    // render thread's fixed stack while the retained-list builder is nested.
    std::deque<ReplayPrediction> offlinePredictions;
    offlinePredictions.emplace_back( m_resultDiagnostics );
    ReplayPrediction& offlinePrediction = offlinePredictions.front();
    std::deque<ReplayPresentation> offlinePresentations;
    offlinePresentations.emplace_back();
    ReplayPresentation& offlinePresentation = offlinePresentations.front();
    offlinePrediction.EnterOfflineVerification();
    offlinePrediction.PresentationOwner().ResetTrajectoryVisualStats();
    char archiveReason[192] = {};

    RunReplayPathVisualizerState archivePath;

    if ( !offlinePrediction.LoadArchive( m_replayVisualPredictionArchive, archivePath, archiveReason,
                                         sizeof( archiveReason ) ) )
    {
        char message[320] = {};

        sprintf_s( message, sizeof( message ), "replay visual offline projection rejected RVPD: %s",
                   archiveReason[0] != '\0' ? archiveReason : "unknown archive failure" );

        status.Fail( message );
        return false;
    }

    offlinePresentation.ApplyArchivePathState( archivePath );

    // The archived value retains the final marker prefix. CPU projection starts
    // at reveal zero and rebuilds first appearance exactly as the sole presented
    // run did. No renderer/backend method is reachable from this function.
    offlinePrediction.PresentationOwner().ResetTrajectoryVisualStats();
    offlinePrediction.ResetVerificationMarkers();
    EditorTracer& tracer = runtimeTools.Tracer();
    std::vector<ReplayVisualTrajectoryDigestState> trajectoryDigests;
    trajectoryDigests.reserve( offlinePrediction.State().trajectoryStore.RecordCount() );

    // Invariant: captured and reconstructed raw generations are independent
    // processes; only their first-publication token sequences may be compared.
    ReplayVisualPacketOperations::ReplayVisualTopologyVersionCanonicalizer capturedTopologyVersions;
    ReplayVisualPacketOperations::ReplayVisualTopologyVersionCanonicalizer projectedTopologyVersions;

    for ( const ReplayVisualFidelityReportTick& tick : m_replayVisualFidelityTicks )
    {
        const uint32_t canonicalTopologyVersion = capturedTopologyVersions.Observe( tick.topologyVersion );

        const ReplayVisualArchiveSample expected = BuildReplayVisualArchiveSample( tick, canonicalTopologyVersion );
        offlinePrediction.SetVerificationRevealFrame( expected.revealFrame );
        tracer.Clear();
        const RunReplayPathVisualizerState& path = offlinePresentation.PathVisualizer();
        ReplayPredictionUpdateResult update;
        offlinePrediction.PreparePresentation( world.Entities(), Physics::PhysicsEngine::ReadColliders( world.Physics() ),
                                               path.targetId, path.targetModelRow, path.hasTarget, 5.0, update );

        if ( update.targetModelRowRepaired )
        {
            offlinePresentation.SetPathTargetModelRow( update.repairedTargetModelRow );
        }

        for ( std::size_t passIndex = 0; passIndex < update.budgetExpiries.size(); ++passIndex )
        {
            for ( uint32_t count = 0; count < update.budgetExpiries[passIndex]; ++count )
            {
                offlinePrediction.PresentationOwner().RecordTrajectoryBudgetExpiry( static_cast<Core::MainMemoryReplayBudgetPass>( passIndex ) );
            }
        }

        for ( std::size_t causeIndex = 0; causeIndex < update.rebuildCauses.size(); ++causeIndex )
        {
            for ( uint32_t count = 0; count < update.rebuildCauses[causeIndex]; ++count )
            {
                offlinePrediction.PresentationOwner().RecordTrajectoryRebuildCause( static_cast<Core::MainMemoryReplayRebuildCause>( causeIndex ) );
            }
        }

        offlinePresentation.PreparePathDrawing( world.BodyStore() );
        const ReplayPredictionPresentationView prediction = offlinePrediction.PresentationView();
        offlinePrediction.PresentationOwner().RenderPathVisualizer( prediction, offlinePresentation.PathVisualizer(),
                                                                    latestSolverSample, world.Physics(), world.Entities(),
                                                                    tracer );

        (void)offlinePrediction.PresentationOwner().BuildGhostDrawRequests( prediction, world.RenderPresentationRecords(),
                                                                            world.BodyStore() );

        ReplayVisualPacket projected = tracer.BuildReplayVisualPacket( expected.cameraEye, expected.cameraUp );
        offlinePrediction.PresentationOwner().PublishVisualPacket( projected, prediction,
                                                                   offlinePresentation.PathVisualizer().targetId,
                                                                   latestSolverSample, expected.replayReserveGrowthEvents );

        projected = offlinePrediction.PresentationOwner().PublishedVisualPacketView();
        projected.header.topologyVersion = projectedTopologyVersions.Observe( projected.header.topologyVersion );
        const ReplayVisualPacketFingerprint
            fingerprint = BuildReplayVisualPacketFingerprint( projected, trajectoryDigests,
                                                              ReplayVisualTrajectoryDigestPolicy::ReuseImmutableRecords );

        char difference[192] = {};

        if ( !ReplayVisualPacketMatchesArchiveSample( projected, expected, difference, sizeof( difference ) ) )
        {
            status.Fail( difference );
            return false;
        }

        const auto laneHashMatches = [&]( const char* lane, uint64_t expectedHash, uint64_t actualHash )
        {
            if ( expectedHash == actualHash )
            {
                return true;
            }

            char message[320] = {};

            sprintf_s( message, sizeof( message ),
                       "replay visual offline projection diverged at reveal %llu %s "
                       "expected=0x%016llX actual=0x%016llX",
                       static_cast<unsigned long long>( expected.revealFrame ), lane,
                       static_cast<unsigned long long>( expectedHash ), static_cast<unsigned long long>( actualHash ) );

            status.Fail( message );
            return false;
        };

        if ( !laneHashMatches( "headerStateHash", tick.headerStateHash, fingerprint.headerStateHash ) ||
             !laneHashMatches( "trajectoryStateHash", tick.trajectoryStateHash, fingerprint.trajectoryStateHash ) ||
             !laneHashMatches( "topologyStateHash", tick.topologyStateHash, fingerprint.topologyStateHash ) ||
             !laneHashMatches( "markerStateHash", tick.markerStateHash, fingerprint.markerStateHash ) ||
             !laneHashMatches( "ghostStateHash", tick.ghostStateHash, fingerprint.ghostStateHash ) )
        {
            return false;
        }

        if ( fingerprint.visualStateHash != expected.visualStateHash || fingerprint.exactHash != expected.exactPacketHash )
        {
            char message[320] = {};

            sprintf_s( message, sizeof( message ),
                       "replay visual offline projection diverged at reveal %llu hashes "
                       "visual=0x%016llX/0x%016llX exact=0x%016llX/0x%016llX",
                       static_cast<unsigned long long>( expected.revealFrame ),
                       static_cast<unsigned long long>( expected.visualStateHash ),
                       static_cast<unsigned long long>( fingerprint.visualStateHash ),
                       static_cast<unsigned long long>( expected.exactPacketHash ),
                       static_cast<unsigned long long>( fingerprint.exactHash ) );

            status.Fail( message );
            return false;
        }
    }

    // Leave the operator-visible enabled bit unchanged. The gate exits after
    // this offline pass; the one-way generation capability guarantees these
    // retained values cannot start another prediction.
    m_replayVisualOfflineProjectionComplete = true;
    return true;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::FinishReplayVisualCapture( InteractionAutomationRunStatus& status, RuntimeTools& runtimeTools, SceneWorld& world,
                                                                                            const ReplayAutomationView& replay )
{
    if ( !m_replayVisualFidelityCaptureEnabled )
    {
        return true;
    }

    if ( !status.failed && !m_replayVisualOfflineProjectionComplete )
    {
        (void)VerifyReplayVisualOfflineProjection( status, runtimeTools, world, replay.latestSolverSample );
    }

    return status.failed || m_replayVisualFidelityCaptureComplete;
}

std::string SkullbonezCore::Runtime::InteractionAutomationReportWriter::FormatPredictionHash( uint64_t hash )
{
    char buffer[24] = {};
    sprintf_s( buffer, sizeof( buffer ), "0x%016llX", static_cast<unsigned long long>( hash ) );
    return buffer;
}

PredictionTrajectoryFingerprint
SkullbonezCore::Runtime::InteractionAutomationReportWriter::BuildPredictionTrajectoryFingerprint( const ReplayAutomationView& replay )
{
    PredictionTrajectoryFingerprint fingerprint;

    const ReplayTrajectoryStore& store = replay.prediction.trajectoryStore;

    for ( const ReplayTrajectoryRecord& record : store.ActiveRecords() )
    {
        const std::size_t publishedPointCount = (std::min)( record.publishedPointCount, record.points.size() );

        if ( publishedPointCount == 0 )
        {
            continue;
        }

        // Invariant: this report hash intentionally ignores record versions and
        // vector capacity. It fingerprints only the sampled polylines and draw
        // hierarchy that should be byte-identical across two identical
        // prediction runs.
        HashPredictionScalar( fingerprint.hash, record.key.bodyId.value );
        HashPredictionScalar( fingerprint.hash, static_cast<uint8_t>( record.key.lane ) );
        HashPredictionScalar( fingerprint.hash, record.key.branchOrdinal );
        HashPredictionScalar( fingerprint.hash, record.styleId );
        HashPredictionScalar( fingerprint.hash, record.parentId.value );
        HashPredictionScalar( fingerprint.hash, record.depth );
        HashPredictionScalar( fingerprint.hash, record.firstFrame );
        HashPredictionScalar( fingerprint.hash, static_cast<uint8_t>( record.contactDerived ? 1u : 0u ) );
        HashPredictionScalar( fingerprint.hash, static_cast<uint64_t>( publishedPointCount ) );

        for ( std::size_t i = 0; i < publishedPointCount; ++i )
        {
            const ReplayTrajectoryPoint& point = record.points[i];
            HashPredictionScalar( fingerprint.hash, point.frameIndex );
            HashPredictionVector( fingerprint.hash, point.position );
        }

        ++fingerprint.recordCount;
        fingerprint.pointCount += publishedPointCount;
    }

    return fingerprint;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::TryPredictionTargetDisplacement( const ReplayAutomationView& replay, float& outDisplacement, Vector3* outFirst, Vector3* outLast )
{
    // Concept: automation reports compare the first and last prediction sample
    // for the selected replay body. Missing target data is a clean "not ready",
    // not an error state for the running scene.
    const RunReplayPredictionState& prediction = replay.prediction;
    std::span<const RunReplayPredictionFrame> activePredictionFrames = replay.activePredictionFrames;
    std::size_t activeFrameCount = activePredictionFrames.size();

    if ( activeFrameCount < 2 && prediction.BuildPrefixShouldBePresented() )
    {
        activeFrameCount = prediction.PublishedBuildFrameCount();
        activePredictionFrames = { prediction.build.buildFrames.data(), activeFrameCount };
    }

    const Physics::PhysicsSceneObjectId targetId = replay.path.targetId;

    if ( targetId.value == 0 || activeFrameCount < 2 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* first = FindPredictionBodyById( activePredictionFrames.front(), targetId );
    const RunReplayPredictionBodySample* last = FindPredictionBodyById( activePredictionFrames[activeFrameCount - 1],
                                                                        targetId );

    if ( !first || !last )
    {
        return false;
    }

    outDisplacement = VectorMag( last->position - first->position );

    if ( outFirst )
    {
        *outFirst = first->position;
    }

    if ( outLast )
    {
        *outLast = last->position;
    }

    return true;
}

std::size_t
SkullbonezCore::Runtime::InteractionAutomationReportWriter::VisiblePredictionFrameCount( const ReplayAutomationView& replay )
{
    const RunReplayPredictionState& prediction = replay.prediction;
    const std::span<const RunReplayPredictionFrame> activePredictionFrames = replay.activePredictionFrames;

    if ( activePredictionFrames.size() >= 2 )
    {
        return activePredictionFrames.size();
    }

    if ( prediction.build.building )
    {
        return prediction.PublishedBuildFrameCount();
    }

    return activePredictionFrames.size();
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayPredictionPathVisible( const ReplayAutomationView& replay )
{
    // Concept: long prediction jobs expose a populated build prefix before the
    // final frame vector is swapped in. Automation should agree with the overlay
    // and count that prefix as visible once it can draw at least one segment.
    return replay.path.hasTarget &&
           ( VisiblePredictionFrameCount( replay ) >= 2 || !replay.prediction.futureNodeCache.futureNodes.empty() );
}

std::size_t SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayPastTrajectoryPublishedPointCount( const ReplayAutomationView& replay )
{
    // Concept: this is a structural performance/flicker probe. The selected
    // path must retain a published drawable prefix while its recorder ring
    // advances, independent of machine-specific frame timing.
    const RunReplayPathVisualizerState& visualizer = replay.path;

    for ( const ReplayTrajectoryRecord& record : replay.prediction.trajectoryStore.ActiveRecords() )
    {
        if ( record.key.lane == ReplayTrajectoryLane::PastRoot && record.key.bodyId.value == visualizer.targetId.value )
        {
            return (std::min)( record.publishedPointCount, record.points.size() );
        }
    }

    return 0u;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayPredictionContactsIncomplete( const ReplayAutomationView& replay )
{
    // Concept: automation reports should distinguish a valid root prediction
    // from a partial contact-derived tree, because contact reserve failures are
    // intentionally non-fatal to prediction drawing.
    const RunReplayPredictionState& prediction = replay.prediction;
    std::span<const RunReplayPredictionFrame> frames = prediction.CommittedFrames();

    if ( prediction.BuildPrefixShouldBePresented() )
    {
        frames = { prediction.build.buildFrames.data(), prediction.PublishedBuildFrameCount() };
    }

    for ( const RunReplayPredictionFrame& frame : frames )
    {
        if ( frame.contactsIncomplete )
        {
            return true;
        }
    }

    return false;
}

bool SkullbonezCore::Runtime::InteractionAutomationReportWriter::LiveSolverHashStableAcrossPrediction( const ReplayAutomationView& replay, uint64_t* outSourceHash, uint64_t* outLiveHash )
{
    // Concept: prediction isolation proof. The source hash is captured before
    // the private prediction engine starts stepping; the live latest hash should
    // still match after prediction has produced visible frames.
    const ReplaySolverFrameSample* latest = replay.latestSolverSample;
    const uint64_t sourceHash = replay.prediction.simulation.sourceSolverHash;
    const uint64_t liveHash = latest ? latest->solverHash : 0;

    if ( outSourceHash )
    {
        *outSourceHash = sourceHash;
    }

    if ( outLiveHash )
    {
        *outLiveHash = liveHash;
    }

    return latest && sourceHash != 0 && sourceHash == liveHash;
}

const char* SkullbonezCore::Runtime::InteractionAutomationReportWriter::CameraModeName( RunCameraMode mode )
{
    switch ( mode )
    {
    case RunCameraMode::Demo:
        return "Demo";
    case RunCameraMode::Scene:
        return "Scene";
    case RunCameraMode::Inspect:
        return "Inspect";
    case RunCameraMode::Attach:
        return "Attach";
    case RunCameraMode::Launcher:
        return "Launcher";
    case RunCameraMode::Manipulator:
        return "Manipulator";
    case RunCameraMode::Director:
        return "Director";
    case RunCameraMode::Count:
        break;
    }

    return "Unknown";
}

const char* SkullbonezCore::Runtime::InteractionAutomationReportWriter::WorkspaceName( RuntimeWorkspace workspace )
{
    switch ( workspace )
    {
    case RuntimeWorkspace::Live:
        return "Live";
    case RuntimeWorkspace::Inspect:
        return "Inspect";
    case RuntimeWorkspace::Edit:
        return "Edit";
    case RuntimeWorkspace::Replay:
        return "Replay";
    }

    return "Unknown";
}

const char* SkullbonezCore::Runtime::InteractionAutomationReportWriter::OwnerName( WorldInteractionOwner owner )
{
    switch ( owner )
    {
    case WorldInteractionOwner::None:
        return "None";
    case WorldInteractionOwner::InspectGizmo:
        return "InspectGizmo";
    case WorldInteractionOwner::EditorPlacement:
        return "EditorPlacement";
    case WorldInteractionOwner::EditorGizmo:
        return "EditorGizmo";
    case WorldInteractionOwner::ReplayScrub:
        return "ReplayScrub";
    case WorldInteractionOwner::ReplayVelocityEdit:
        return "ReplayVelocityEdit";
    case WorldInteractionOwner::ReplayPrediction:
        return "ReplayPrediction";
    case WorldInteractionOwner::ReplayBranchTarget:
        return "ReplayBranchTarget";
    case WorldInteractionOwner::ReplayCauseTree:
        return "ReplayCauseTree";
    case WorldInteractionOwner::Launcher:
        return "Launcher";
    case WorldInteractionOwner::Manipulator:
        return "Manipulator";
    }

    return "Unknown";
}

const char* SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayTrackName( RunReplayTrack track )
{
    return track == RunReplayTrack::Solver ? "Solver" : "Presentation";
}

const char*
SkullbonezCore::Runtime::InteractionAutomationReportWriter::ReplayPredictionBuildModeName( ReplayPredictionBuildMode mode )
{
    switch ( mode )
    {
    case ReplayPredictionBuildMode::Instant:
        return "Instant";
    case ReplayPredictionBuildMode::Amortized:
        return "Amortized";
    case ReplayPredictionBuildMode::Undecided:
    default:
        return "Undecided";
    }
}

ReplayVisualArchiveSample SkullbonezCore::Runtime::InteractionAutomationReportWriter::BuildReplayVisualArchiveSample( const ReplayVisualFidelityReportTick& tick, uint32_t canonicalTopologyVersion )
{
    ReplayVisualArchiveSample packet;
    packet.sourceFrame = tick.sourceFrame;
    packet.revealFrame = tick.revealFrame;
    packet.semanticHash = tick.semanticHash;
    packet.visualStateHash = tick.visualStateHash;
    packet.exactPacketHash = tick.exactPacketHash;
    packet.schemaVersion = tick.schemaVersion;
    packet.targetId = tick.targetId;
    packet.branchId = tick.branchId;
    packet.eventCursor = tick.eventCursor;
    packet.topologyVersion = canonicalTopologyVersion;
    packet.publishedFrameCount = tick.publishedFrameCount;
    packet.predictionEnabled = tick.predictionEnabled ? 1u : 0u;
    packet.predictionBuilding = tick.predictionBuilding ? 1u : 0u;
    packet.predictionComplete = tick.predictionComplete ? 1u : 0u;
    packet.cameraEye = tick.cameraEye;
    packet.cameraUp = tick.cameraUp;
    packet.combinedLineHash = tick.combinedLineHash;
    packet.ordinaryLineHash = tick.ordinaryLineHash;
    packet.priorityLineHash = tick.priorityLineHash;
    packet.priorityLineCanonicalHash = tick.priorityLineCanonicalHash;
    packet.ordinaryRibbonHash = tick.ordinaryRibbonHash;
    packet.priorityRibbonHash = tick.priorityRibbonHash;
    packet.priorityRibbonCanonicalHash = tick.priorityRibbonCanonicalHash;
    packet.expandedVertexHash = tick.vertexHash;
    packet.ordinaryExpandedVertexHash = tick.ordinaryVertexHash;
    packet.droppedSegmentCount = tick.droppedSegmentCount;

    // Concept: reserve growth is process telemetry. The report retains the live
    // counter; RVIS and offline reconstruction use the durable zero constant.
    packet.replayReserveGrowthEvents = 0u;
    packet.combinedLineBytes = tick.combinedLineBytes;
    packet.ordinaryLineBytes = tick.ordinaryLineBytes;
    packet.priorityLineBytes = tick.priorityLineBytes;
    packet.ordinaryRibbonBytes = tick.ordinaryRibbonBytes;
    packet.priorityRibbonBytes = tick.priorityRibbonBytes;
    packet.expandedVertexBytes = tick.vertexBytes;
    packet.ordinaryExpandedVertexBytes = tick.ordinaryVertexBytes;
    packet.hasGeometry = tick.hasGeometry ? 1u : 0u;
    packet.trajectoryRecordCount = tick.trajectoryRecordCount;
    packet.futureNodeCount = tick.futureNodeCount;
    packet.retainedMarkerCount = tick.retainedMarkerCount;
    packet.ghostRequestCount = tick.ghostRequestCount;
    packet.combinedLineVertexCount = tick.combinedLineVertexCount;
    packet.ordinaryLineVertexCount = tick.ordinaryLineVertexCount;
    packet.priorityLineVertexCount = tick.priorityLineVertexCount;
    packet.ordinaryRibbonSegmentCount = tick.ordinaryRibbonSegmentCount;
    packet.priorityRibbonSegmentCount = tick.priorityRibbonSegmentCount;
    packet.expandedVertexCount = tick.vertexCount;
    packet.ordinaryExpandedVertexCount = tick.ordinaryVertexCount;
    packet.segmentCount = tick.segmentCount;
    return packet;
}


SkullbonezCore::Core::SbResult SkullbonezCore::Runtime::InteractionAutomationReportWriter::Write( InteractionAutomationRunStatus& status, const char* scriptPath, const SceneWorld& world, const SceneSessionState& scene,
                                                                                                  const char* scenePath, const EditorToolsOwner& editorTools, const RuntimeTools& runtimeTools, const ReplayAutomationView& replay,
                                                                                                  const RuntimeInteractionController& interaction, const CameraControlState& camera, const UI::InGameUI& ui,
                                                                                                  const Rendering::RenderSceneSnapshot& renderSnapshot )
{
    CoreAllocation::RuntimeAllocationScope diagnosticsScope( CoreAllocation::RuntimeAllocationPhase::Diagnostics );

    if ( m_written )
    {
        return status.Result( m_resultDiagnostics );
    }

    std::string replayArtifactPath;
    ReplayV2SaveResult replayArtifactResult;
    bool replayArtifactSaved = false;

    if ( m_replayVisualFidelityCaptureEnabled && m_replayVisualOfflineProjectionComplete && !status.failed )
    {
        replayArtifactPath = m_path;
        const std::size_t extensionOffset = replayArtifactPath.find_last_of( '.' );

        if ( extensionOffset != std::string::npos )
        {
            replayArtifactPath.resize( extensionOffset );
        }

        replayArtifactPath += ".skreplay";
        std::vector<ReplayVisualArchiveSample> visualPackets;
        visualPackets.reserve( m_replayVisualFidelityTicks.size() );
        ReplayVisualPacketOperations::ReplayVisualTopologyVersionCanonicalizer topologyVersions;

        for ( const ReplayVisualFidelityReportTick& tick : m_replayVisualFidelityTicks )
        {
            const uint32_t canonicalTopologyVersion = topologyVersions.Observe( tick.topologyVersion );

            visualPackets.push_back( BuildReplayVisualArchiveSample( tick, canonicalTopologyVersion ) );
        }

        // Recoverable error: the artifact is cold validation IO. Its failure belongs in
        // the machine-readable automation result, never in runtime ownership.
        char archiveReason[192] = {};

        if ( !VerifyReplayPredictionArchiveRoundTrip( m_replayVisualPredictionArchive, archiveReason,
                                                      sizeof( archiveReason ) ) )
        {
            char message[320] = {};

            sprintf_s( message, sizeof( message ), "replay visual prediction archive failed offline round-trip: %s",
                       archiveReason[0] != '\0' ? archiveReason : "unknown archive failure" );

            status.Fail( message );
        }

        if ( !status.failed )
        {
            replayArtifactSaved = ReplayV2Artifact::SavePresentationWithSolverHashes( replay.presentationRecorder,
                                                                                      replay.solverRecorder,
                                                                                      replay.eventRecorder, visualPackets,
                                                                                      m_replayVisualPredictionArchive,
                                                                                      replayArtifactPath.c_str(),
                                                                                      &replayArtifactResult );
        }

        if ( replayArtifactSaved )
        {
            std::vector<uint8_t> loadedPredictionArchive;
            char loadedArchiveReason[192] = {};

            const bool loadedArchiveVerified = ReplayV2Artifact::LoadVisualPredictionState( replayArtifactPath.c_str(),
                                                                                            loadedPredictionArchive ) &&
                                               loadedPredictionArchive == m_replayVisualPredictionArchive &&
                                               VerifyReplayPredictionArchiveRoundTrip( loadedPredictionArchive,
                                                                                       loadedArchiveReason,
                                                                                       sizeof( loadedArchiveReason ) );

            if ( !loadedArchiveVerified )
            {
                replayArtifactSaved = false;
                char message[320] = {};

                sprintf_s( message, sizeof( message ), "saved replay prediction archive failed offline readback: %s",
                           loadedArchiveReason[0] != '\0' ? loadedArchiveReason : "byte mismatch or missing RVPD" );

                status.Fail( message );
            }
        }

        if ( !status.failed && !replayArtifactSaved )
        {
            status.Fail( "replay visual fidelity probe failed to save its durable presentation artifact" );
        }
    }

    Json actions = Json::array();

    for ( const RunInteractionAutomationReportAction& report : m_actionReports )
    {
        Json item;
        item["frame"] = report.frame;
        item["type"] = report.type;
        item["target"] = report.target;
        item["consumed"] = report.consumed;
        item["detail"] = report.detail;

        if ( report.hasMouse )
        {
            item["mouse"] = Json::array( { report.mouse.x, report.mouse.y } );
        }

        actions.push_back( item );
    }

    Json assertions = Json::array();

    for ( const RunInteractionAutomationReportAssertion& assertion : m_assertionReports )
    {
        assertions.push_back( Json { { "frame", assertion.frame },
                                     { "name", assertion.name },
                                     { "expected", assertion.expected },
                                     { "actual", assertion.actual },
                                     { "passed", assertion.passed } } );
    }

    Json screenshots = Json::array();

    for ( const std::string& screenshot : m_screenshots )
    {
        screenshots.push_back( screenshot );
    }

    Json replayVisualFidelityTickRows = Json::array();

    for ( const ReplayVisualFidelityReportTick& tick : m_replayVisualFidelityTicks )
    {
        replayVisualFidelityTickRows.push_back( Json { { "sceneFrame", tick.sceneFrame },
                                                       { "revealFrame", tick.revealFrame },
                                                       { "sourceFrame", tick.sourceFrame },
                                                       { "semanticHash", FormatPredictionHash( tick.semanticHash ) },
                                                       { "headerStateHash", FormatPredictionHash( tick.headerStateHash ) },
                                                       { "trajectoryStateHash", FormatPredictionHash( tick.trajectoryStateHash ) },
                                                       { "topologyStateHash", FormatPredictionHash( tick.topologyStateHash ) },
                                                       { "markerStateHash", FormatPredictionHash( tick.markerStateHash ) },
                                                       { "ghostStateHash", FormatPredictionHash( tick.ghostStateHash ) },
                                                       { "visualStateHash", FormatPredictionHash( tick.visualStateHash ) },
                                                       { "exactPacketHash", FormatPredictionHash( tick.exactPacketHash ) },
                                                       { "schemaVersion", tick.schemaVersion },
                                                       { "targetId", tick.targetId },
                                                       { "branchId", tick.branchId },
                                                       { "eventCursor", tick.eventCursor },
                                                       { "topologyVersion", tick.topologyVersion },
                                                       { "publishedFrameCount", tick.publishedFrameCount },
                                                       { "predictionEnabled", tick.predictionEnabled },
                                                       { "predictionBuilding", tick.predictionBuilding },
                                                       { "predictionComplete", tick.predictionComplete },
                                                       { "cameraEye", { tick.cameraEye.x, tick.cameraEye.y, tick.cameraEye.z } },
                                                       { "cameraUp", { tick.cameraUp.x, tick.cameraUp.y, tick.cameraUp.z } },
                                                       { "trajectoryRecordCount", tick.trajectoryRecordCount },
                                                       { "futureNodeCount", tick.futureNodeCount },
                                                       { "retainedMarkerCount", tick.retainedMarkerCount },
                                                       { "ghostRequestCount", tick.ghostRequestCount },
                                                       { "droppedSegmentCount", tick.droppedSegmentCount },
                                                       { "replayReserveGrowthEvents", tick.replayReserveGrowthEvents },
                                                       { "hasGeometry", tick.hasGeometry },
                                                       { "combinedLineHash", FormatPredictionHash( tick.combinedLineHash ) },
                                                       { "combinedLineBytes", tick.combinedLineBytes },
                                                       { "combinedLineVertexCount", tick.combinedLineVertexCount },
                                                       { "ordinaryLineHash", FormatPredictionHash( tick.ordinaryLineHash ) },
                                                       { "priorityLineHash", FormatPredictionHash( tick.priorityLineHash ) },
                                                       { "priorityLineCanonicalHash", FormatPredictionHash( tick.priorityLineCanonicalHash ) },
                                                       { "ordinaryRibbonHash", FormatPredictionHash( tick.ordinaryRibbonHash ) },
                                                       { "priorityRibbonHash", FormatPredictionHash( tick.priorityRibbonHash ) },
                                                       { "priorityRibbonCanonicalHash", FormatPredictionHash( tick.priorityRibbonCanonicalHash ) },
                                                       { "vertexHash", FormatPredictionHash( tick.vertexHash ) },
                                                       { "ordinaryVertexHash", FormatPredictionHash( tick.ordinaryVertexHash ) },
                                                       { "ordinaryLineBytes", tick.ordinaryLineBytes },
                                                       { "priorityLineBytes", tick.priorityLineBytes },
                                                       { "ordinaryRibbonBytes", tick.ordinaryRibbonBytes },
                                                       { "priorityRibbonBytes", tick.priorityRibbonBytes },
                                                       { "vertexBytes", tick.vertexBytes },
                                                       { "ordinaryVertexBytes", tick.ordinaryVertexBytes },
                                                       { "ordinaryLineVertexCount", tick.ordinaryLineVertexCount },
                                                       { "priorityLineVertexCount", tick.priorityLineVertexCount },
                                                       { "ordinaryRibbonSegmentCount", tick.ordinaryRibbonSegmentCount },
                                                       { "priorityRibbonSegmentCount", tick.priorityRibbonSegmentCount },
                                                       { "vertexCount", tick.vertexCount },
                                                       { "ordinaryVertexCount", tick.ordinaryVertexCount },
                                                       { "segmentCount", tick.segmentCount } } );
    }

    Json replayCausalTicks = Json::array();

    for ( const ReplayCausalProofTick& tick : m_replayCausalProofTicks )
    {
        replayCausalTicks.push_back( Json { { "revealFrame", tick.revealFrame },
                                            { "activeTopologyHash", FormatPredictionHash( tick.activeTopologyHash ) },
                                            { "activeNodeCount", tick.activeNodeCount },
                                            { "revealedRecordCount", tick.revealedRecordCount },
                                            { "revealedPointCount", tick.revealedPointCount },
                                            { "revealedSegmentCount", tick.revealedSegmentCount },
                                            { "entryMarkerCount", tick.entryMarkerCount },
                                            { "restMarkerCount", tick.restMarkerCount },
                                            { "horizonMarkerCount", tick.horizonMarkerCount },
                                            { "ghostRequestCount", tick.ghostRequestCount } } );
    }

    Json replayCausalTopologyRows = Json::array();

    for ( const ReplayCausalTopologyNodeReport& node : m_replayCausalTopology )
    {
        replayCausalTopologyRows.push_back( Json { { "id", node.id },
                                                   { "parentId", node.parentId },
                                                   { "firstFrame", node.firstFrame },
                                                   { "depth", node.depth },
                                                   { "contactDerived", node.contactDerived } } );
    }

    // Concept: record the complete small, typed publication surface rather than
    // a screenshot-only impression. These rows prove that selected-causal and
    // authored-space policies produce different root shapes.
    Json predictionTrajectoryRows = Json::array();

    for ( const ReplayTrajectoryRecord& record : replay.visualPacket.trajectoryRecords )
    {
        predictionTrajectoryRows.push_back( Json { { "lane", ReplayTrajectoryLaneName( record.key.lane ) },
                                                   { "bodyId", record.key.bodyId.value },
                                                   { "parentId", record.parentId.value },
                                                   { "depth", record.depth },
                                                   { "firstFrame", record.firstFrame },
                                                   { "branchOrdinal", record.key.branchOrdinal },
                                                   { "publishedPointCount", record.publishedPointCount },
                                                   { "contactDerived", record.contactDerived } } );
    }

    Json predictionFutureNodeRows = Json::array();

    for ( const RunReplayPathTraceNode& node : replay.visualPacket.futureNodes )
    {
        predictionFutureNodeRows.push_back( Json { { "bodyId", node.id.value },
                                                   { "parentId", node.parentId.value },
                                                   { "depth", node.depth },
                                                   { "firstFrame", node.firstFrame },
                                                   { "contactDerived", node.contactDerived } } );
    }

    Json replayCauseTreeRows = Json::array();

    for ( const RunReplayCauseTreeRow& row : replay.causeTree.rows )
    {
        replayCauseTreeRows.push_back( Json { { "kind", ReplayCauseTreeRowKindName( row.kind ) },
                                              { "bodyId", row.id.value },
                                              { "parentId", row.parentId.value },
                                              { "counterpartId", row.counterpartId.value },
                                              { "depth", row.depth },
                                              { "firstFrame", row.firstFrame },
                                              { "prediction", row.prediction },
                                              { "modelRow", row.modelRow.value },
                                              { "counterpartModelRow", row.counterpartModelRow.value },
                                              { "contactIndex", row.contactIndex },
                                              { "solverRowIndex", row.solverRowIndex },
                                              { "pipelineIndex", row.pipelineIndex },
                                              { "featureId", row.featureId },
                                              { "sourceGeneration", row.sourceGeneration },
                                              { "sourceBankEpoch", row.sourceBankEpoch },
                                              { "sourceTopologyVersion", row.sourceTopologyVersion },
                                              { "sourcePublicationVersion", row.sourcePublicationVersion },
                                              { "sourceHighDetail", row.sourceHighDetail },
                                              { "name", row.name },
                                              { "detail", row.detail } } );
    }

    const int selectedIndex = PeekSelectedEditorModelIndex( editorTools.Editor(), world.BodyStore() );
    const char* selectedName = "";

    if ( selectedIndex >= 0 && selectedIndex < world.SceneEntityCount() )
    {
        selectedName = world.Entities().At( selectedIndex ).displayName;
    }

    const bool gizmoVisible = selectedIndex >= 0 &&
                              ( editorTools.Editor().editorModeEnabled ||
                                editorTools.InspectGizmoInteractionActive( camera.mode, replay.input.inspectionActive ) );

    const bool replayPastPathVisible = replay.path.hasTarget && replay.path.pastPathVisible;
    const std::size_t predictionVisibleFrameCount = VisiblePredictionFrameCount( replay );
    const RunReplayPredictionState& predictionState = replay.prediction;
    const PredictionSimulationHashes predictionPrivateSimulationHashes = PredictionSimulationHashes::Build( replay.activePredictionFrames );
    const std::span<const RunReplayPredictionFrame> committedPredictionFrames = predictionState.CommittedFrames();
    const bool predictionPathVisible = ReplayPredictionPathVisible( replay );
    const bool predictionContactsIncomplete = ReplayPredictionContactsIncomplete( replay );
    uint64_t predictionSourceSolverHash = 0;
    uint64_t liveSolverHash = 0;
    const bool liveSolverHashStableAcrossPrediction = LiveSolverHashStableAcrossPrediction( replay,
                                                                                            &predictionSourceSolverHash,
                                                                                            &liveSolverHash );

    const float replaySolverTrackPosition = replay.solverTrackPosition;
    const float replaySolverPresentTrackPosition = replay.solverPresentTrackPosition;
    const bool replaySolverTrackAtPresent = ReplayAtPresentTrackPosition( replaySolverTrackPosition,
                                                                          replaySolverPresentTrackPosition );

    const bool predictionScrubFrameActive = replay.currentPredictionFrame != nullptr;
    bool predictionTargetDisplacementValid = false;
    Vector3 predictionTargetFirst = ZERO_VECTOR;
    Vector3 predictionTargetLast = ZERO_VECTOR;
    float predictionTargetDisplacement = 0.0f;
    predictionTargetDisplacementValid = TryPredictionTargetDisplacement( replay, predictionTargetDisplacement,
                                                                         &predictionTargetFirst, &predictionTargetLast );

    const ReplayPredictionBaselineSnapshot& predictionBaseline = predictionState.baseline;
    const bool predictionBaselineVisible = predictionBaseline.valid && predictionBaseline.comparisonActive;
    int predictionAuthoredWallBrickCount = 0;
    int replayCauseTreeMaximumDepth = 0;
    int predictionAffectedWallBrickCount = 0;
    int predictionMovedWallBrickCount = 0;
    int predictionToppledWallBrickCount = 0;
    int predictionSustainedToppledWallBrickCount = 0;
    int predictionSettledWallBrickCount = 0;
    const RunReplayPredictionFrame* predictionFirstFrame = committedPredictionFrames.empty()
                                                               ? nullptr
                                                               : &committedPredictionFrames.front();

    const RunReplayPredictionFrame* predictionLastFrame = committedPredictionFrames.empty()
                                                              ? nullptr
                                                              : &committedPredictionFrames.back();

    const auto findPredictionBodyByModelRow = []( const RunReplayPredictionFrame* sample,
                                                  int modelIndex ) -> const RunReplayPredictionBodySample*
    {
        if ( !sample )
        {
            return nullptr;
        }

        for ( const RunReplayPredictionBodySample& body : sample->bodies )
        {
            if ( body.modelRow.value == modelIndex )
            {
                return &body;
            }
        }

        return nullptr;
    };

    const auto orientationDeltaSquared = []( const RunReplayPredictionBodySample& first,
                                            const RunReplayPredictionBodySample& second )
    {
        float firstX = 0.0f;

        float firstY = 0.0f;
        float firstZ = 0.0f;
        float firstW = 1.0f;
        float secondX = 0.0f;
        float secondY = 0.0f;
        float secondZ = 0.0f;
        float secondW = 1.0f;
        first.orientation.GetComponents( firstX, firstY, firstZ, firstW );
        second.orientation.GetComponents( secondX, secondY, secondZ, secondW );
        const float directDelta = ( firstX - secondX ) * ( firstX - secondX ) + ( firstY - secondY ) * ( firstY - secondY ) +
                                  ( firstZ - secondZ ) * ( firstZ - secondZ ) + ( firstW - secondW ) * ( firstW - secondW );

        const float antipodalDelta = ( firstX + secondX ) * ( firstX + secondX ) +
                                     ( firstY + secondY ) * ( firstY + secondY ) +
                                     ( firstZ + secondZ ) * ( firstZ + secondZ ) +
                                     ( firstW + secondW ) * ( firstW + secondW );

        return (std::min)( directDelta, antipodalDelta );
    };

    for ( int modelIndex = 0; modelIndex < world.SceneEntityCount(); ++modelIndex )
    {
        const SceneEntityRecord& entity = world.Entities().At( modelIndex );

        if ( strncmp( entity.displayName, "prediction_wall_brick_", 22u ) != 0 )
        {
            continue;
        }

        ++predictionAuthoredWallBrickCount;
        const bool affected = std::any_of( predictionState.futureNodeCache.futureNodes.begin(),
                                           predictionState.futureNodeCache.futureNodes.end(),
                                           [&]( const RunReplayPathTraceNode& node )
                                           { return node.modelRow.value == modelIndex; } );

        predictionAffectedWallBrickCount += affected ? 1 : 0;

        if ( !predictionFirstFrame || !predictionLastFrame )
        {
            continue;
        }

        const RunReplayPredictionBodySample* firstBody = findPredictionBodyByModelRow( predictionFirstFrame, modelIndex );

        const RunReplayPredictionBodySample* lastBody = findPredictionBodyByModelRow( predictionLastFrame, modelIndex );
        const Physics::PhysicsColliderHandle colliderHandle = world.Colliders().HandleForModelIndex( modelIndex );
        const Physics::ColliderRecord* collider = world.Colliders().RecordForHandle( colliderHandle );
        const SkullbonezCore::Math::CollisionDetection::BoundingBox*
            wallBrickShape = collider ? SkullbonezCore::Math::CollisionDetection::GetShapeIf<
                                            SkullbonezCore::Math::CollisionDetection::BoundingBox>( &collider->shape )
                                      : nullptr;

        const auto grounded = [&]( const RunReplayPredictionBodySample& body )
        {
            if ( !wallBrickShape )
            {
                return false;
            }

            // The 200-box fixture owns a flat y=0 terrain plane. A box is on
            // that plane when its oriented support point is within the contact
            // tolerance; a sleeper resting on another brick does not qualify.
            constexpr float GROUND_CONTACT_TOLERANCE = 0.05f;
            SkullbonezCore::Math::Orientation::Quaternion orientation = body.orientation;
            const RotationMatrix rotation = orientation.GetOrientationMatrix();
            const float verticalExtent = rotation.SupportExtentY( wallBrickShape->GetHalfExtents() );
            return body.position.y - verticalExtent <= GROUND_CONTACT_TOLERANCE;
        };

        if ( firstBody && lastBody && VectorMagSquared( lastBody->position - firstBody->position ) > 0.000001f )
        {
            ++predictionMovedWallBrickCount;
        }

        const bool toppledAtEnd = lastBody && lastBody->sleeping && grounded( *lastBody );
        predictionToppledWallBrickCount += toppledAtEnd ? 1 : 0;
        bool toppledThroughoutFinalSecond = toppledAtEnd;
        bool settledThroughoutFinalSecond = firstBody && lastBody;
        const std::size_t finalSecondStart = committedPredictionFrames.size() > 120u
                                                 ? committedPredictionFrames.size() - 121u
                                                 : 0u;

        for ( std::size_t frameIndex = finalSecondStart; ( toppledThroughoutFinalSecond || settledThroughoutFinalSecond ) &&
                                                         frameIndex < committedPredictionFrames.size();
              ++frameIndex )
        {
            const RunReplayPredictionBodySample*
                finalSecondBody = findPredictionBodyByModelRow( &committedPredictionFrames[frameIndex], modelIndex );

            if ( toppledThroughoutFinalSecond )
            {
                toppledThroughoutFinalSecond = finalSecondBody && finalSecondBody->sleeping && grounded( *finalSecondBody );
            }

            if ( settledThroughoutFinalSecond )
            {
                // Invariant: horizon completeness requires the whole authored
                // wall to be motionless for its final second. The one-micron
                // position bound is only a completion predicate; frame-exact
                // packet hashes remain the visual-parity oracle.
                constexpr float ONE_MICRON_SQUARED = 0.000000000001f;
                settledThroughoutFinalSecond = finalSecondBody &&
                                               VectorMagSquared( finalSecondBody->position - lastBody->position ) <=
                                                   ONE_MICRON_SQUARED &&
                                               VectorMagSquared( finalSecondBody->linearVelocity ) <= ONE_MICRON_SQUARED &&
                                               orientationDeltaSquared( *finalSecondBody, *lastBody ) <= ONE_MICRON_SQUARED;
            }
        }
        predictionSustainedToppledWallBrickCount += toppledThroughoutFinalSecond ? 1 : 0;
        predictionSettledWallBrickCount += settledThroughoutFinalSecond ? 1 : 0;
    }

    for ( const RunReplayCauseTreeRow& row : replay.causeTree.rows )
    {
        replayCauseTreeMaximumDepth = (std::max)( replayCauseTreeMaximumDepth, row.depth );
    }

    // Automation has the clamped Replay anchor but not a second viewport copy.
    // The anchor's reachable right/bottom margins provide the minimum viewport
    // that reproduces the exact effective attachment width, including compact
    // cases where the drawer has already shrunk.
    const int causeInspectorLayoutWidth = (std::max)( 1, replay.causeTree.x + replay.causeTree.width + 8 );
    const int causeInspectorLayoutHeight = (std::max)( 1, replay.causeTree.y + replay.causeTree.height + 8 );
    const ReplayCauseInspectorLayout
        causeInspectorLayout = BuildReplayCauseInspectorLayout( replay.causeInspection, replay.causeTree,
                                                                causeInspectorLayoutWidth, causeInspectorLayoutHeight,
                                                                replay.causeInspection.detailVisible ? 1.0f : 0.0f );
    const auto inspectorRect = []( const UI::UIRect& rect )
    { return Json { { "x", rect.x }, { "y", rect.y }, { "width", rect.w }, { "height", rect.h } }; };
    Json causeInspectorTabBounds = Json::array();

    for ( const UI::UIRect& tab : causeInspectorLayout.tabs )
    {
        causeInspectorTabBounds.push_back( inspectorRect( tab ) );
    }

    PredictionTrajectoryFingerprint predictionTrajectoryFingerprint = BuildPredictionTrajectoryFingerprint( replay );

    if ( m_replayVisualFidelityTrajectoryCaptured )
    {
        // The V0 oracle describes the completed prediction reveal. Report the
        // fingerprint frozen at its last presented tick rather than any later
        // non-presenting verification scratch state.
        predictionTrajectoryFingerprint.hash = m_replayVisualFidelityTrajectoryHash;
        predictionTrajectoryFingerprint.recordCount = static_cast<std::size_t>( m_replayVisualFidelityTrajectoryRecordCount );

        predictionTrajectoryFingerprint.pointCount = static_cast<std::size_t>( m_replayVisualFidelityTrajectoryPointCount );
    }

    const ReplayTrajectorySubmissionProbeStats& predictionSubmissionProbe = replay.trajectorySubmission;
    std::size_t predictionRetainedEntryMarkerCount = 0;
    std::size_t predictionRetainedRestMarkerCount = 0;
    std::size_t predictionRetainedHorizonMarkerCount = 0;

    // Why: prediction visual regressions are often spatial, so the interaction
    // report records the retained marker inventory that backs screenshot proof.
    for ( std::size_t i = 0; i < predictionState.futureNodeCache.retainedMarkerCount; ++i )
    {
        const ReplayPredictionRetainedMarker& marker = predictionState.futureNodeCache.retainedMarkers[i];

        if ( marker.hasEntryPose )
        {
            ++predictionRetainedEntryMarkerCount;
        }

        if ( marker.hasRestPose )
        {
            ++predictionRetainedRestMarkerCount;
        }

        if ( marker.hasHorizonPose )
        {
            ++predictionRetainedHorizonMarkerCount;
        }
    }

    Json directorPhaseCameraEye = nullptr;
    Json directorPhaseCameraView = nullptr;
    Json directorPhaseCameraUp = nullptr;
    Json directorPhaseRevealRate = nullptr;
    const char* directorPhaseName = "";
    const char* directorPhaseStylePath = "";
    const DemoDirectorPlaybackState& director = camera.director;

    if ( director.hasActiveShotList && director.currentPhaseIndex >= 0 &&
         director.currentPhaseIndex < director.activeShotList.phaseCount )
    {
        const DemoPhase& phase = director.activeShotList.phases[static_cast<std::size_t>( director.currentPhaseIndex )];
        directorPhaseName = phase.name;
        directorPhaseStylePath = phase.stylePath;
        directorPhaseCameraEye = Vec3Json( phase.camera.eye );
        directorPhaseCameraView = Vec3Json( phase.camera.view );
        directorPhaseCameraUp = Vec3Json( phase.camera.up );
        directorPhaseRevealRate = phase.revealRate;
    }

    const SkullbonezCore::Core::MainMemoryReplayStats replayMemoryStats = replay.memoryStats;
    uint64_t trajectoryDroppedTotal = 0;

    for ( std::size_t laneIndex = 0; laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT;
          ++laneIndex )
    {
        trajectoryDroppedTotal += replayMemoryStats.trajectory.droppedSegments[laneIndex];
    }

    Json report;
    report["ok"] = !status.failed;
    report["scene"] = scenePath ? scenePath : "";
    report["script"] = scriptPath;
    report["framesRun"] = scene.currentFrame;
    report["actions"] = actions;
    report["assertions"] = assertions;
    report["screenshots"] = screenshots;
    report["replayVisualFidelity"] = Json { { "schemaVersion", 2 },
                                            { "startFrame", m_replayVisualFidelityStartFrame },
                                            { "tickCount", m_replayVisualFidelityTicks.size() },
                                            { "offlineProjectionComplete", m_replayVisualOfflineProjectionComplete },
                                            { "ticks", replayVisualFidelityTickRows } };

    report["replayCausalProof"] = Json { { "schemaVersion", 2 },
                                         { "singleRevealGeneration", true },
                                         { "singlePresentedCascade", true },
                                         { "targetId", replay.visualPacket.header.targetId.value },
                                         { "tickCount", m_replayCausalProofTicks.size() },
                                         { "topologyCount", m_replayCausalTopology.size() },
                                         { "topology", replayCausalTopologyRows },
                                         { "ticks", replayCausalTicks } };

    report["replayArtifact"] = Json { { "schemaVersion", 4 },
                                      { "saved", replayArtifactSaved },
                                      { "path", replayArtifactPath },
                                      { "sampleCount", replayArtifactResult.sampleCount },
                                      { "bodyDictionaryCount", replayArtifactResult.bodyDictionaryCount },
                                      { "solverHashCount", replayArtifactResult.solverHashCount },
                                      { "solverCheckpointCount", replayArtifactResult.solverCheckpointCount },
                                      { "eventCount", replayArtifactResult.eventCount },
                                      { "eventCursorCount", replayArtifactResult.eventCursorCount },
                                      { "visualPacketCount", replayArtifactResult.visualPacketCount },
                                      { "visualPredictionHash",
                                        FormatPredictionHash( replayArtifactResult.visualPredictionHash ) },
                                      { "fileBytes", replayArtifactResult.fileBytes } };

    report["failure"] = status.failure;
    report
        ["finalState"] = Json { { "cameraMode", CameraModeName( camera.mode ) },
                                { "directorShotListLoaded", camera.director.hasActiveShotList },
                                { "directorPhaseIndex", camera.director.currentPhaseIndex },
                                { "directorPhaseCount", camera.director.activeShotList.phaseCount },
                                { "directorGrabbed", camera.director.grabbed },
                                { "directorShotListPath", camera.director.activeShotListPath },
                                { "directorPhaseName", directorPhaseName },
                                { "directorPhaseStylePath", directorPhaseStylePath },
                                { "directorPhaseRevealRate", directorPhaseRevealRate },
                                { "directorAppliedStylePhaseIndex", camera.director.appliedStylePhaseIndex },
                                { "directorAppliedStylePath", camera.director.appliedStylePath },
                                { "directorAppliedStyleCount", camera.director.appliedStyleCount },
                                { "directorAppliedRevealRatePhaseIndex", camera.director.appliedRevealRatePhaseIndex },
                                { "directorAppliedRevealRate", camera.director.appliedRevealRate },
                                { "directorAppliedRevealRateCount", camera.director.appliedRevealRateCount },
                                { "directorPhaseCameraEye", directorPhaseCameraEye },
                                { "directorPhaseCameraView", directorPhaseCameraView },
                                { "directorPhaseCameraUp", directorPhaseCameraUp },
                                { "workspace", WorkspaceName( interaction.Workspace() ) },
                                { "owner", OwnerName( interaction.Owner() ) },
                                { "selectedObject", selectedName },
                                { "selectedModelIndex", selectedIndex },
                                { "gizmoVisible", gizmoVisible },
                                { "mousePickupActive",
                                  interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag },
                                { "launcherRayActive", runtimeTools.Laser().HasActiveShots() },
                                { "memoryOverlayEnabled", ui.IsMemoryOverlayEnabled() },
                                { "replayPredictionEnabled", predictionState.enabled },
                                { "predictionDetailMode",
                                  replay.predictionDetailMode == ReplayPredictionDetailMode::High ? "High" : "Low" },
                                { "predictionCauseWindowAvailable",
                                  !replay.causeTree.rows.empty() &&
                                      ReplayPredictionCauseWindowAvailable( replay.predictionDetailMode,
                                                                            replay.causeTree.rows.front().prediction ) },
                                { "predictionHorizonSeconds", predictionState.simulation.horizonSeconds },
                                { "predictionRevealSecondsPerSecond", predictionState.revealClock.secondsPerSecond },
                                { "predictionBuildMode", ReplayPredictionBuildModeName( predictionState.build.buildMode ) },
                                { "predictionMeasuredTicksPerMs",
                                  predictionState.simulation.measuredTicksPerMs.load( std::memory_order_acquire ) },
                                { "predictionLastBuildWallMs", predictionState.build.lastBuildWallMs },
                                { "predictionPendingLatestRestart", predictionState.build.pendingLatestRestart },
                                { "predictionSupersededRestartCount", predictionState.build.supersededRestartCount },
                                { "predictionLatestRestartBeginCount", predictionState.build.latestRestartBeginCount },
                                { "replayPathTarget", replay.path.hasTarget ? replay.path.targetName : "" },
                                { "replayPathTargetId", replay.path.targetId.value },
                                { "replayPathTargetCount", static_cast<int>( replay.path.targets.size() ) },
                                { "predictionPrivateMutualGravityEnabled",
                                  predictionState.simulation.predictionWorldForces.mutualGravity.enabled },
                                { "predictionDrawListShowAllFuturePaths",
                                  ReplayPredictionPathPresentationShowsAllBodies( ReplayPrediction::PresentationViewFromState( predictionState, true )
                                                                                      .pathPresentation ) },
                                { "predictionEvidenceBuildBeginCount", replay.predictionEvidenceCapture.buildBeginCount },
                                { "predictionEvidenceConsumerAcquireCount",
                                  replay.predictionEvidenceCapture.consumerAcquireCount },
                                { "predictionEvidenceConsumerReleaseCount",
                                  replay.predictionEvidenceCapture.consumerReleaseCount },
                                { "predictionEvidenceConsumerActive", replay.predictionEvidenceCapture.consumerActive },
                                { "predictionEvidenceSealedFrameCount", replay.predictionEvidenceCapture.sealedFrameCount },
                                { "predictionEvidenceCopiedContactCount",
                                  replay.predictionEvidenceCapture.copiedContactCount },
                                { "predictionEvidenceCopiedPipelineCount",
                                  replay.predictionEvidenceCapture.copiedPipelineCount },
                                { "predictionEvidenceBuildPublishedFrameCount",
                                  replay.predictionEvidenceMemory.build.publishedFrameCount },
                                { "predictionEvidenceCommittedPublishedFrameCount",
                                  replay.predictionEvidenceMemory.committed.publishedFrameCount },
                                { "predictionEvidenceCurrentCapacityBytes",
                                  replay.predictionEvidenceMemory.currentCapacityBytes },
                                { "predictionEvidenceCurrentContactCapacityBytes",
                                  replay.predictionEvidenceMemory.currentContactCapacityBytes },
                                { "predictionEvidenceCurrentPipelineCapacityBytes",
                                  replay.predictionEvidenceMemory.currentPipelineCapacityBytes },
                                { "predictionEvidenceCurrentFrameCapacityBytes",
                                  replay.predictionEvidenceMemory.currentFrameCapacityBytes },
                                { "predictionEvidenceReleaseCheckpointCount",
                                  replay.predictionEvidenceMemory.releaseCheckpointCount },
                                { "predictionEvidenceLastReleaseBeforeCapacityBytes",
                                  replay.predictionEvidenceMemory.lastReleaseBeforeCapacityBytes },
                                { "predictionEvidenceLastReleaseAfterCapacityBytes",
                                  replay.predictionEvidenceMemory.lastReleaseAfterCapacityBytes },
                                { "predictionEvidenceLastReleaseBeforeReplayTotalBytes",
                                  replay.memoryStats.predictionEvidence.lastReleaseBeforeReplayTotalBytes },
                                { "predictionEvidenceLastReleaseAfterReplayTotalBytes",
                                  replay.memoryStats.predictionEvidence.lastReleaseAfterReplayTotalBytes },
                                { "predictionEvidenceLastReleaseBeforeCategoryTotalBytes",
                                  replay.memoryStats.predictionEvidence.lastReleaseBeforeCategoryTotalBytes },
                                { "predictionEvidenceLastReleaseAfterCategoryTotalBytes",
                                  replay.memoryStats.predictionEvidence.lastReleaseAfterCategoryTotalBytes },
                                { "predictionEvidenceMemoryReconciled",
                                  SkullbonezCore::Core::MainMemoryReplayPredictionEvidenceReleaseReconciles( replay.memoryStats ) },
                                { "replayTrackedCapacityBytes", replay.memoryStats.totalBytes },
                                { "predictionPrivateSimulationHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.All() ) },
                                { "predictionPrivateFrameIndexHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.FrameIndex() ) },
                                { "predictionPrivatePoseHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.Pose() ) },
                                { "predictionPrivateVelocityHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.Velocity() ) },
                                { "predictionPrivateSleepHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.Sleep() ) },
                                { "predictionPrivateContactHash",
                                  FormatPredictionHash( predictionPrivateSimulationHashes.Contacts() ) },
                                { "predictionSourceFrameIndex", predictionState.simulation.sourceFrameIndex },
                                { "predictionSourceSimulationSeconds", predictionState.simulation.sourceSimulationSeconds },
                                { "predictionTrajectoryRecords", predictionTrajectoryRows },
                                { "predictionFutureNodes", predictionFutureNodeRows },
                                { "replayInterceptValid", replay.intercept.valid },
                                { "replayIntercept", replay.intercept.intercept },
                                { "replayInterceptMissDistance", replay.intercept.missDistance },
                                { "replayInterceptEtaSeconds", replay.intercept.etaSeconds },
                                { "replayPastTrajectoryFullRebuildCount", replay.path.pastTrajectory.fullRebuildCount },
                                { "replayPastTrajectoryIncrementalTrimCount",
                                  replay.path.pastTrajectory.incrementalTrimCount },
                                { "replayPastTrajectoryPublishedPointCount",
                                  static_cast<int>( ReplayPastTrajectoryPublishedPointCount( replay ) ) },
                                { "replayPastPathVisible", replayPastPathVisible },
                                { "predictionPathVisible", predictionPathVisible },
                                { "predictionContactsIncomplete", predictionContactsIncomplete },
                                { "predictionBaselineVisible", predictionBaselineVisible },
                                { "predictionBaselineRootPointCount",
                                  static_cast<int>( predictionBaseline.rootPolyline.size() ) },
                                { "predictionBaselineBodyPoseCount",
                                  static_cast<int>( predictionBaseline.bodyPoses.size() ) },
                                { "predictionDivergenceValid", predictionBaseline.divergenceValid },
                                { "predictionDivergenceUnits", predictionBaseline.divergenceUnits },
                                { "liveSolverHashStableAcrossPrediction", liveSolverHashStableAcrossPrediction },
                                { "predictionSourceSolverHash", predictionSourceSolverHash },
                                { "liveSolverHash", liveSolverHash },
                                { "predictionActiveFrameCount", static_cast<int>( predictionVisibleFrameCount ) },
                                { "predictionFrameCount", static_cast<int>( committedPredictionFrames.size() ) },
                                { "predictionBuildFrameCount",
                                  static_cast<int>( predictionState.PublishedBuildFrameCount() ) },
                                { "predictionTargetDisplacementValid", predictionTargetDisplacementValid },
                                { "predictionTargetFirst", Vec3Json( predictionTargetFirst ) },
                                { "predictionTargetLast", Vec3Json( predictionTargetLast ) },
                                { "predictionTargetDisplacement", predictionTargetDisplacement },
                                { "predictionTrajectoryFingerprintReady", predictionTrajectoryFingerprint.Ready() },
                                { "predictionTrajectoryFingerprint",
                                  FormatPredictionHash( predictionTrajectoryFingerprint.hash ) },
                                { "predictionTrajectoryRecordCount",
                                  static_cast<int>( predictionTrajectoryFingerprint.recordCount ) },
                                { "predictionTrajectoryPointCount",
                                  static_cast<int>( predictionTrajectoryFingerprint.pointCount ) },
                                { "predictionAppearanceInvalidationCount", replay.predictionAppearanceInvalidationCount },
                                { "shadowPassExecuted", renderSnapshot.shadowPassExecuted },
                                { "terrainShadowValid", renderSnapshot.terrainShadowValid },
                                { "objectShadowValid", renderSnapshot.objectShadowValid },
                                { "reflectionPassExecuted", renderSnapshot.reflectionPassExecuted },
                                { "predictionTrajectorySubmissionStable", predictionSubmissionProbe.stableWindowReady },
                                { "predictionTrajectorySubmissionFrameCount", predictionSubmissionProbe.stableFrameCount },
                                { "predictionTrajectorySubmissionObservedFrameCount",
                                  predictionSubmissionProbe.observedFrameCount },
                                { "predictionTrajectorySubmissionHash",
                                  FormatPredictionHash( predictionSubmissionProbe.stableHash ) },
                                { "predictionTrajectorySubmissionVertexBytes", predictionSubmissionProbe.vertexBytes },
                                { "predictionTrajectorySubmissionVertexCount",
                                  static_cast<int>( predictionSubmissionProbe.vertexCount ) },
                                { "predictionTrajectorySubmissionSegmentCount",
                                  static_cast<int>( predictionSubmissionProbe.segmentCount ) },
                                { "predictionTrajectoryDroppedSegmentCount", trajectoryDroppedTotal },
                                { "predictionTrajectoryDroppedSegments",
                                  Json { { "pastRoot",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::PastRoot )] },
                                         { "futureRoot",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::FutureRoot )] },
                                         { "futureChildIncoming",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::
                                                                                                                      FutureChildIncoming )] },
                                         { "futureChildOutgoing",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::
                                                                                                                      FutureChildOutgoing )] },
                                         { "retainedTrail",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail )] },
                                         { "baselineRoot",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot )] },
                                         { "causalMarker",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker )] },
                                         { "auxiliaryTrail",
                                           replayMemoryStats.trajectory.droppedSegments[static_cast<std::size_t>( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::AuxiliaryTrail )] } } },
                                { "predictionTrajectorySubmissionFirstFrame", predictionSubmissionProbe.firstFrame },
                                { "predictionTrajectorySubmissionLastFrame", predictionSubmissionProbe.lastFrame },
                                { "predictionTrajectorySteadyStateNoReserveGrowth",
                                  predictionSubmissionProbe.noReserveGrowth },
                                { "predictionTrajectoryReserveGrowthEventsAtStart",
                                  predictionSubmissionProbe.reserveGrowthEventsAtStart },
                                { "predictionTrajectoryReserveGrowthEventsAtEnd",
                                  predictionSubmissionProbe.reserveGrowthEventsAtEnd },
                                { "predictionFutureTreeReadinessDropCount",
                                  predictionSubmissionProbe.futureTreeReadinessDropCount },
                                { "predictionFutureNodeCount",
                                  static_cast<int>( predictionState.futureNodeCache.futureNodes.size() ) },
                                { "predictionAuthoredWallBrickCount", predictionAuthoredWallBrickCount },
                                { "predictionAffectedWallBrickCount", predictionAffectedWallBrickCount },
                                { "predictionMovedWallBrickCount", predictionMovedWallBrickCount },
                                { "predictionToppledWallBrickCount", predictionToppledWallBrickCount },
                                { "predictionSustainedToppledWallBrickCount", predictionSustainedToppledWallBrickCount },
                                { "predictionSettledWallBrickCount", predictionSettledWallBrickCount },
                                { "predictionGenerationCount", predictionState.build.generationBeginCount },
                                { "predictionFutureNodeBuildFrameCount",
                                  static_cast<int>( predictionState.futureNodeCache.futureNodesBuiltFrameCount ) },
                                { "predictionRetainedEntryMarkerCount",
                                  static_cast<int>( predictionRetainedEntryMarkerCount ) },
                                { "predictionRetainedRestMarkerCount",
                                  static_cast<int>( predictionRetainedRestMarkerCount ) },
                                { "predictionRetainedHorizonMarkerCount",
                                  static_cast<int>( predictionRetainedHorizonMarkerCount ) },
                                { "replayCauseTreeRowCount", static_cast<int>( replay.causeTree.rows.size() ) },
                                { "replayCauseTreeMaximumDepth", replayCauseTreeMaximumDepth },
                                { "replayCauseTreeSelectedRow", replay.causeTree.selectedRow },
                                { "replayCauseTreeHasWindowPlacement", replay.causeTree.hasWindowPlacement },
                                { "replayCauseTreeWindowX", replay.causeTree.x },
                                { "replayCauseTreeWindowY", replay.causeTree.y },
                                { "replayCauseTreeWindowWidth", replay.causeTree.width },
                                { "replayCauseTreeWindowHeight", replay.causeTree.height },
                                { "replayCauseTreeMouseX", replay.causeTree.mouseX },
                                { "replayCauseTreeMouseY", replay.causeTree.mouseY },
                                { "replayCauseTreePointerBlocked", replay.causeTree.pointerBlocked },
                                { "replayCauseInspectorHierarchyBounds", inspectorRect( causeInspectorLayout.hierarchy ) },
                                { "replayCauseInspectorDrawerBounds", inspectorRect( causeInspectorLayout.drawer ) },
                                { "replayCauseInspectorVisibleDrawerBounds",
                                  inspectorRect( causeInspectorLayout.visibleDrawer ) },
                                { "replayCauseInspectorTargetDrawerBounds",
                                  inspectorRect( causeInspectorLayout.targetDrawer ) },
                                { "replayCauseInspectorCompoundBounds", inspectorRect( causeInspectorLayout.compound ) },
                                { "replayCauseInspectorTargetCompoundBounds",
                                  inspectorRect( causeInspectorLayout.targetCompound ) },
                                { "replayCauseInspectorHierarchyTitleBounds",
                                  inspectorRect( causeInspectorLayout.hierarchyTitle ) },
                                { "replayCauseInspectorDrawerTitleBounds",
                                  inspectorRect( causeInspectorLayout.drawerTitle ) },
                                { "replayCauseInspectorCloseBounds", inspectorRect( causeInspectorLayout.drawerClose ) },
                                { "replayCauseInspectorTabBounds", causeInspectorTabBounds },
                                { "replayCauseInspectorContentBounds", inspectorRect( causeInspectorLayout.content ) },
                                { "replayCauseInspectorDrawerScrollbarBounds",
                                  inspectorRect( causeInspectorLayout.drawerScrollbar ) },
                                { "replayCauseInspectorHierarchyScrollbarBounds",
                                  inspectorRect( causeInspectorLayout.hierarchyScrollbar ) },
                                { "replayCauseInspectorSharedSeamBounds", inspectorRect( causeInspectorLayout.sharedSeam ) },
                                { "replayCauseInspectorResizeBounds", inspectorRect( causeInspectorLayout.resize ) },
                                { "replayCauseInspectionMode", static_cast<int>( replay.causeInspection.mode ) },
                                { "replayCauseInspectionDetailVisible", replay.causeInspection.detailVisible },
                                { "replayCauseInspectionDetailAvailability",
                                  ReplayCauseSolverDetailAvailabilityName( replay.causeInspection.solverDetailAvailability ) },
                                { "replayCauseTreeRows", replayCauseTreeRows },
                                { "replayCauseInspectionSelectedRow", replay.causeInspection.selectedRow },
                                { "replayCauseInspectionTargetFrame", replay.causeInspection.targetFrame },
                                { "replayCauseInspectionPresentedFrame", replay.causeInspection.presentedFrame },
                                { "replayCauseInspectionContactRowCount",
                                  static_cast<int>( replay.causeInspection.solverDetailContactRowCount ) },
                                { "replayCauseInspectionPipelineRecordCount",
                                  static_cast<int>( replay.causeInspection.solverDetailPipelineRecordCount ) },
                                { "replayCauseInspectionFirstVisibleRow", replay.causeInspection.solverDetailFirstRow },
                                { "replayCauseInspectionFixedStorageBytes",
                                  static_cast<uint64_t>( sizeof( ReplayCauseInspection ) ) },
                                { "replayActiveTrack", ReplayTrackName( replay.scrubber.activeTrack ) },
                                { "replayHistoricalSamplePaused", replay.scrubber.historicalSamplePaused },
                                { "replaySolverTrackPosition", replaySolverTrackPosition },
                                { "replaySolverPresentTrackPosition", replaySolverPresentTrackPosition },
                                { "replaySolverTrackAtPresent", replaySolverTrackAtPresent },
                                { "predictionScrubFrameActive", predictionScrubFrameActive },
                                { "replayFutureNodeCount",
                                  static_cast<int>( replay.prediction.futureNodeCache.futureNodes.size() ) } };

    std::ofstream output;

    if ( !RuntimeFileWriter::OpenTextFile( m_path, output ) )
    {
        m_written = true;
        status.failed = true;
        strcpy_s( status.failure, sizeof( status.failure ), "failed to open interaction report path" );
        return m_resultDiagnostics.Failure( "InteractionAutomation", status.failure );
    }

    output << report.dump( 2 ) << "\n";
    output.close();
    m_written = true;
    printf( "[interaction] Report written: %s ok=%d\n", m_path, status.failed ? 0 : 1 );
    return status.Result( m_resultDiagnostics );
}
