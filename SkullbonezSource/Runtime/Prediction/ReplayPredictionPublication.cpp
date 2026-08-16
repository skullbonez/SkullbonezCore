/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp
Purpose:
  Owns replay prediction trajectory, topology, baseline, and marker publication.

Summary:
  Isolated simulation supplies completed frame rows. This owner derives the
  contiguous trajectory and causal-topology records consumed by presentation,
  resuming whole-node hidden duplication at budget boundaries.

Invariants:
  - Derived rows never outpace the acquire-visible prediction frame prefix.
  - Trajectory and topology versions advance only after complete replacement data exists.
  - All-body records reserve their full frame capacity once, then append only new points.

Related:
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h
  - Agentic/Reference/engine-glossary.md
*/
#include "ReplayPredictionPublicationOperations.h"
#include "../Scene/SceneEntityStore.h"
#include "../Editor/EditorHullAssets.h"
#include "../Replay/ReplayOverlayLayout.h"
#include "ReplayPredictionReserve.h"
#include "../Replay/ReplayScrubber.h"
#include "../../Core/Config.h"
#include "../../Core/SceneCapacity.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsApi.h"
#include "../../Physics/PhysicsMass.h"
#include "../../Physics/PhysicsTimestep.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::ReplayPredictionReserveOperations;
using namespace SkullbonezCore::Runtime::ReplayScrubberOperations;
namespace Gameplay = SkullbonezCore::Gameplay;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
namespace Physics = SkullbonezCore::Physics;
using SkullbonezCore::Assets::EDITOR_HULL_ASSET_COUNT;
using SkullbonezCore::Assets::EDITOR_HULL_ASSETS;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetDefaultsToContactRelease;
using SkullbonezCore::Assets::EditorHullAssetPath;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Math::Vector::Vector3;
namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
{
namespace
{
constexpr std::size_t REPLAY_PATH_MAX_FUTURE_NODES = REPLAY_VISUAL_FUTURE_NODE_CAPACITY;
constexpr std::size_t REPLAY_PATH_MAX_SEGMENTS = 260;
constexpr float REPLAY_PATH_MIN_SEGMENT_DISTANCE_SQ = 0.0001f;
constexpr float REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ = 8.0f * 8.0f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE = 0.05f;
constexpr float REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE_SQ = REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE *
                                                                 REPLAY_PREDICTION_CHILD_ACTIVATION_DISTANCE;
constexpr double REPLAY_PREDICTION_REST_GRACE_SECONDS = 0.4;
constexpr ReplayFrameIndex REPLAY_PREDICTION_REST_GRACE_FRAMES = static_cast<ReplayFrameIndex>( REPLAY_PREDICTION_REST_GRACE_SECONDS / PHYSICS_FIXED_DT );
constexpr float REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ = 0.5f * 0.5f;

} // namespace

std::size_t ReplayPredictionPathStrideForSampleCount( std::size_t sampleCount ) noexcept
{
    if ( sampleCount <= REPLAY_PATH_MAX_SEGMENTS )
    {
        return 1;
    }

    return ( sampleCount + REPLAY_PATH_MAX_SEGMENTS - 1 ) / REPLAY_PATH_MAX_SEGMENTS;
}

void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction )
{
    prediction.futureNodeCache.futureNodes.clear();
    prediction.futureNodeCache.futureNodeBuildScratch.clear();
    prediction.futureNodeCache.futureNodesBuiltFrameCount = 0;
    prediction.futureNodeCache.futureNodesBuiltContactIndex = 0;
    prediction.futureNodeCache.futureNodesAffectedBodyCursor = 0;
    prediction.futureNodeCache.futureNodesAffectedFrameCount = 0;
    prediction.futureNodeCache.futureNodesAffectedComplete = false;
    prediction.futureNodeCache.futureNodesBuiltTargetId = Physics::PhysicsSceneObjectId {};
    prediction.futureNodeCache.futureNodesTopologyVersion = 0;
    prediction.futureNodeCache.futureNodesBuiltRagdollVisuals = prediction.ragdollVisualsEnabled;
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;
    prediction.futureNodeCache.futureNodesCacheValid = false;
    prediction.futureNodeCache.ResetRetainedMarkers();
    prediction.trajectoryBuild.childFrameCount = 0;
    prediction.trajectoryBuild.builtNodeCount = 0;
    prediction.trajectoryBuild.childAppendTargetFrameCount = 0;
    prediction.trajectoryBuild.childAppendNodeIndex = 0;
    prediction.trajectoryBuild.topologyVersion = 0;
}


const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, Physics::PhysicsSceneObjectId id )
{
    return FindReplayBodyByIdInSample<ReplaySolverFrameSample, ReplaySolverBodySample>( sample, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   Physics::PhysicsSceneObjectId id )
{
    return FindReplayBodyByIdInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample>( frame, id );
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, false>( frame,
                                                                                                               modelIndex );
}

const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex )
{
    return FindReplayBodyByModelIndexInSample<ReplaySolverFrameSample, ReplaySolverBodySample, true>( sample, modelIndex );
}

const ReplaySolverBodySample* FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample,
                                                          Physics::PhysicsSceneObjectId id, int modelIndex )
{
    if ( const ReplaySolverBodySample* hinted = FindReplayBodyByModelIndex( sample, modelIndex ) )
    {
        if ( hinted->id.value == id.value )
        {
            return hinted;
        }
    }

    return FindReplayBodyById( sample, id );
}

Physics::PhysicsSceneObjectId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex )
{
    return SceneObjectIdForModelIndexInSample<RunReplayPredictionFrame, RunReplayPredictionBodySample, false>( frame,
                                                                                                               modelIndex );
}

bool ReplayModelIndexIsRagdollPart( const SceneEntityStore& entities, int modelIndex )
{
    // Hazard: physics debug contacts use -1 for terrain/world counterparts.
    // That sentinel is not a scene row and must never reach group metadata.
    if ( modelIndex < 0 || modelIndex >= entities.Count() )
    {
        return false;
    }

    const SceneEntityRecord* entity = entities.TryGet( modelIndex );
    return entity && entity->behaviorGroup.kind == SceneBehaviorGroupKind::SimpleRagdoll;
}

int ReplayRagdollTorsoModelIndexForPart( const SceneEntityStore& entities, int modelIndex )
{
    const SceneEntityRecord* entity = entities.TryGet( modelIndex );

    if ( !entity || entity->behaviorGroup.kind != SceneBehaviorGroupKind::SimpleRagdoll )
    {
        return modelIndex;
    }

    const int rootRow = entities.FindBySceneObjectId( entity->behaviorGroup.rootObjectId );
    return rootRow >= 0 ? rootRow : modelIndex;
}

Vector3 ReplayNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );

    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }

    value /= sqrtf( magSq );
    return value;
}

const RunReplayPredictionBodySample* FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame,
                                                                           Physics::PhysicsSceneObjectId id, int modelIndex )
{
    if ( const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByModelIndex( frame, modelIndex ) )
    {
        if ( body->id.value == id.value )
        {
            return body;
        }
    }

    return FindReplayPredictionBodyById( frame, id );
}

// Concept: TrajectoryStore records are the future line-draw source. Branch
// ordinal 0 is the canonical committed prediction; branch 1 is the worker or
// alternate hidden replacement. Child branches are offset by bank so a refresh
// cannot overwrite the trajectory bank still owned by readers.
constexpr uint16_t REPLAY_TRAJECTORY_COMMITTED_BRANCH = 0;
constexpr uint16_t REPLAY_TRAJECTORY_BUILD_BRANCH = 1;

int ReplayTrajectoryFrameNumberForReserve( ReplayFrameIndex frameIndex )
{
    return static_cast<int>( (std::min)( frameIndex, static_cast<ReplayFrameIndex>( ( std::numeric_limits<int>::max )() ) ) );
}

ReplayTrajectoryRecordKey ReplayTrajectoryKey( Physics::PhysicsSceneObjectId bodyId, ReplayTrajectoryLane lane,
                                               uint16_t branchOrdinal )
{
    ReplayTrajectoryRecordKey key;
    key.bodyId = bodyId;
    key.lane = lane;
    key.branchOrdinal = branchOrdinal;
    return key;
}

bool ReserveReplayTrajectoryRecordSlot( ReplayTrajectoryStore& store, const ReplayTrajectoryRecordKey& key, int frameNumber )
{
    return store.FindRecord( key ) || store.ReserveRecords( store.RecordCount() + 1u, frameNumber );
}

ReplayTrajectoryRecord* BeginReplayTrajectoryRecord( ReplayTrajectoryStore& store, const ReplayTrajectoryRecordKey& key,
                                                     uint16_t styleId, Physics::PhysicsSceneObjectId parentId, int depth,
                                                     ReplayFrameIndex firstFrame, bool contactDerived,
                                                     std::size_t pointCapacity )
{
    const int frameNumber = ReplayTrajectoryFrameNumberForReserve( firstFrame );

    if ( !ReserveReplayTrajectoryRecordSlot( store, key, frameNumber ) )
    {
        return nullptr;
    }

    ReplayTrajectoryRecord* record = store.BeginReplaceRecord( key, styleId, parentId, depth, firstFrame, contactDerived,
                                                               pointCapacity );

    if ( !record || !store.ReserveRecordPoints( *record, pointCapacity, frameNumber ) )
    {
        return nullptr;
    }

    return record;
}

bool AppendReplayTrajectoryPoint( ReplayTrajectoryStore& store, ReplayTrajectoryRecord& record, ReplayFrameIndex frameIndex,
                                  const Vector3& position )
{
    if ( !store.TryAppendPoint( record, { frameIndex, position } ) )
    {
        return false;
    }

    store.PublishPrefix( record, record.points.size() );
    return true;
}

ReplayFrameIndex ReplayOldestFrameFromStats( const ReplayRecorderStats& stats )
{
    return stats.nextFrameIndex > static_cast<ReplayFrameIndex>( stats.sampleCount )
               ? stats.nextFrameIndex - static_cast<ReplayFrameIndex>( stats.sampleCount )
               : 0;
}

// Concept: the past-root trajectory mirrors the solver recorder window. Rebuild
// handles target changes and ring eviction; capture-time append handles the
// ordinary newest-sample case without re-walking retained history.
ReplayTrajectoryRecordKey ReplayPastRootTrajectoryKey( Physics::PhysicsSceneObjectId targetId )
{
    return ReplayTrajectoryKey( targetId, ReplayTrajectoryLane::PastRoot, 0 );
}

ReplayTrajectoryRecord* BeginReplayPastRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                             Physics::PhysicsSceneObjectId targetId,
                                                             std::size_t pointCapacity, int frameNumber )
{
    return BeginReplayTrajectoryRecord( store, ReplayPastRootTrajectoryKey( targetId ), 0, Physics::PhysicsSceneObjectId {},
                                        0, static_cast<ReplayFrameIndex>( frameNumber ), false, pointCapacity );
}

std::size_t ReplayPredictionTrajectoryRecordCapacity( std::size_t bodyCount )
{
    const std::size_t visibleBodyCount = (std::min)( bodyCount,
                                                     static_cast<std::size_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );

    // Mutual-gravity scenes retain committed and in-progress banks for every
    // visible body while the causal incoming/outgoing banks remain available
    // for marker evidence.
    return 2u + REPLAY_PATH_MAX_FUTURE_NODES * 4u + REPLAY_PATH_MAX_ROOT_TARGETS + visibleBodyCount * 2u;
}

uint16_t ReplayPredictionChildTrajectoryBranch( std::size_t nodeIndex, bool usingBuildFrames )
{
    const std::size_t branchBase = usingBuildFrames ? REPLAY_PATH_MAX_FUTURE_NODES : 0u;
    return static_cast<uint16_t>( (std::min)( branchBase + nodeIndex, static_cast<std::size_t>( ( std::numeric_limits<uint16_t>::max )() ) ) );
}

bool PrepareReplayPredictionTrajectoryBuild( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId rootId,
                                             std::size_t frameCapacity, std::size_t bodyCount )
{
    prediction.trajectoryBuild = RunReplayPredictionTrajectoryBuildState {};

    if ( rootId.value == 0 )
    {
        return true;
    }

    const std::size_t recordCapacity = (std::max)( prediction.trajectoryStore.RecordCount(),
                                                   ReplayPredictionTrajectoryRecordCapacity( bodyCount ) );

    if ( !prediction.trajectoryStore.ReserveRecords( recordCapacity, 0 ) )
    {
        return false;
    }

    ReplayTrajectoryRecord* rootRecord = BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                                                      ReplayTrajectoryKey( rootId,
                                                                                           ReplayTrajectoryLane::FutureRoot,
                                                                                           REPLAY_TRAJECTORY_BUILD_BRANCH ),
                                                                      0, Physics::PhysicsSceneObjectId {}, 0, 0, false,
                                                                      frameCapacity );

    if ( !rootRecord )
    {
        return false;
    }

    rootRecord->points.resize( frameCapacity );

    // Invariant: branch 1 is the in-progress prediction record. Branch 0 stays
    // as the committed future until the build swap publishes the completed
    // frame vector.
    prediction.trajectoryBuild.rootId = rootId;
    prediction.trajectoryBuild.usingBuildFrames = true;
    prediction.trajectoryBuild.valid = true;
    return true;
}

bool PublishReplayPredictionRootTrajectoryFrame( RunReplayPredictionState& prediction, const RunReplayPredictionFrame& frame,
                                                 std::size_t frameSlot )
{
    if ( !prediction.trajectoryBuild.valid || prediction.trajectoryBuild.rootId.value == 0 ||
         !prediction.trajectoryBuild.usingBuildFrames )
    {
        return true;
    }

    ReplayTrajectoryRecord* record = prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( prediction.trajectoryBuild.rootId, ReplayTrajectoryLane::FutureRoot,
                                                                                                 REPLAY_TRAJECTORY_BUILD_BRANCH ) );

    if ( !record || frameSlot >= record->points.size() )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    const RunReplayPredictionBodySample*
        body = FindReplayPredictionBodyByIdWithHint( frame, prediction.trajectoryBuild.rootId,
                                                     prediction.simulation.targetModelRow.value );

    if ( !body )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    record->points[frameSlot] = { frame.frameIndex, body->position };
    prediction.trajectoryBuild.rootFrameCount = frameSlot + 1u;
    return true;
}

bool PublishReplayPredictionBuildRootTrajectoryPrefix( RunReplayPredictionState& prediction,
                                                       std::size_t presentedFrameCount )
{
    if ( !prediction.trajectoryBuild.valid || !prediction.trajectoryBuild.usingBuildFrames ||
         prediction.trajectoryBuild.rootId.value == 0 )
    {
        return false;
    }

    ReplayTrajectoryRecord* record = prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( prediction.trajectoryBuild.rootId, ReplayTrajectoryLane::FutureRoot,
                                                                                                 REPLAY_TRAJECTORY_BUILD_BRANCH ) );

    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    // Invariant: PresentedCount is bounded by the acquire-loaded worker
    // publication. Publishing exactly that many root points makes the ball path
    // grow with the visible prediction prefix without exposing an in-flight row.
    prediction.trajectoryStore.PublishPrefix( *record, ReplayPredictionBuildRootPrefixCount( presentedFrameCount,
                                                                                             record->points.size() ) );

    return true;
}

bool RebuildReplayPredictionReplacementRootTrajectory( RunReplayPredictionState& prediction,
                                                       ReplayPredictionTrajectoryBank replacementBank )
{
    const std::size_t committedFrameCount = prediction.CommittedFrameCount();

    if ( prediction.simulation.targetId.value == 0 || committedFrameCount < 2u )
    {
        return true;
    }

    const bool replacementUsesBuildBank = replacementBank == ReplayPredictionTrajectoryBank::Build;
    const uint16_t replacementRootBranch = replacementUsesBuildBank ? REPLAY_TRAJECTORY_BUILD_BRANCH
                                                                    : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    // Invariant: only the bank opposite the captured visible snapshot is
    // cleared here. Its dormant vectors are immediately reusable by the hidden
    // root/child rebuild without touching the branch still owned by readers.
    prediction.trajectoryStore.RetirePredictionBank( replacementBank, REPLAY_TRAJECTORY_BUILD_BRANCH,
                                                     static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );

    ReplayTrajectoryRecord* record = BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                                                  ReplayTrajectoryKey( prediction.simulation.targetId,
                                                                                       ReplayTrajectoryLane::FutureRoot,
                                                                                       replacementRootBranch ),
                                                                  0, Physics::PhysicsSceneObjectId {}, 0, 0, false,
                                                                  committedFrameCount );

    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( std::size_t frameIndex = 0; frameIndex < committedFrameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = prediction.simulation.frames[frameIndex];
        const RunReplayPredictionBodySample*
            body = FindReplayPredictionBodyByIdWithHint( frame, prediction.simulation.targetId,
                                                         prediction.simulation.targetModelRow.value );

        if ( body && !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    prediction.trajectoryBuild.rootId = prediction.simulation.targetId;
    prediction.trajectoryBuild.usingBuildFrames = replacementUsesBuildBank;
    prediction.trajectoryBuild.rootFrameCount = record->points.size();
    prediction.trajectoryBuild.childFrameCount = 0;
    prediction.trajectoryBuild.builtNodeCount = 0;
    prediction.trajectoryBuild.childAppendTargetFrameCount = 0;
    prediction.trajectoryBuild.childAppendNodeIndex = 0;
    prediction.trajectoryBuild.allBodyFrameCount = 0;
    prediction.trajectoryBuild.builtAllBodyCount = 0;
    prediction.trajectoryBuild.allBodyBodyCount = 0;
    prediction.trajectoryBuild.topologyVersion = 0;
    prediction.trajectoryBuild.allBodyPaths = false;
    prediction.trajectoryBuild.valid = true;
    return true;
}

bool RebuildReplayPredictionCommittedRootTrajectory( RunReplayPredictionState& prediction )
{
    return RebuildReplayPredictionReplacementRootTrajectory( prediction, ReplayPredictionTrajectoryBank::Committed );
}

bool BuildReplayPredictionChildTrajectoryRecord( RunReplayPredictionState& prediction,
                                                 const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                                 const RunReplayPathTraceNode& node, std::size_t nodeIndex,
                                                 bool usingBuildFrames, ReplayTrajectoryLane lane, bool seedOutgoingEntry )
{
    if ( frameCount == 0 )
    {
        return true;
    }

    const uint16_t branchOrdinal = ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames );
    const std::size_t predictionFrameCapacity = usingBuildFrames ? prediction.build.buildFrames.size()
                                                                 : prediction.simulation.frames.size();

    const std::size_t pointCapacity = (std::max)( frameCount, predictionFrameCapacity ) + ( seedOutgoingEntry ? 1u : 0u );

    ReplayTrajectoryRecord* record = BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                                                  ReplayTrajectoryKey( node.id, lane, branchOrdinal ),
                                                                  static_cast<uint16_t>( std::clamp( node.depth, 0, 0xFFFF ) ),
                                                                  node.parentId, node.depth, node.firstFrame,
                                                                  node.contactDerived, pointCapacity );

    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    if ( seedOutgoingEntry )
    {
        const RunReplayPredictionBodySample* initial = FindReplayPredictionBodyByIdWithHint( frames[0], node.id,
                                                                                             node.modelRow.value );

        if ( initial &&
             !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frames[0].frameIndex, initial->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];

        if ( seedOutgoingEntry && lane == ReplayTrajectoryLane::FutureChildOutgoing && frameIndex == 0u )
        {
            continue;
        }

        const bool includeFrame = lane == ReplayTrajectoryLane::FutureChildIncoming ? frame.frameIndex <= node.firstFrame
                                                                                    : frame.frameIndex >= node.firstFrame;

        if ( !includeFrame )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, node.id,
                                                                                          node.modelRow.value );

        if ( body && !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    return true;
}

bool AppendReplayPredictionChildTrajectoryFrames( RunReplayPredictionState& prediction,
                                                  const std::vector<RunReplayPredictionFrame>& frames,
                                                  std::size_t beginFrame, std::size_t frameCount,
                                                  const RunReplayPathTraceNode& node, std::size_t nodeIndex,
                                                  bool usingBuildFrames, ReplayTrajectoryLane lane )
{
    const uint16_t branchOrdinal = ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames );
    ReplayTrajectoryRecord* record = prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( node.id, lane, branchOrdinal ) );

    if ( !record )
    {
        return BuildReplayPredictionChildTrajectoryRecord( prediction, frames, frameCount, node, nodeIndex, usingBuildFrames,
                                                           lane, lane == ReplayTrajectoryLane::FutureChildOutgoing );
    }

    const int frameNumber = frameCount > 0u ? ReplayTrajectoryFrameNumberForReserve( frames[frameCount - 1u].frameIndex )
                                            : 0;

    const std::size_t predictionFrameCapacity = usingBuildFrames ? prediction.build.buildFrames.size()
                                                                 : prediction.simulation.frames.size();

    if ( !prediction.trajectoryStore.ReserveRecordPoints( *record, (std::max)( frameCount, predictionFrameCapacity ) + 1u,
                                                          frameNumber ) )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( std::size_t frameIndex = beginFrame; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        const bool includeFrame = lane == ReplayTrajectoryLane::FutureChildIncoming ? frame.frameIndex <= node.firstFrame
                                                                                    : frame.frameIndex >= node.firstFrame;

        if ( !includeFrame )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, node.id,
                                                                                          node.modelRow.value );

        if ( body && !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    return true;
}

bool BuildReplayPredictionAllBodyTrajectoryRecord( RunReplayPredictionState& prediction,
                                                   const std::vector<RunReplayPredictionFrame>& frames,
                                                   std::size_t frameCount, const RunReplayPredictionBodySample& seedBody,
                                                   bool usingBuildFrames )
{
    const uint16_t branchOrdinal = usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    const std::size_t pointCapacity = usingBuildFrames ? prediction.build.buildFrames.size()
                                                       : prediction.simulation.frames.size();

    ReplayTrajectoryRecord* record = BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                                                  ReplayTrajectoryKey( seedBody.id,
                                                                                       ReplayTrajectoryLane::FutureRoot,
                                                                                       branchOrdinal ),
                                                                  0, Physics::PhysicsSceneObjectId {}, 0, 0, false,
                                                                  (std::max)( frameCount, pointCapacity ) );

    if ( !record )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, seedBody.id,
                                                                                          seedBody.modelRow.value );

        if ( body && !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    return true;
}

bool AppendReplayPredictionAllBodyTrajectoryFrames( RunReplayPredictionState& prediction,
                                                    const std::vector<RunReplayPredictionFrame>& frames,
                                                    std::size_t beginFrame, std::size_t frameCount,
                                                    const RunReplayPredictionBodySample& seedBody, bool usingBuildFrames )
{
    const uint16_t branchOrdinal = usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    ReplayTrajectoryRecord* record = prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( seedBody.id, ReplayTrajectoryLane::FutureRoot, branchOrdinal ) );

    if ( !record )
    {
        return BuildReplayPredictionAllBodyTrajectoryRecord( prediction, frames, frameCount, seedBody, usingBuildFrames );
    }

    const int frameNumber = frameCount > 0u ? ReplayTrajectoryFrameNumberForReserve( frames[frameCount - 1u].frameIndex )
                                            : 0;

    const std::size_t pointCapacity = usingBuildFrames ? prediction.build.buildFrames.size()
                                                       : prediction.simulation.frames.size();

    if ( !prediction.trajectoryStore.ReserveRecordPoints( *record, (std::max)( frameCount, pointCapacity ), frameNumber ) )
    {
        prediction.trajectoryBuild.valid = false;
        return false;
    }

    for ( std::size_t frameIndex = beginFrame; frameIndex < frameCount; ++frameIndex )
    {
        const RunReplayPredictionFrame& frame = frames[frameIndex];
        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, seedBody.id,
                                                                                          seedBody.modelRow.value );

        if ( body && !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, frame.frameIndex, body->position ) )
        {
            prediction.trajectoryBuild.valid = false;
            return false;
        }
    }

    return true;
}

bool ReplayPredictionChildTrajectoryRecordMatches( const RunReplayPredictionState& prediction,
                                                   const RunReplayPathTraceNode& node, std::size_t nodeIndex,
                                                   bool usingBuildFrames, ReplayTrajectoryLane lane )
{
    const ReplayTrajectoryRecord* record = prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( node.id, lane, ReplayPredictionChildTrajectoryBranch( nodeIndex, usingBuildFrames ) ) );

    return record && record->styleId == static_cast<uint16_t>( std::clamp( node.depth, 0, 0xFFFF ) ) &&
           record->parentId.value == node.parentId.value && record->depth == node.depth &&
           record->firstFrame == node.firstFrame && record->contactDerived == node.contactDerived;
}

void UpdateReplayPredictionAllBodyTrajectories( RunReplayPredictionState& prediction,
                                                const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                                bool usingBuildFrames, Physics::PhysicsSceneObjectId rootId,
                                                const std::chrono::steady_clock::time_point& budgetStart,
                                                double budgetMilliseconds )
{
    const bool showAllFuturePaths = prediction.simulation.predictionWorldForces.mutualGravity.enabled;

    if ( !showAllFuturePaths || frameCount < 2u || frames.empty() )
    {
        prediction.trajectoryBuild.allBodyFrameCount = 0;
        prediction.trajectoryBuild.builtAllBodyCount = 0;
        prediction.trajectoryBuild.allBodyBodyCount = 0;
        prediction.trajectoryBuild.allBodyPaths = false;
        return;
    }

    const std::size_t bodyCount = (std::min)( frames[0].bodies.size(),
                                              static_cast<std::size_t>( REPLAY_VISUAL_FUTURE_NODE_CAPACITY ) );

    const uint16_t activeBranch = usingBuildFrames ? REPLAY_TRAJECTORY_BUILD_BRANCH : REPLAY_TRAJECTORY_COMMITTED_BRANCH;

    const std::size_t builtPrefixCount = (std::min)( prediction.trajectoryBuild.builtAllBodyCount, bodyCount );
    bool builtPrefixMissing = false;

    for ( std::size_t bodyIndex = 0; bodyIndex < builtPrefixCount; ++bodyIndex )
    {
        const RunReplayPredictionBodySample& seedBody = frames[0].bodies[bodyIndex];

        if ( seedBody.id.value != 0u && seedBody.id.value != rootId.value &&
             !prediction.trajectoryStore.FindRecord( ReplayTrajectoryKey( seedBody.id, ReplayTrajectoryLane::FutureRoot, activeBranch ) ) )
        {
            builtPrefixMissing = true;
            break;
        }
    }

    const bool sourceChanged = prediction.trajectoryBuild.AllBodyPublicationSourceChanged( rootId, usingBuildFrames,
                                                                                           frameCount, bodyCount,
                                                                                           builtPrefixMissing );

    if ( sourceChanged )
    {
        // Invariant: establish the bank identity once. Missing records beyond
        // builtAllBodyCount are pending work, not evidence that the source
        // changed; treating them as a mismatch restarts at body zero forever.
        prediction.trajectoryBuild.allBodyFrameCount = 0;
        prediction.trajectoryBuild.builtAllBodyCount = 0;
        prediction.trajectoryBuild.allBodyBodyCount = bodyCount;
        prediction.trajectoryBuild.allBodyPaths = true;
    }

    // Concept: every space-body record is independent of the contact-derived
    // future tree. Extending a prediction therefore appends only the new frame
    // suffix to each body record; topology churn cannot rebuild these paths.
    if ( !sourceChanged && prediction.trajectoryBuild.allBodyFrameCount < frameCount )
    {
        for ( std::size_t bodyIndex = 0; bodyIndex < prediction.trajectoryBuild.builtAllBodyCount; ++bodyIndex )
        {
            const RunReplayPredictionBodySample& seedBody = frames[0].bodies[bodyIndex];

            if ( seedBody.id.value == rootId.value )
            {
                continue;
            }

            if ( !AppendReplayPredictionAllBodyTrajectoryFrames( prediction, frames,
                                                                 prediction.trajectoryBuild.allBodyFrameCount, frameCount,
                                                                 seedBody, usingBuildFrames ) )
            {
                return;
            }
        }
    }

    const std::size_t firstBody = prediction.trajectoryBuild.builtAllBodyCount;
    std::size_t completedBodies = bodyCount;

    for ( std::size_t bodyIndex = firstBody; bodyIndex < bodyCount; ++bodyIndex )
    {
        // Invariant: a body record is indivisible, so the budget is read between
        // whole bodies. Recording the reached index and keeping allBodyPaths set
        // makes builtAllBodyCount the resume cursor rather than a completion
        // claim; the next frame continues from here instead of restarting.
        if ( ReplayPredictionSchedulingOperations::ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            completedBodies = bodyIndex;
            break;
        }

        const RunReplayPredictionBodySample& seedBody = frames[0].bodies[bodyIndex];

        if ( seedBody.id.value == 0u || seedBody.id.value == rootId.value )
        {
            continue;
        }

        if ( !BuildReplayPredictionAllBodyTrajectoryRecord( prediction, frames, frameCount, seedBody, usingBuildFrames ) )
        {
            return;
        }
    }

    prediction.trajectoryBuild.allBodyFrameCount = frameCount;
    prediction.trajectoryBuild.builtAllBodyCount = completedBodies;
    prediction.trajectoryBuild.allBodyBodyCount = bodyCount;
    prediction.trajectoryBuild.allBodyPaths = true;
}

void UpdateReplayPredictionTrajectoryStore( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                            bool usingBuildFrames, Physics::PhysicsSceneObjectId rootId,
                                            const std::chrono::steady_clock::time_point& budgetStart,
                                            double budgetMilliseconds )
{
    frameCount = (std::min)( frameCount, frames.size() );

    if ( rootId.value == 0 || frameCount < 2u )
    {
        prediction.trajectoryBuild.childFrameCount = 0;
        prediction.trajectoryBuild.builtNodeCount = 0;
        prediction.trajectoryBuild.childAppendTargetFrameCount = 0;
        prediction.trajectoryBuild.childAppendNodeIndex = 0;
        prediction.trajectoryBuild.allBodyFrameCount = 0;
        prediction.trajectoryBuild.builtAllBodyCount = 0;
        prediction.trajectoryBuild.allBodyBodyCount = 0;
        prediction.trajectoryBuild.allBodyPaths = false;
        return;
    }

    // Update the topology-independent body bank before causal publication
    // changes the shared source-bank cursor below.
    UpdateReplayPredictionAllBodyTrajectories( prediction, frames, frameCount, usingBuildFrames, rootId, budgetStart,
                                               budgetMilliseconds );

    if ( !prediction.trajectoryBuild.valid )
    {
        return;
    }

    const std::size_t nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(), REPLAY_PATH_MAX_FUTURE_NODES );

    const uint32_t topologyVersion = prediction.futureNodeCache.futureNodesTopologyVersion;
    const std::size_t existingNodeCount = (std::min)( prediction.trajectoryBuild.builtNodeCount, nodeCount );
    bool existingTopologyChanged = false;

    for ( std::size_t nodeIndex = 0; nodeIndex < existingNodeCount; ++nodeIndex )
    {
        const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[nodeIndex];

        if ( !ReplayPredictionChildTrajectoryRecordMatches( prediction, node, nodeIndex, usingBuildFrames,
                                                            ReplayTrajectoryLane::FutureChildIncoming ) ||
             !ReplayPredictionChildTrajectoryRecordMatches( prediction, node, nodeIndex, usingBuildFrames,
                                                            ReplayTrajectoryLane::FutureChildOutgoing ) )
        {
            existingTopologyChanged = true;
            break;
        }
    }

    const bool sourceChanged = prediction.trajectoryBuild.rootId.value != rootId.value ||
                               prediction.trajectoryBuild.usingBuildFrames != usingBuildFrames || existingTopologyChanged ||
                               prediction.trajectoryBuild.childFrameCount > frameCount ||
                               prediction.trajectoryBuild.childAppendTargetFrameCount > frameCount ||
                               prediction.trajectoryBuild.builtNodeCount > nodeCount;

    if ( sourceChanged )
    {
        // Invariant: establish the committed-bank identity before yielding.
        // builtNodeCount then owns the exact next whole-node resume boundary;
        // the visible completed-build snapshot remains separate until closure.
        prediction.trajectoryBuild.rootId = rootId;
        prediction.trajectoryBuild.usingBuildFrames = usingBuildFrames;
        prediction.trajectoryBuild.childFrameCount = frameCount;
        prediction.trajectoryBuild.builtNodeCount = 0u;
        prediction.trajectoryBuild.childAppendTargetFrameCount = frameCount;
        prediction.trajectoryBuild.childAppendNodeIndex = 0u;
        prediction.trajectoryBuild.topologyVersion = topologyVersion;
        prediction.trajectoryBuild.valid = true;
    }

    if ( !sourceChanged && prediction.trajectoryBuild.childFrameCount == frameCount &&
         prediction.trajectoryBuild.builtNodeCount == nodeCount )
    {
        return;
    }

    // Hazard: child records depend on the frozen future-node order. When the
    // source prefix or topology changes, replace the affected records instead
    // of mutating already-published points under an old version.
    if ( !sourceChanged && prediction.trajectoryBuild.childFrameCount < frameCount )
    {
        if ( prediction.trajectoryBuild.childAppendTargetFrameCount <= prediction.trajectoryBuild.childFrameCount )
        {
            prediction.trajectoryBuild.childAppendTargetFrameCount = frameCount;
            prediction.trajectoryBuild.childAppendNodeIndex = 0u;
        }

        const std::size_t appendTargetFrameCount = prediction.trajectoryBuild.childAppendTargetFrameCount;

        for ( std::size_t i = prediction.trajectoryBuild.childAppendNodeIndex; i < existingNodeCount; ++i )
        {
            if ( ReplayPredictionSchedulingOperations::ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
            {
                prediction.trajectoryBuild.childAppendNodeIndex = i;
                return;
            }

            const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[i];

            if ( !AppendReplayPredictionChildTrajectoryFrames( prediction, frames,
                                                               prediction.trajectoryBuild.childFrameCount,
                                                               appendTargetFrameCount, node, i, usingBuildFrames,
                                                               ReplayTrajectoryLane::FutureChildIncoming ) ||
                 !AppendReplayPredictionChildTrajectoryFrames( prediction, frames,
                                                               prediction.trajectoryBuild.childFrameCount,
                                                               appendTargetFrameCount, node, i, usingBuildFrames,
                                                               ReplayTrajectoryLane::FutureChildOutgoing ) )
            {
                return;
            }

            prediction.trajectoryBuild.childAppendNodeIndex = i + 1u;
        }

        prediction.trajectoryBuild.childFrameCount = appendTargetFrameCount;
        prediction.trajectoryBuild.childAppendNodeIndex = 0u;

        // Invariant: a worker may publish another suffix while this pass is in
        // flight. Finish one coherent target for every existing node before
        // adopting that newer count on the next presentation pass.
        if ( appendTargetFrameCount < frameCount )
        {
            return;
        }
    }

    const std::size_t firstNode = prediction.trajectoryBuild.builtNodeCount;
    std::size_t completedNodeCount = nodeCount;

    for ( std::size_t i = firstNode; i < nodeCount; ++i )
    {
        if ( ReplayPredictionSchedulingOperations::ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
        {
            completedNodeCount = i;
            break;
        }

        const RunReplayPathTraceNode& node = prediction.futureNodeCache.futureNodes[i];

        if ( !BuildReplayPredictionChildTrajectoryRecord( prediction, frames, frameCount, node, i, usingBuildFrames,
                                                          ReplayTrajectoryLane::FutureChildIncoming, false ) ||
             !BuildReplayPredictionChildTrajectoryRecord( prediction, frames, frameCount, node, i, usingBuildFrames,
                                                          ReplayTrajectoryLane::FutureChildOutgoing, true ) )
        {
            return;
        }
    }

    // New causal nodes are appended records. A version change alone therefore
    // advances readiness without replacing already-published record prefixes.
    prediction.trajectoryBuild.topologyVersion = topologyVersion;
    prediction.trajectoryBuild.childFrameCount = frameCount;
    prediction.trajectoryBuild.builtNodeCount = completedNodeCount;
    prediction.trajectoryBuild.childAppendTargetFrameCount = frameCount;
    prediction.trajectoryBuild.childAppendNodeIndex = 0u;
}

bool TryFlipReplayPredictionCommittedPublication( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId rootId,
                                                  std::size_t frameCount, ReplayFrameIndex revealFrame,
                                                  const std::chrono::steady_clock::time_point& budgetStart,
                                                  double budgetMilliseconds )
{
    if ( !prediction.committedPublication.pending )
    {
        return false;
    }

    const ReplayPredictionTrajectoryBank replacementBank = prediction.committedPublication.ReplacementTrajectoryBank();
    const bool replacementUsesBuildBank = replacementBank == ReplayPredictionTrajectoryBank::Build;
    const std::size_t nodeCount = (std::min)( prediction.futureNodeCache.futureNodes.size(),
                                              static_cast<std::size_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );
    ReplayPredictionChildMarkerScanState& markerScan = prediction.futureNodeCache.childMarkerScan;
    const bool markerPublicationReady = nodeCount == 0u ||
                                        markerScan.Matches( prediction.build.generationBeginCount,
                                                            prediction.futureNodeCache.futureNodesTopologyVersion, nodeCount,
                                                            rootId, frameCount, revealFrame, replacementUsesBuildBank,
                                                            true );

    if ( !prediction.FutureTreePublicationComplete( prediction.trajectoryBuild, rootId, replacementUsesBuildBank,
                                                    frameCount ) ||
         !markerPublicationReady ||
         ReplayPredictionSchedulingOperations::ReplayPredictionBudgetExpired( budgetStart, budgetMilliseconds ) )
    {
        return false;
    }

    // Invariant: topology, child/all-body trajectories, retained markers, and
    // the active trajectory prefix now describe one committed generation. The
    // obsolete visible records become dormant in this same reader-visible flip.
    // A hidden build bank is normalized to canonical committed keys so final
    // identity and fingerprints do not depend on completion timing.
    prediction.futureNodeCache.futureNodesBuiltFromBuildFrames = false;

    if ( replacementBank == ReplayPredictionTrajectoryBank::Build )
    {
        prediction.trajectoryStore.CommitPredictionReplacementBank( replacementBank, REPLAY_TRAJECTORY_BUILD_BRANCH,
                                                                    static_cast<uint16_t>( REPLAY_PATH_MAX_FUTURE_NODES ) );
    }
    else
    {
        // Invariant: the first completed generation replaces the committed
        // bank while its already-presented build trajectories remain part of
        // the approved reveal packet. A later refresh selects Build as its
        // replacement bank, retires these rows before rebuilding them, and
        // therefore cannot accumulate another generation of active records.
        // The immutable 200-box fidelity oracle locks this first-cascade shape.
        ++prediction.trajectoryStore.publicationVersion;
    }

    prediction.trajectoryBuild.usingBuildFrames = false;

    if ( nodeCount > 0u )
    {
        // Why: the retained marker values already belong to this completed
        // topology. Canonicalize only the bank key so the next idle frame does
        // not repeat the bounded frame-by-node scan after a build-bank flip.
        markerScan.Commit( prediction.build.generationBeginCount, prediction.futureNodeCache.futureNodesTopologyVersion,
                           rootId, frameCount, revealFrame, false, true );
    }

    prediction.committedPublication.Reset();
    return true;
}

bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body )
{
    return VectorMagSquared( body.linearVelocity ) >= REPLAY_PREDICTION_CHILD_LINEAR_SPEED_SQ;
}

std::size_t BuildReplayPredictionAffectedBodyTrails( std::span<const RunReplayPredictionFrame> frames,
                                                     std::size_t frameCount, ReplayFrameIndex revealFrame,
                                                     Physics::PhysicsSceneObjectId rootId, int rootModelIndex,
                                                     std::span<const RunReplayPathTraceNode> futureNodes,
                                                     const SceneEntityStore& entities,
                                                     std::span<ReplayPredictionAffectedBodyTrail> outTrails )
{
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount < 2 || rootId.value == 0 || outTrails.empty() )
    {
        return 0;
    }

    const auto idIsAlreadyPublished = [futureNodes]( Physics::PhysicsSceneObjectId id )
    {
        for ( const RunReplayPathTraceNode& node : futureNodes )
        {
            if ( node.id.value == id.value )
            {
                return true;
            }
        }

        return false;
    };

    // Concept: affected-body trails are visual evidence, not contact authority.
    // The future-node cache is authoritative when it already names a body; this
    // bounded fallback derives only bodies whose motion is visible by revealFrame.
    std::size_t trailCount = 0;
    const RunReplayPredictionFrame& firstFrame = frames.front();

    for ( const RunReplayPredictionBodySample& initialBody : firstFrame.bodies )
    {
        if ( trailCount >= outTrails.size() )
        {
            break;
        }

        if ( initialBody.id.value == 0 || initialBody.id.value == rootId.value ||
             initialBody.modelRow.value == rootModelIndex || idIsAlreadyPublished( initialBody.id ) ||
             ReplayModelIndexIsRagdollPart( entities, initialBody.modelRow.value ) )
        {
            continue;
        }

        for ( std::size_t frameSlot = 1; frameSlot < frameCount; ++frameSlot )
        {
            // Why: delaying construction until the first revealed motion keeps
            // both marker publication and drawing from pre-spawning the body.
            if ( frames[frameSlot].frameIndex > revealFrame )
            {
                break;
            }

            const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frames[frameSlot],
                                                                                              initialBody.id,
                                                                                              initialBody.modelRow.value );

            if ( !body || !ReplayPredictionBodyHasVisibleLinearMotion( *body ) )
            {
                continue;
            }

            ReplayPredictionAffectedBodyTrail& trail = outTrails[trailCount++];
            trail.id = initialBody.id;
            trail.modelRow.value = body->modelRow.value;
            trail.firstFrameSlot = frameSlot;
            trail.firstFrame = frames[frameSlot].frameIndex;
            trail.lastMotionFrame = frames[frameSlot].frameIndex;
            trail.previous = initialBody.position;
            trail.entryPosition = initialBody.position;
            trail.entryOrientation = initialBody.orientation;
            trail.entryOrientation.Normalise();
            break;
        }
    }

    return trailCount;
}

// Concept: rest is decided by how the story ends, never by a momentary pause.
//
// A body has a resting pose only when the COMPLETED prediction ends with it
// visibly still and it has not drifted across the final grace window. Bodies
// still moving at the horizon end return false: they get a travel line and no
// grey box, because any resting pose we could draw for them would be a guess.
// Invariant: callers must pass a completed frame buffer; a growing build
// prefix has no authoritative final frame.
bool ReplayPredictionBodyRestingPose( const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                      Physics::PhysicsSceneObjectId id, int modelIndexHint, Vector3& outPosition,
                                      Quaternion& outOrientation )
{
    frameCount = (std::min)( frameCount, frames.size() );

    if ( frameCount < 2 || id.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionBodySample* finalBody = FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1], id,
                                                                                           modelIndexHint );

    if ( !finalBody || ReplayPredictionBodyHasVisibleLinearMotion( *finalBody ) )
    {
        return false;
    }

    const std::size_t graceSlots = (std::min)( static_cast<std::size_t>( REPLAY_PREDICTION_REST_GRACE_FRAMES ),
                                               frameCount - 1 );

    const RunReplayPredictionBodySample*
        graceBody = FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1 - graceSlots], id, modelIndexHint );

    if ( !graceBody || ReplayPredictionBodyHasVisibleLinearMotion( *graceBody ) ||
         VectorMagSquared( finalBody->position - graceBody->position ) > REPLAY_PREDICTION_REST_POSITION_EPSILON_SQ )
    {
        return false;
    }

    outPosition = finalBody->position;
    outOrientation = finalBody->orientation;
    return true;
}

bool ReplayContactHasModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                 int modelIndex )
{
    return modelIndex >= 0 && ( contact.bodyA == modelIndex || contact.bodyB == modelIndex );
}

int ReplayContactOtherModelIndex( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                  int modelIndex )
{
    if ( contact.bodyA == modelIndex )
    {
        return contact.bodyB;
    }

    if ( contact.bodyB == modelIndex )
    {
        return contact.bodyA;
    }

    return -1;
}

Vector3 ReplayContactPoint( const ReplaySolverFrameSample& sample,
                            const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    if ( const ReplaySolverBodySample* bodyA = FindReplayBodyByModelIndex( sample, contact.bodyA ) )
    {
        return bodyA->position + contact.rA;
    }

    if ( const ReplaySolverBodySample* bodyB = FindReplayBodyByModelIndex( sample, contact.bodyB ) )
    {
        return bodyB->position + contact.rB;
    }

    return SkullbonezCore::Math::Vector::ZERO_VECTOR;
}

Vector3 ReplayContactNormalForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                     int modelIndex )
{
    Vector3 normal = contact.normal;

    if ( contact.isTerrain && VectorMagSquared( contact.terrainNormal ) > TOLERANCE * TOLERANCE )
    {
        normal = contact.terrainNormal;
    }

    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        normal = normal * -1.0f;
    }

    return ReplayNormalizeOr( normal, Vector3( 0.0f, 1.0f, 0.0f ) );
}

Vector3 ReplayContactImpulseForModel( const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact,
                                      int modelIndex )
{
    const Vector3 rowImpulse = contact.normal * contact.accN + contact.tangent1 * contact.accT1 +
                               contact.tangent2 * contact.accT2;

    if ( contact.bodyB == modelIndex && !contact.isTerrain )
    {
        return rowImpulse;
    }

    return rowImpulse * -1.0f;
}

int ReplayFindPipelineIndexForContact( const SkullbonezCore::Physics::PhysicsSolverSnapshot& snapshot,
                                       const SkullbonezCore::Physics::PhysicsSolverPersistentContactSample& contact )
{
    for ( int i = 0; i < static_cast<int>( snapshot.pipelineTrace.size() ); ++i )
    {
        const PhysicsPipelineRecord& record = snapshot.pipelineTrace[static_cast<std::size_t>( i )];

        if ( record.featureId == contact.featureId &&
             ( ( record.bodyA == contact.bodyA && record.bodyB == contact.bodyB ) ||
               ( record.bodyA == contact.bodyB && record.bodyB == contact.bodyA ) ) )
        {
            return i;
        }
    }

    return -1;
}


void ClearReplayPredictionBaseline( ReplayPredictionBaselineSnapshot& baseline )
{
    baseline.valid = false;
    baseline.comparisonActive = false;
    baseline.rootId = Physics::PhysicsSceneObjectId {};
    baseline.rootModelRow.value = -1;
    baseline.lastFrame = 0;
    baseline.rootPolyline.clear();
    baseline.bodyPoses.clear();
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
}

bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction );
std::size_t ReplayTrajectoryPublishedPointCount( const ReplayTrajectoryRecord& record );
const ReplayTrajectoryRecord* ReplayTrajectoryRecordForDraw( const ReplayTrajectoryStore& store,
                                                             Physics::PhysicsSceneObjectId id, ReplayTrajectoryLane lane,
                                                             uint16_t branchOrdinal );

// Concept: baseline capture freezes the old committed future before a velocity
// edit. It keeps a bounded root path plus completed entry/rest poses so the
// renderer can contrast "what would have happened" against the nudged rebuild.
bool CaptureReplayPredictionBaselineSnapshot( RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                              Physics::PhysicsSceneObjectId rootId, int rootModelIndex )
{
    frameCount = (std::min)( frameCount, frames.size() );
    ClearReplayPredictionBaseline( prediction.baseline );

    if ( frameCount < 2 || rootId.value == 0 )
    {
        return false;
    }

    const RunReplayPredictionFrame& firstFrame = frames.front();
    const RunReplayPredictionFrame& lastFrame = frames[frameCount - 1];
    const std::size_t bodyCapacity = (std::min)( static_cast<std::size_t>( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS ),
                                                 firstFrame.bodies.size() );

    const int reserveFrame = static_cast<int>( lastFrame.frameIndex );

    if ( !ReserveReplayPredictionVector( prediction.baseline.rootPolyline, REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY,
                                         reserveFrame, "ReplayPredictionBaselineRootPoint[]" ) ||
         !ReserveReplayPredictionVector( prediction.baseline.bodyPoses, bodyCapacity, reserveFrame,
                                         "ReplayPredictionBaselineBodyPose[]" ) )
    {
        ClearReplayPredictionBaseline( prediction.baseline );
        return false;
    }

    prediction.baseline.rootId = rootId;
    prediction.baseline.rootModelRow.value = rootModelIndex;
    prediction.baseline.lastFrame = lastFrame.frameIndex;

    const std::size_t rootStride = frameCount <= REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY
                                       ? 1u
                                       : ( frameCount + REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY - 1u ) /
                                             REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY;

    for ( std::size_t frameSlot = 0; frameSlot < frameCount; ++frameSlot )
    {
        const RunReplayPredictionFrame& frame = frames[frameSlot];
        const bool endpointFrame = frameSlot == 0 || frameSlot + 1 == frameCount;

        if ( !endpointFrame && rootStride > 1u && ( frame.frameIndex % static_cast<ReplayFrameIndex>( rootStride ) ) != 0u )
        {
            continue;
        }

        const RunReplayPredictionBodySample* body = FindReplayPredictionBodyByIdWithHint( frame, rootId, rootModelIndex );

        if ( !body )
        {
            continue;
        }

        ReplayPredictionBaselineRootPoint point;
        point.frameIndex = frame.frameIndex;
        point.position = body->position;

        if ( prediction.baseline.rootPolyline.size() < REPLAY_PREDICTION_BASELINE_ROOT_POINT_CAPACITY )
        {
            prediction.baseline.rootPolyline.push_back( point );
        }
        else if ( endpointFrame && !prediction.baseline.rootPolyline.empty() )
        {
            prediction.baseline.rootPolyline.back() = point;
        }
    }

    for ( const RunReplayPredictionBodySample& body : firstFrame.bodies )
    {
        if ( prediction.baseline.bodyPoses.size() >= bodyCapacity || body.id.value == 0 )
        {
            break;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;
        const bool hasRestPose = ReplayPredictionBodyRestingPose( frames, frameCount, body.id, body.modelRow.value,
                                                                  restPosition, restOrientation );

        if ( !hasRestPose )
        {
            const RunReplayPredictionBodySample* horizonBody = FindReplayPredictionBodyByIdWithHint( lastFrame, body.id,
                                                                                                     body.modelRow.value );

            if ( !horizonBody )
            {
                continue;
            }

            // Why: orbital bodies never reach a resting pose. Retain their
            // horizon endpoint for divergence math, but keep hasRestPose false
            // so the renderer does not mislabel it as a grey rest marker.
            restPosition = horizonBody->position;
            restOrientation = horizonBody->orientation;
        }

        ReplayPredictionBaselineBodyPose pose;
        pose.id = body.id;
        pose.modelRow.value = body.modelRow.value;
        pose.hasEntryPose = true;
        pose.hasRestPose = hasRestPose;
        pose.entryPosition = body.position;
        pose.entryOrientation = body.orientation;
        pose.entryOrientation.Normalise();
        pose.restPosition = restPosition;
        pose.restOrientation = restOrientation;
        pose.restOrientation.Normalise();
        prediction.baseline.bodyPoses.push_back( pose );
    }

    prediction.baseline.valid = prediction.baseline.rootPolyline.size() >= 2 || !prediction.baseline.bodyPoses.empty();
    prediction.baseline.comparisonActive = prediction.baseline.valid;

    if ( prediction.baseline.valid && !PublishReplayPredictionBaselineRootTrajectory( prediction ) )
    {
        ClearReplayPredictionBaseline( prediction.baseline );
        return false;
    }

    return prediction.baseline.valid;
}

bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction )
{
    const ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;

    if ( !baseline.valid || baseline.rootId.value == 0 || baseline.rootPolyline.size() < 2u )
    {
        return true;
    }

    ReplayTrajectoryRecord* record = BeginReplayTrajectoryRecord( prediction.trajectoryStore,
                                                                  ReplayTrajectoryKey( baseline.rootId,
                                                                                       ReplayTrajectoryLane::BaselineRoot,
                                                                                       REPLAY_TRAJECTORY_COMMITTED_BRANCH ),
                                                                  0, Physics::PhysicsSceneObjectId {}, 0, 0, false,
                                                                  baseline.rootPolyline.size() );

    if ( !record )
    {
        return false;
    }

    for ( const ReplayPredictionBaselineRootPoint& point : baseline.rootPolyline )
    {
        if ( !AppendReplayTrajectoryPoint( prediction.trajectoryStore, *record, point.frameIndex, point.position ) )
        {
            return false;
        }
    }

    return true;
}

// Concept: divergence is a demo-facing separation metric, not physics authority.
// It sums how far matched bodies' resting endpoints moved between the cold
// baseline and the rebuilt prediction.
void UpdateReplayPredictionBaselineDivergence( RunReplayPredictionState& prediction,
                                               const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount )
{
    ReplayPredictionBaselineSnapshot& baseline = prediction.baseline;
    baseline.divergenceValid = false;
    baseline.divergenceUnits = 0.0f;
    frameCount = (std::min)( frameCount, frames.size() );

    if ( !baseline.valid || frameCount < 2 || baseline.bodyPoses.empty() )
    {
        return;
    }

    float divergence = 0.0f;
    int matchedBodies = 0;

    for ( const ReplayPredictionBaselineBodyPose& baselinePose : baseline.bodyPoses )
    {
        if ( baselinePose.id.value == 0 )
        {
            continue;
        }

        Vector3 restPosition = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        Quaternion restOrientation = IDENTITY_QUATERNION;

        if ( baselinePose.hasRestPose )
        {
            if ( !ReplayPredictionBodyRestingPose( frames, frameCount, baselinePose.id, baselinePose.modelRow.value,
                                                   restPosition, restOrientation ) )
            {
                continue;
            }
        }
        else
        {
            const RunReplayPredictionBodySample*
                horizonBody = FindReplayPredictionBodyByIdWithHint( frames[frameCount - 1u], baselinePose.id,
                                                                    baselinePose.modelRow.value );

            if ( !horizonBody )
            {
                continue;
            }

            restPosition = horizonBody->position;
        }

        divergence += VectorMag( restPosition - baselinePose.restPosition );
        ++matchedBodies;
    }

    baseline.divergenceUnits = divergence;
    baseline.divergenceValid = matchedBodies > 0;
}

} // namespace SkullbonezCore::Runtime::ReplayPredictionPublicationOperations
