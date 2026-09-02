/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.MarkerScan.inl
Purpose:
  Advances the budgeted replay-prediction child-marker scan and publishes its
  retained causal-marker values.

Summary:
  Prediction owns one scan state per published topology. Stable child rows
  consume only newly revealed frame suffixes, while changed rows restart from
  frame zero and retain their exact budget-resume cursor for the next frame.
  A completed coherent scan projects the same fixed entry and rest poses that
  presentation retains for later frames.

Invariants:
  - A scan commits its publication key only after every node reaches the same
    revealed frame prefix.
  - Buffer completion is part of that key because only an authoritative end
    may publish rest poses or authorize a committed-bank flip.
  - Source regression or bank/generation replacement resets every node; topology
    growth resets only new or changed rows.
  - Frame rows are read only through the caller's bounded published prefix.
  - Retained entry poses never slide, and rest poses exist only for completed
    predictions whose reveal cursor has passed the body's settling grace.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPrediction.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp
  - SkullbonezTests/TestReplayVisualPacket.cpp
*/
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>

namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
{
using namespace ReplayPredictionSchedulingOperations;
using Math::Orientation::Quaternion;
using Math::Vector::Vector3;

inline constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
inline constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES = static_cast<ReplayFrameIndex>(
    REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );

inline ReplayPredictionRetainedMarker* FindOrAddReplayPredictionRetainedMarker( RunReplayPredictionState& prediction,
                                                                                Physics::PhysicsSceneObjectId id,
                                                                                int modelIndex )
{
    if ( id.value == 0 )
    {
        return nullptr;
    }

    for ( std::size_t i = 0; i < prediction.futureNodeCache.retainedMarkerCount; ++i )
    {
        ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache.retainedMarkers[i];

        if ( marker.id.value == id.value )
        {
            if ( modelIndex >= 0 )
            {
                marker.modelRow.value = modelIndex;
            }

            return &marker;
        }
    }

    if ( prediction.futureNodeCache.retainedMarkerCount >= prediction.futureNodeCache.retainedMarkers.size() )
    {
        return nullptr;
    }

    ReplayPredictionRetainedMarker& marker = prediction.futureNodeCache
                                                 .retainedMarkers[prediction.futureNodeCache.retainedMarkerCount++];
    marker = ReplayPredictionRetainedMarker {};
    marker.id = id;
    marker.modelRow.value = modelIndex;
    return &marker;
}

inline void RetainReplayPredictionEntryMarker( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId id,
                                               int modelIndex, const Vector3& position, Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasEntryPose = true;
        marker->entryPosition = position;
        marker->entryOrientation = orientation;
        marker->entryOrientation.Normalise();
    }
}

inline void RetainReplayPredictionRestMarker( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId id,
                                              int modelIndex, const Vector3& position, Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        marker->hasRestPose = true;
        marker->hasHorizonPose = false;
        marker->restPosition = position;
        marker->restOrientation = orientation;
        marker->restOrientation.Normalise();
    }
}

inline void RetainReplayPredictionHorizonMarker( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId id,
                                                 int modelIndex, const Vector3& position, Quaternion orientation )
{
    if ( ReplayPredictionRetainedMarker* marker = FindOrAddReplayPredictionRetainedMarker( prediction, id, modelIndex ) )
    {
        if ( marker->hasRestPose )
        {
            return;
        }

        marker->hasHorizonPose = true;
        marker->horizonPosition = position;
        marker->horizonOrientation = orientation;
        marker->horizonOrientation.Normalise();
    }
}

// Concept: causal markers are the two-wireframe story projected from one
// coherent scan. Yellow is fixed at the predicted collision pose; grey appears
// only at the final resting pose. Neither wireframe ever slides.
// Keeping publication beside the scan lets focused tests compare the exact
// retained values consumed by presentation, not only cursors.
inline void RetainReplayPredictionCausalMarkers( RunReplayPredictionState& prediction,
                                                 const ReplayPredictionChildMarkerScanState& scan,
                                                 ReplayFrameIndex revealFrame,
                                                 const std::vector<RunReplayPredictionFrame>* completeFrames,
                                                 std::size_t completeFrameCount )
{
    for ( std::size_t i = 0; i < scan.nodeCount; ++i )
    {
        const ReplayPredictionChildMarkerNodeScanState& drawState = scan.nodes[i];

        if ( drawState.hasEntryPose )
        {
            RetainReplayPredictionEntryMarker( prediction, drawState.node.id, drawState.entryModelIndex,
                                               drawState.entryPosition, drawState.entryOrientation );
        }

        // Why: completeFrames is null while the job is still building. A
        // growing prefix has no authoritative ending, and the grace check
        // prevents a grey rest box appearing before the reveal watches it stop.
        if ( !drawState.active || !completeFrames ||
             revealFrame < drawState.lastMotionFrame + REPLAY_PREDICTION_REST_GRACE_FRAMES )
        {
            continue;
        }

        Vector3 restPosition = Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = Math::Orientation::IDENTITY_QUATERNION;

        if ( ReplayPredictionBodyRestingPose( *completeFrames, completeFrameCount, drawState.node.id,
                                              drawState.node.modelRow.value, restPosition, restOrientation ) )
        {
            RetainReplayPredictionRestMarker( prediction, drawState.node.id, drawState.node.modelRow.value, restPosition,
                                              restOrientation );
        }
    }
}

inline bool AdvanceReplayPredictionChildMarkerScan(
    ReplayPredictionChildMarkerScanState& scan, const RunReplayPredictionState& prediction,
    const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount, ReplayFrameIndex revealFrame,
    uint32_t generation, Physics::PhysicsSceneObjectId targetId, bool usingBuildFrames, bool bufferComplete,
    const std::chrono::steady_clock::time_point& budgetStart, double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );
    const std::size_t nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(),
                                              static_cast<std::size_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );
    const uint32_t topologyVersion = prediction.futureNodeCache.futureNodesTopologyVersion;

    const bool sourceChanged = !scan.initialized || scan.generation != generation || scan.targetId.value != targetId.value ||
                               scan.usingBuildFrames != usingBuildFrames || frameCount < scan.frameCount ||
                               revealFrame < scan.revealFrame;

    if ( sourceChanged )
    {
        scan.Reset();
        scan.generation = generation;
        scan.targetId = targetId;
        scan.usingBuildFrames = usingBuildFrames;
        scan.initialized = true;
    }

    const std::size_t previousNodeCount = sourceChanged ? 0u : scan.nodeCount;

    for ( std::size_t i = 0; i < nodeCount; ++i )
    {
        (void)scan.PreserveOrResetNode( i, previousNodeCount, prediction.futureNodeCache.futureNodes[i] );
    }

    scan.nodeCount = nodeCount;
    scan.topologyVersion = topologyVersion;
    scan.valid = false;

    if ( frameCount < 2 || nodeCount == 0 )
    {
        scan.Commit( generation, topologyVersion, targetId, frameCount, revealFrame, usingBuildFrames, bufferComplete );
        return true;
    }

    // Invariant: prediction frames publish in increasing solver-frame order.
    // Binary search therefore finds the revealed prefix without rewalking a
    // 14,401-frame horizon merely to locate its end.
    const auto visibleEnd = std::upper_bound( frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>( frameCount ),
                                              revealFrame,
                                              []( ReplayFrameIndex frame, const RunReplayPredictionFrame& sample )
                                              { return frame < sample.frameIndex; } );

    const std::size_t visibleFrameCount = static_cast<std::size_t>( std::distance( frames.begin(), visibleEnd ) );

    // Concept: every node owns an independent frame cursor. Topology growth
    // initializes only new or changed nodes; reveal growth scans only the new
    // suffix for stable nodes. Marker side effects remain deferred until every
    // node reaches this call's coherent visible prefix.
    for ( std::size_t i = 0; i < nodeCount; ++i )
    {
        ReplayPredictionChildMarkerNodeScanState& drawState = scan.nodes[i];

        while ( drawState.scannedFrameCount < visibleFrameCount )
        {
            if ( ( drawState.scannedFrameCount & 63u ) == 0u &&
                 ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                scan.frameCount = frameCount;
                scan.revealFrame = revealFrame;
                return false;
            }

            const RunReplayPredictionFrame& frame = frames[drawState.scannedFrameCount];
            ++drawState.scannedFrameCount;

            if ( frame.frameIndex < drawState.node.firstFrame )
            {
                continue;
            }

            const RunReplayPredictionBodySample*
                body = FindReplayPredictionBodyByIdWithHint( frame, drawState.node.id, drawState.node.modelRow.value );

            if ( !body )
            {
                continue;
            }

            drawState.ObserveBody( frame.frameIndex, *body, ReplayPredictionBodyHasVisibleLinearMotion( *body ) );
        }
    }

    scan.Commit( generation, topologyVersion, targetId, frameCount, revealFrame, usingBuildFrames, bufferComplete );
    return true;
}
} // namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
