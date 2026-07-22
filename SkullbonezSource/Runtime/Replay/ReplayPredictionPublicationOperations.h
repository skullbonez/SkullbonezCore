/*
File: ReplayPredictionPublicationOperations.h
Purpose:
  Declares the Prediction domain's internal publication operations.

Summary:
  Scheduling and isolated simulation call these narrow operations without
  owning trajectory, baseline, topology, or marker publication policy.

Glossary:
  Published prefix: Contiguous prediction frames whose completed rows are
    visible to readers after a release/acquire publication step.
  Topology publication: Bounded cause-tree and trajectory values derived only
    from the published frame prefix.

Invariants:
  - This is an internal Replay header and is not part of ReplayRuntime's public seam.
  - Published frame rows are written before any prefix or derived topology is exposed.

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
