/*
File: ReplayPredictionPublicationOperations.h
Purpose:
  Declares the Prediction domain's internal publication operations.

Summary:
  Scheduling and isolated simulation call these narrow operations without
  owning trajectory, baseline, topology, marker, or sample-lookup policy.

Glossary:
  Published prefix: Contiguous prediction frames whose completed rows are
    visible to readers after a release/acquire publication step.
  Topology publication: Bounded cause-tree and trajectory values derived only
    from the published frame prefix.
  Model row hint: Sample-local shortcut repaired against stable scene identity;
    never durable identity itself.

Invariants:
  - This is an internal Replay header and is not part of ReplayRuntime's public seam.
  - Published frame rows are written before any prefix or derived topology is exposed.
  - Solver lookup preserves its legacy negative-row scan; prediction lookup
    rejects negative rows before scanning.

Related:
  - ReplayPredictionPublication.cpp
  - ReplayPredictionTopologyPublication.cpp
  - ReplayPrediction.cpp
*/
#pragma once

#include "ReplayPrediction.h"

namespace SkullbonezCore
{
namespace Runtime
{
namespace ReplayPredictionPublicationOperations
{
struct ReplayPastRootRebuildContext
{
    ReplayTrajectoryStore* store = nullptr;
    ReplayTrajectoryRecord* record = nullptr;
    Physics::ModelRowHint targetModelRow;
    ReplayFrameIndex firstFrame = 0;
    bool hasSample = false;
    bool ok = true;
};

// Concept: solver samples and prediction frames share one value-only lookup
// policy because both expose `bodies` rows with stable `id` and repairable
// `modelRow` fields. No owner or callback crosses this seam.
template <typename FrameSample, typename BodySample>
const BodySample* FindReplayBodyByIdInSample( const FrameSample& sample, Physics::PhysicsSceneObjectId id )
{
    for ( const BodySample& body : sample.bodies )
    {
        if ( body.id.value == id.value )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
const BodySample* FindReplayBodyByModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if constexpr ( !AllowNegativeModelIndex )
    {
        if ( modelIndex < 0 )
        {
            return nullptr;
        }
    }

    if ( modelIndex >= 0 && modelIndex < static_cast<int>( sample.bodies.size() ) )
    {
        const BodySample& body = sample.bodies[static_cast<std::size_t>( modelIndex )];
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }

    for ( const BodySample& body : sample.bodies )
    {
        if ( body.modelRow.value == modelIndex )
        {
            return &body;
        }
    }
    return nullptr;
}

template <typename FrameSample, typename BodySample, bool AllowNegativeModelIndex>
Physics::PhysicsSceneObjectId SceneObjectIdForModelIndexInSample( const FrameSample& sample, int modelIndex )
{
    if ( const BodySample* body =
             FindReplayBodyByModelIndexInSample<FrameSample, BodySample, AllowNegativeModelIndex>( sample,
                                                                                                   modelIndex ) )
    {
        return body->id;
    }
    return Physics::PhysicsSceneObjectId{};
}

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample,
                                                  Physics::PhysicsSceneObjectId id );
const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex );
const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   Physics::PhysicsSceneObjectId id );
const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex );
const RunReplayPredictionBodySample* FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame,
                                                                           Physics::PhysicsSceneObjectId id,
                                                                           int modelIndex );
Physics::PhysicsSceneObjectId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame,
                                                                   int modelIndex );
bool ReplayModelIndexIsRagdollPart( const SceneEntityStore& entities, int modelIndex );
int ReplayRagdollTorsoModelIndexForPart( const SceneEntityStore& entities, int modelIndex );
Math::Vector::Vector3 ReplayNormalizeOr( Math::Vector::Vector3 value, const Math::Vector::Vector3& fallback );

ReplayFrameIndex ReplayOldestFrameFromStats( const ReplayRecorderStats& stats );
int ReplayTrajectoryFrameNumberForReserve( ReplayFrameIndex frameIndex );
ReplayTrajectoryRecordKey ReplayPastRootTrajectoryKey( Physics::PhysicsSceneObjectId targetId );
ReplayTrajectoryRecord* BeginReplayPastRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                             Physics::PhysicsSceneObjectId targetId,
                                                             std::size_t pointCapacity,
                                                             int frameNumber );
bool AppendReplayTrajectoryPoint( ReplayTrajectoryStore& store,
                                  ReplayTrajectoryRecord& record,
                                  ReplayFrameIndex frameIndex,
                                  const Math::Vector::Vector3& position );
const ReplaySolverBodySample*
FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample, Physics::PhysicsSceneObjectId id, int modelIndex );

bool PrepareReplayPredictionTrajectoryBuild( RunReplayPredictionState& prediction,
                                             Physics::PhysicsSceneObjectId rootId,
                                             std::size_t frameCapacity );
bool PublishReplayPredictionRootTrajectoryFrame( RunReplayPredictionState& prediction,
                                                 const RunReplayPredictionFrame& frame,
                                                 std::size_t frameSlot );
bool RebuildReplayPredictionCommittedRootTrajectory( RunReplayPredictionState& prediction );
bool CaptureReplayPredictionBaselineSnapshot( RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames,
                                              std::size_t frameCount,
                                              Physics::PhysicsSceneObjectId rootId,
                                              int rootModelIndex );
bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction );
void UpdateReplayPredictionBaselineDivergence( RunReplayPredictionState& prediction,
                                               const std::vector<RunReplayPredictionFrame>& frames,
                                               std::size_t frameCount );
void UpdateReplayPredictionTrajectoryStore( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames,
                                            std::size_t frameCount,
                                            bool usingBuildFrames,
                                            Physics::PhysicsSceneObjectId rootId );
bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body );
bool ReplayPredictionBodyRestingPose( const std::vector<RunReplayPredictionFrame>& frames,
                                      std::size_t frameCount,
                                      Physics::PhysicsSceneObjectId id,
                                      int modelIndexHint,
                                      Math::Vector::Vector3& outPosition,
                                      Math::Orientation::Quaternion& outOrientation );
void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction );
void RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( RunReplayPredictionState& prediction,
                                                                const SceneEntityStore& entities,
                                                                Physics::PhysicsSceneObjectId rootId );
void PrepareReplayPredictionOverlay( RunReplayPredictionState& prediction,
                                     const SceneEntityStore& entities,
                                     const Physics::ColliderStore& colliderStore,
                                     Physics::PhysicsSceneObjectId targetId,
                                     Physics::ModelRowHint targetModelRow,
                                     bool targetAvailable,
                                     double budgetMilliseconds,
                                     ReplayPredictionUpdateResult& result );
} // namespace ReplayPredictionPublicationOperations
} // namespace Runtime
} // namespace SkullbonezCore
