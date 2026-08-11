/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp
Purpose:
  Implements the bounded prediction presentation sub-owner.

Summary:
  Prediction may consume synchronous Replay path values, but Replay cannot name
  Prediction. Pose, ghost, focus-mask, trajectory, and packet state therefore
  remain physically and logically owned by ReplayPredictionPresentation.

Invariants:
  - Stable scene identity resolves every dense model-row hint before use.
  - Prediction frame pose application marks matched rows before hiding unmatched bodies.
  - Ghost and packet publications borrow Prediction spans only for the current frame.
  - No Replay owner pointer or reference is retained between commands.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayPredictionPresentation.h"

#include "../../Core/Profiler.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <algorithm>

namespace SkullbonezCore::Runtime
{
namespace
{
constexpr int REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET = 120;

template <typename T> uint64_t VectorCapacityBytes( const std::vector<T>& values )
{
    return static_cast<uint64_t>( values.capacity() * sizeof( T ) );
}

bool IsRagdollPart( std::span<const Rendering::RenderInstancePresentationRecord> records, int modelIndex )
{
    return modelIndex >= 0 && modelIndex < static_cast<int>( records.size() ) &&
           records[static_cast<std::size_t>( modelIndex )].simpleRagdollPart;
}

const Physics::PhysicsBodyRecord* ResolveReplayBody( const Physics::PhysicsBodyStore& bodyStore,
                                                     Physics::PhysicsSceneObjectId id, int modelIndexHint, int modelCount,
                                                     int& outModelIndex )
{
    outModelIndex = -1;

    if ( !id.IsValid() )
    {
        return nullptr;
    }

    const Physics::PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, modelIndexHint );
    const int modelIndex = bodyStore.ModelIndexForHandle( body );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );

    if ( !record || record->sceneObjectId != id || modelIndex < 0 || modelIndex >= modelCount )
    {
        return nullptr;
    }

    outModelIndex = modelIndex;
    return record;
}

bool QueuePose( Rendering::RenderInstanceStore& renderInstances, const Physics::PhysicsBodyStore& bodyStore,
                const Physics::ColliderStore& colliderStore, const RunReplayPredictionBodySample& body )
{
    const Physics::PhysicsBodyHandle handle = bodyStore.HandleForSceneObjectId( body.id );
    const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( handle );
    const int modelIndex = bodyStore.ModelIndexForHandle( handle );

    if ( !record || record->sceneObjectId != body.id || modelIndex < 0 )
    {
        return false;
    }

    Math::Orientation::Quaternion orientation = body.orientation;
    orientation.Normalise();
    return renderInstances.OverridePose( modelIndex, body.id, body.position, orientation, colliderStore );
}

bool HideUnmatchedBodies( Rendering::RenderInstanceStore& renderInstances, const Physics::PhysicsBodyStore& bodyStore,
                          const Physics::ColliderStore& colliderStore, std::span<const uint8_t> matchedBodies,
                          int modelCount )
{
    bool queuedAny = false;
    const Math::Vector::Vector3 hiddenPosition( 0.0f, -100000.0f, 0.0f );

    for ( int modelIndex = 0; modelIndex < modelCount; ++modelIndex )
    {
        if ( matchedBodies[static_cast<std::size_t>( modelIndex )] != 0 )
        {
            continue;
        }

        const Physics::PhysicsBodyHandle body = bodyStore.HandleForModelIndex( modelIndex );
        const Physics::PhysicsBodyRecord* record = bodyStore.RecordForHandle( body );

        if ( !record || bodyStore.ModelIndexForHandle( body ) != modelIndex || !record->sceneObjectId.IsValid() )
        {
            continue;
        }

        queuedAny = renderInstances.OverridePose( modelIndex, record->sceneObjectId, hiddenPosition,
                                                  Math::Orientation::IDENTITY_QUATERNION, colliderStore ) ||
                    queuedAny;
    }

    return queuedAny;
}
} // namespace

ReplayPredictionPresentation::ReplayPredictionPresentation( Core::SbDiagnosticStore& resultDiagnostics,
                                                            Core::Profiler* profiler )
    : m_profiler( profiler ), m_retainedMarkerDrawList( resultDiagnostics )
{

    // Runtime allocation policy: focus masks are rewritten during replay render
    // passes, so the byte vector owns its full model-capacity storage up front.
    m_focusModelMask.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_renderPoseBodyMatched.fill( uint8_t { 0 } );
}


SkullbonezCore::Core::MainMemoryReplayTrajectoryStats
ReplayPredictionPresentation::TrajectoryVisualStatsSnapshot() const noexcept
{
    return m_trajectoryVisualStats;
}


ReplayTrajectorySubmissionProbeStats ReplayPredictionPresentation::TrajectorySubmissionProbeSnapshot() const noexcept
{
    return m_trajectorySubmissionProbe;
}


const ReplayVisualPacket& ReplayPredictionPresentation::PublishedVisualPacketView() const noexcept
{
    return m_publishedVisualPacket;
}


std::span<const ReplayPredictionGhostDrawRequest> ReplayPredictionPresentation::GhostDrawRequestsView() const noexcept
{
    return m_ghostDrawRequests;
}


const std::vector<uint8_t>& ReplayPredictionPresentation::FocusModelMaskView() const noexcept
{
    return m_focusModelMask;
}


ReplayPredictionPresentationMemoryStats ReplayPredictionPresentation::CollectMemoryStats() const noexcept
{
    ReplayPredictionPresentationMemoryStats stats;
    stats.ghostRequestCapacityBytes = VectorCapacityBytes( m_ghostDrawRequests );
    stats.focusModelMaskCapacityBytes = VectorCapacityBytes( m_focusModelMask );
    stats.ghostRequestCount = static_cast<uint64_t>( m_ghostDrawRequests.size() );
    stats.trajectory = m_trajectoryVisualStats;
    return stats;
}


void ReplayPredictionPresentation::ReserveRecordingBuffers()
{

    // Runtime allocation policy: ghost requests are replay-only overlay data.
    // Reserve during startup replay configuration, before steady interaction.
    m_ghostDrawRequests.reserve( REPLAY_PREDICTION_GHOST_REQUEST_CAPACITY );
}


bool ReplayPredictionPresentation::BuildFocusModelMask( const RunReplayPathVisualizerState& path,
                                                        const Physics::PhysicsBodyStore& bodyStore, int modelCount,
                                                        std::span<const RunReplayPathTraceNode> futureNodes )
{
    if ( !path.hasTarget || path.targetId.value == 0 || modelCount <= 0 ||
         modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        m_focusModelMask.clear();
        return false;
    }

    m_focusModelMask.assign( static_cast<std::size_t>( modelCount ), 0 );
    int markedCount = 0;
    const auto markBySceneObjectId = [&]( Physics::PhysicsSceneObjectId id, int preferredModelIndex )
    {
        if ( id.value == 0 )
        {
            return;
        }

        const Physics::PhysicsBodyHandle body = bodyStore.HandleForSceneObjectId( id, preferredModelIndex );
        const int resolvedIndex = bodyStore.ModelIndexForHandle( body );

        if ( resolvedIndex >= 0 && resolvedIndex < modelCount )
        {
            uint8_t& mask = m_focusModelMask[static_cast<std::size_t>( resolvedIndex )];

            if ( mask == 0 )
            {
                mask = 1;
                ++markedCount;
            }
        }
    };

    if ( path.targets.empty() )
    {
        markBySceneObjectId( path.targetId, path.targetModelRow.value );
    }
    else
    {
        for ( const RunReplayPathTarget& target : path.targets )
        {
            markBySceneObjectId( target.id, target.modelRow.value );
        }
    }

    for ( const RunReplayPathTraceNode& node : futureNodes )
    {
        markBySceneObjectId( node.id, node.modelRow.value );
    }

    if ( markedCount <= 0 || markedCount >= modelCount )
    {
        m_focusModelMask.clear();
        return false;
    }

    return true;
}


void ReplayPredictionPresentation::ClearGhostDrawRequests() noexcept
{
    m_ghostDrawRequests.clear();
}


bool ReplayPredictionPresentation::CanAppendGhostDrawRequests( std::size_t count ) const noexcept
{
    return m_ghostDrawRequests.size() + count <= m_ghostDrawRequests.capacity();
}


void ReplayPredictionPresentation::AppendGhostDrawRequest( const ReplayPredictionGhostDrawRequest& request )
{

    // Invariant: callers prove capacity before the bounded presentation pass;
    // replay steady-state rendering must never grow this vector.
    if ( m_ghostDrawRequests.size() < m_ghostDrawRequests.capacity() )
    {
        m_ghostDrawRequests.push_back( request );
    }
}


bool ReplayPredictionPresentation::HasGhostDrawRequests() const noexcept
{
    return !m_ghostDrawRequests.empty();
}


bool ReplayPredictionPresentation::PrepareRenderPoseBodyMatch( int modelCount ) noexcept
{
    if ( modelCount < 0 || modelCount > SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS )
    {
        return false;
    }

    std::fill( m_renderPoseBodyMatched.begin(), m_renderPoseBodyMatched.begin() + static_cast<std::size_t>( modelCount ),
               uint8_t { 0 } );

    return true;
}


bool ReplayPredictionPresentation::ApplyFrameForRender( Rendering::RenderInstanceStore& renderInstances,
                                                        const Physics::PhysicsBodyStore& bodyStore,
                                                        const Physics::ColliderStore& colliderStore,
                                                        const RunReplayPredictionFrame& frame )
{
    const int modelCount = renderInstances.Count();

    if ( !PrepareRenderPoseBodyMatch( modelCount ) )
    {
        return false;
    }

    std::span<uint8_t> matchedBodies( m_renderPoseBodyMatched.data(), static_cast<std::size_t>( modelCount ) );
    bool queuedBodies = false;

    for ( const RunReplayPredictionBodySample& body : frame.bodies )
    {
        int resolvedModelIndex = -1;

        if ( !ResolveReplayBody( bodyStore, body.id, body.modelRow.value, modelCount, resolvedModelIndex ) )
        {
            continue;
        }

        if ( QueuePose( renderInstances, bodyStore, colliderStore, body ) )
        {
            matchedBodies[static_cast<std::size_t>( resolvedModelIndex )] = 1;
            queuedBodies = true;
        }
    }

    return HideUnmatchedBodies( renderInstances, bodyStore, colliderStore, matchedBodies, modelCount ) || queuedBodies;
}

bool ReplayPredictionPresentation::BuildGhostDrawRequests( const ReplayPredictionPresentationView& prediction,
                                                           std::span<const Rendering::RenderInstancePresentationRecord> presentationRecords,
                                                           const Physics::PhysicsBodyStore& bodyStore )
{
    ClearGhostDrawRequests();
    const std::span<const RunReplayPredictionFrame> frames = prediction.frames;
    const bool drawLivePrediction = prediction.enabled && prediction.ragdollVisualsEnabled && frames.size() >= 2;
    const bool drawBaseline = prediction.baselineValid && prediction.baselineComparisonActive &&
                              prediction.ragdollVisualsEnabled && !prediction.baselineBodyPoses.empty();

    bool hasRagdollPart = false;

    for ( int index = 0; index < static_cast<int>( presentationRecords.size() ); ++index )
    {
        hasRagdollPart = hasRagdollPart || IsRagdollPart( presentationRecords, index );
    }

    if ( !hasRagdollPart )
    {
        return false;
    }

    const std::size_t liveCapacity = drawLivePrediction
                                         ? (std::min)( frames.size(), REPLAY_PREDICTION_GHOST_MAX_FRAMES + 1 ) *
                                               presentationRecords.size()
                                         : 0u;

    const std::size_t baselineCapacity = drawBaseline ? prediction.baselineBodyPoses.size() : 0u;

    if ( !CanAppendGhostDrawRequests( liveCapacity + baselineCapacity ) )
    {
        return false;
    }

    if ( drawBaseline )
    {
        for ( const ReplayPredictionBaselineBodyPose& pose : prediction.baselineBodyPoses )
        {
            if ( !pose.hasRestPose || !IsRagdollPart( presentationRecords, pose.modelRow.value ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelRow = pose.modelRow;
            request.position = pose.restPosition;
            request.orientation = pose.restOrientation;
            request.orientation.Normalise();
            request.alpha = 0.075f;
            request.tintR = 0.28f;
            request.tintG = 0.76f;
            request.tintB = 1.0f;
            request.tintStrength = 0.82f;
            AppendGhostDrawRequest( request );
        }
    }

    if ( !drawLivePrediction )
    {
        return HasGhostDrawRequests();
    }

    const std::size_t lastIndex = frames.size() - 1u;
    const std::size_t stride = (std::max)( std::size_t { 1u }, ( lastIndex + REPLAY_PREDICTION_GHOST_MAX_FRAMES - 1u ) /
                                                                   REPLAY_PREDICTION_GHOST_MAX_FRAMES );

    const ReplayFrameIndex lastFrame = frames.back().frameIndex;
    const auto appendGhostFrame = [&]( std::size_t index )
    {
        const RunReplayPredictionFrame& frame = frames[index];

        if ( frame.frameIndex == 0 )
        {
            return;
        }

        const float t = lastFrame > 0 ? std::clamp( static_cast<float>( frame.frameIndex ) / static_cast<float>( lastFrame ),
                                                    0.0f, 1.0f )
                                      : 1.0f;

        const float alpha = std::clamp( 0.055f + ( 1.0f - t ) * 0.105f, 0.045f, 0.18f );

        for ( const RunReplayPredictionBodySample& body : frame.bodies )
        {
            int resolvedModelIndex = -1;

            if ( !ResolveReplayBody( bodyStore, body.id, body.modelRow.value, static_cast<int>( presentationRecords.size() ),
                                     resolvedModelIndex ) ||
                 !IsRagdollPart( presentationRecords, resolvedModelIndex ) )
            {
                continue;
            }

            ReplayPredictionGhostDrawRequest request;
            request.modelRow.value = resolvedModelIndex;
            request.position = body.position;
            request.orientation = body.orientation;
            request.orientation.Normalise();
            request.alpha = alpha;
            AppendGhostDrawRequest( request );
        }
    };

    std::size_t farIndex = lastIndex;

    if ( farIndex % stride != 0 )
    {
        appendGhostFrame( farIndex );
        farIndex = ( farIndex / stride ) * stride;
    }

    for ( std::size_t index = farIndex; index >= stride; index -= stride )
    {
        appendGhostFrame( index );

        if ( index == stride )
        {
            break;
        }
    }

    return HasGhostDrawRequests();
}


bool ReplayPredictionPresentation::PrepareRetainedGeometryDrawList( const ReplayPredictionPresentationView& prediction, const RunReplayPathVisualizerState& path,
                                                                    const SceneEntityStore& entities, const Physics::ColliderStore& colliderStore, EditorTracer& frameTracer,
                                                                    const Core::ReplayTrajectoryAppearanceConfig& trajectoryAppearance )
{
    const bool retainedAppearanceChanged = m_retainedGeometry.SetAppearance( trajectoryAppearance );
    const bool markerAppearanceChanged = m_retainedMarkerDrawList.SetReplayTrajectoryAppearance( trajectoryAppearance );

    if ( retainedAppearanceChanged || markerAppearanceChanged )
    {

        // Invariant: packed retained records carry style values. A live UI edit
        // invalidates geometry only; prediction samples remain authoritative.
        m_retainedGeometry.Clear();
        m_retainedMarkerDrawList.Clear();
        m_retainedDrawListState.Reset();
        ++m_retainedDrawStreamId;
        ++m_retainedAppearanceInvalidationCount;
        m_retainedDrawPacketDirty = true;
    }

    if ( prediction.deterministicRevealEnabled )
    {

        // Invariant: deterministic reveal is the untouched frame-local oracle.
        // It must not mix retained provisional tails into the packet it hashes.
        m_retainedRenderingActive = false;
        return false;
    }

    const ReplayOverlay::ReplayPredictionDrawListUpdate
        drawListUpdate = ReplayOverlay::UpdateReplayPredictionDrawList( prediction, path, entities, colliderStore,
                                                                        m_retainedGeometry, m_retainedMarkerDrawList,
                                                                        m_retainedDrawListState );

    if ( drawListUpdate.reset )
    {
        ++m_retainedDrawStreamId;
    }

    if ( drawListUpdate.reset || drawListUpdate.appended )
    {
        m_retainedDrawPacketDirty = true;
    }

    ReplayOverlay::AppendReplayPredictionProvisionalTails( prediction, path, m_retainedDrawListState, colliderStore,
                                                           frameTracer );

    m_retainedRenderingActive = m_retainedDrawListState.valid;
    return m_retainedRenderingActive;
}


void ReplayPredictionPresentation::AttachRetainedPredictionGeometry( ReplayVisualPacket& packet,
                                                                     const Math::Vector::Vector3& cameraEye,
                                                                     const Math::Vector::Vector3& cameraUp )
{
    if ( !m_retainedRenderingActive )
    {
        return;
    }

    if ( m_retainedDrawPacketDirty )
    {
        PROFILE_SCOPED( "Frame/Replay/PublishRenderPacket/AttachRetained/RebuildRetainedPacket" );

        // Compact retained trajectory records are world-space and camera-neutral.
        // Camera values remain explicit publication inputs for the tracer packet.
        m_retainedDrawPacket = m_retainedMarkerDrawList.BuildReplayVisualPacket( cameraEye, cameraUp );
        m_retainedGeometry.PublishToPacket( m_retainedDrawPacket );
        m_retainedDrawPacketDirty = false;
        ++m_retainedDrawRevision;
    }

    ReplayVisualPacketOperations::AttachRetainedPredictionGeometry( packet, m_retainedDrawPacket, m_retainedDrawStreamId,
                                                                    m_retainedDrawRevision );
}


void ReplayPredictionPresentation::PublishVisualPacket( ReplayVisualPacket packet,
                                                        const ReplayPredictionPresentationView& prediction,
                                                        Physics::PhysicsSceneObjectId pathTargetId,
                                                        const ReplaySolverFrameSample* latestSolver,
                                                        uint64_t replayReserveGrowthEvents )
{
    const bool samePresentation = m_trajectorySubmissionProbe.presentationKeyValid &&
                                  m_trajectorySubmissionProbe.presentationTargetId == prediction.targetId.value &&
                                  m_trajectorySubmissionProbe.presentationSourceFrame == prediction.sourceFrame;

    if ( !samePresentation )
    {
        m_trajectorySubmissionProbe.presentationTargetId = prediction.targetId.value;
        m_trajectorySubmissionProbe.presentationSourceFrame = prediction.sourceFrame;
        m_trajectorySubmissionProbe.futureTreeReadinessDropCount = 0;
        m_trajectorySubmissionProbe.futureTreeReadySeen = false;
        m_trajectorySubmissionProbe.futureTreeReadyLastFrame = false;
        m_trajectorySubmissionProbe.presentationKeyValid = prediction.targetId.IsValid();
    }

    if ( m_trajectorySubmissionProbe.presentationKeyValid )
    {
        if ( m_trajectorySubmissionProbe.futureTreeReadySeen && m_trajectorySubmissionProbe.futureTreeReadyLastFrame &&
             !prediction.futureTreeReady )
        {
            ++m_trajectorySubmissionProbe.futureTreeReadinessDropCount;
        }

        m_trajectorySubmissionProbe.futureTreeReadySeen = m_trajectorySubmissionProbe.futureTreeReadySeen ||
                                                          prediction.futureTreeReady;

        m_trajectorySubmissionProbe.futureTreeReadyLastFrame = prediction.futureTreeReady;
    }

    packet.header.sourceFrame = prediction.sourceFrame;
    packet.header.revealFrame = prediction.revealFrame;
    packet.header.targetId = pathTargetId;
    packet.header.branchId = latestSolver ? latestSolver->branch.branchId : 0u;
    packet.header.eventCursor = latestSolver ? latestSolver->eventCursor : 0u;
    packet.header.topologyVersion = prediction.topologyVersion;
    packet.header.publishedFrameCount = static_cast<uint32_t>( prediction.frames.size() );
    packet.header.futureNodeCount = static_cast<uint32_t>( prediction.futureNodes.size() );
    const std::span<const ReplayPredictionGhostDrawRequest> ghostRequests = GhostDrawRequestsView();
    packet.header.ghostRequestCount = static_cast<uint32_t>( ghostRequests.size() );
    packet.header.replayReserveGrowthEvents = replayReserveGrowthEvents;
    packet.header.predictionEnabled = prediction.enabled;
    packet.header.predictionBuilding = prediction.building;
    packet.header.predictionComplete = prediction.complete;
    packet.trajectoryRecords = prediction.trajectoryRecords;
    packet.futureNodes = prediction.futureNodes;
    packet.retainedMarkers = prediction.retainedMarkers;
    packet.ghostRequests = ghostRequests;
    packet.trajectoryDiagnostics = TrajectoryVisualStatsSnapshot();
    StorePublishedVisualPacket( packet );
}


void ReplayPredictionPresentation::StorePublishedVisualPacket( ReplayVisualPacket packet )
{

    // Lifetime: spans point into fixed tracer or prediction reserves and remain
    // valid until the next frame clears those owners. No packet survives order.
    m_publishedVisualPacket = packet;
}


void ReplayPredictionPresentation::ResetTrajectoryVisualStats() noexcept
{
    m_trajectoryVisualStats = {};
}


void ReplayPredictionPresentation::RecordTrajectoryFrameStats( const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& frameStats )
{
    for ( std::size_t index = 0; index < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT; ++index )
    {
        m_trajectoryVisualStats.emittedSegments[index] += frameStats.emittedSegments[index];
        m_trajectoryVisualStats.droppedSegments[index] += frameStats.droppedSegments[index];
    }
}


void ReplayPredictionPresentation::RecordTrajectorySubmissionFrame( const SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats& submissionStats, int frameNumber,
                                                                    uint64_t reserveGrowthEventCount )
{
    if ( !submissionStats.hasGeometry || submissionStats.vertexBytes == 0 || submissionStats.vertexCount == 0 )
    {
        return;
    }

    ++m_trajectorySubmissionProbe.observedFrameCount;
    m_trajectorySubmissionProbe.hasSubmission = true;
    m_trajectorySubmissionProbe.stableWindowTargetFrameCount = REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET;
    const bool sameSubmittedBytes = m_trajectorySubmissionProbe.stableFrameCount > 0 &&
                                    m_trajectorySubmissionProbe.stableHash == submissionStats.vertexHash &&
                                    m_trajectorySubmissionProbe.vertexBytes == submissionStats.vertexBytes &&
                                    m_trajectorySubmissionProbe.vertexCount == submissionStats.vertexCount &&
                                    m_trajectorySubmissionProbe.segmentCount == submissionStats.segmentCount;

    const bool sameReserveWindow = reserveGrowthEventCount == m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;

    if ( !sameSubmittedBytes || !sameReserveWindow )
    {

        // Invariant: build reserve growth may finish before the steady window;
        // evidence begins only once submitted bytes and counters hold steady.
        m_trajectorySubmissionProbe.stableFrameCount = 1;
        m_trajectorySubmissionProbe.firstFrame = frameNumber;
        m_trajectorySubmissionProbe.stableHash = submissionStats.vertexHash;
        m_trajectorySubmissionProbe.vertexBytes = submissionStats.vertexBytes;
        m_trajectorySubmissionProbe.vertexCount = submissionStats.vertexCount;
        m_trajectorySubmissionProbe.segmentCount = submissionStats.segmentCount;
        m_trajectorySubmissionProbe.reserveGrowthEventsAtStart = reserveGrowthEventCount;
    }
    else
    {
        ++m_trajectorySubmissionProbe.stableFrameCount;
    }

    m_trajectorySubmissionProbe.lastFrame = frameNumber;
    m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd = reserveGrowthEventCount;
    m_trajectorySubmissionProbe.noReserveGrowth = m_trajectorySubmissionProbe.reserveGrowthEventsAtStart ==
                                                  m_trajectorySubmissionProbe.reserveGrowthEventsAtEnd;

    m_trajectorySubmissionProbe.stableWindowReady = m_trajectorySubmissionProbe.stableFrameCount >=
                                                        REPLAY_TRAJECTORY_SUBMISSION_STEADY_FRAME_TARGET &&
                                                    m_trajectorySubmissionProbe.noReserveGrowth;
}


void ReplayPredictionPresentation::RecordTrajectoryBudgetExpiry( SkullbonezCore::Core::MainMemoryReplayBudgetPass pass )
{
    const std::size_t passIndex = static_cast<std::size_t>( pass );

    if ( passIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_BUDGET_PASS_COUNT )
    {
        ++m_trajectoryVisualStats.budgetExpiries[passIndex];
    }
}


void ReplayPredictionPresentation::RecordTrajectoryRebuildCause( SkullbonezCore::Core::MainMemoryReplayRebuildCause cause )
{
    const std::size_t causeIndex = static_cast<std::size_t>( cause );

    if ( causeIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_REBUILD_CAUSE_COUNT )
    {
        ++m_trajectoryVisualStats.rebuildCauses[causeIndex];
    }
}
} // namespace SkullbonezCore::Runtime
