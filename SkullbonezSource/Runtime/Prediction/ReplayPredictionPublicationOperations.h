/*
File: SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h
Purpose:
  Declares the Prediction domain's internal publication operations.

Summary:
  Scheduling, publication, and drawing call these narrow operations without
  taking ownership of trajectory, baseline, topology, marker, or lookup state.

Glossary:
  Topology publication: Bounded cause-tree and trajectory values derived only
    from the published frame prefix.
  Affected-body trail: Bounded fallback evidence for a moving body that is not
    already represented by the published causal topology.
  Build-root prefix: Worker-filled root points made reader-visible only through
    the frame thread's acquire-latched presentation count.

Invariants:
  - This is an internal Replay header and is not part of ReplayRuntime's public seam.
  - Published frame rows are written before any prefix or derived topology is exposed.
  - The frame thread publishes no build-root point beyond the presentation
    prefix acquired from ReplayPredictionPublication.
  - Solver lookup preserves its legacy negative-row scan; prediction lookup
    rejects negative rows before scanning.
  - Trail derivation writes only to caller-owned spans and never allocates.

Related:
  - ReplayPredictionPublication.cpp
  - ReplayPredictionTopologyPublication.cpp
  - ReplayPrediction.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayPrediction.h"

#include <span>

namespace SkullbonezCore
{
namespace Runtime
{
namespace ReplayPredictionPublicationOperations
{
struct ReplayPredictionAffectedBodyTrail
{
    Physics::PhysicsSceneObjectId id;
    Physics::ModelRowHint modelRow;
    std::size_t firstFrameSlot = 0;
    ReplayFrameIndex firstFrame = 0;
    int causalDepth = 1;

    // Concept: entry is the body's in-place pose before the predicted impulse;
    // lastMotionFrame controls when the completed-buffer rest pose may appear.
    ReplayFrameIndex lastMotionFrame = 0;
    Math::Vector::Vector3 previous = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 entryPosition = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion entryOrientation = Math::Orientation::IDENTITY_QUATERNION;
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

    if ( const BodySample* body = FindReplayBodyByModelIndexInSample<FrameSample, BodySample,
                                                                     AllowNegativeModelIndex>( sample, modelIndex ) )
    {
        return body->id;
    }

    return Physics::PhysicsSceneObjectId {};
}

const ReplaySolverBodySample* FindReplayBodyById( const ReplaySolverFrameSample& sample, Physics::PhysicsSceneObjectId id );
const ReplaySolverBodySample* FindReplayBodyByModelIndex( const ReplaySolverFrameSample& sample, int modelIndex );
const RunReplayPredictionBodySample* FindReplayPredictionBodyById( const RunReplayPredictionFrame& frame,
                                                                   Physics::PhysicsSceneObjectId id );
const RunReplayPredictionBodySample* FindReplayPredictionBodyByModelIndex( const RunReplayPredictionFrame& frame,
                                                                           int modelIndex );
const RunReplayPredictionBodySample* FindReplayPredictionBodyByIdWithHint( const RunReplayPredictionFrame& frame,
                                                                           Physics::PhysicsSceneObjectId id,
                                                                           int modelIndex );
Physics::PhysicsSceneObjectId ReplayPredictionBodyIdForModelIndex( const RunReplayPredictionFrame& frame, int modelIndex );
bool ReplayModelIndexIsRagdollPart( const SceneEntityStore& entities, int modelIndex );
int ReplayRagdollTorsoModelIndexForPart( const SceneEntityStore& entities, int modelIndex );
Math::Vector::Vector3 ReplayNormalizeOr( Math::Vector::Vector3 value, const Math::Vector::Vector3& fallback );

// Concept: publication and drawing share one bounded sampling policy so the
// prepared topology and emitted ribbons cannot drift to different densities.
std::size_t ReplayPredictionPathStrideForSampleCount( std::size_t sampleCount ) noexcept;
constexpr std::size_t ReplayPredictionBuildRootPrefixCount( std::size_t presentedFrameCount,
                                                            std::size_t rootPointCapacity ) noexcept
{
    return presentedFrameCount < rootPointCapacity ? presentedFrameCount : rootPointCapacity;
}

ReplayFrameIndex ReplayOldestFrameFromStats( const ReplayRecorderStats& stats );
int ReplayTrajectoryFrameNumberForReserve( ReplayFrameIndex frameIndex );
ReplayTrajectoryRecordKey ReplayPastRootTrajectoryKey( Physics::PhysicsSceneObjectId targetId );
ReplayTrajectoryRecord* BeginReplayPastRootTrajectoryRecord( ReplayTrajectoryStore& store,
                                                             Physics::PhysicsSceneObjectId targetId,
                                                             std::size_t pointCapacity, int frameNumber );
bool AppendReplayTrajectoryPoint( ReplayTrajectoryStore& store, ReplayTrajectoryRecord& record, ReplayFrameIndex frameIndex,
                                  const Math::Vector::Vector3& position );
const ReplaySolverBodySample* FindReplayBodyByIdWithHint( const ReplaySolverFrameSample& sample,
                                                          Physics::PhysicsSceneObjectId id, int modelIndex );

bool PrepareReplayPredictionTrajectoryBuild( RunReplayPredictionState& prediction, Physics::PhysicsSceneObjectId rootId,
                                             std::size_t frameCapacity, std::size_t bodyCount );
bool PublishReplayPredictionRootTrajectoryFrame( RunReplayPredictionState& prediction, const RunReplayPredictionFrame& frame,
                                                 std::size_t frameSlot );
bool PublishReplayPredictionBuildRootTrajectoryPrefix( RunReplayPredictionState& prediction,
                                                       std::size_t presentedFrameCount );
bool RebuildReplayPredictionCommittedRootTrajectory( RunReplayPredictionState& prediction );
bool CaptureReplayPredictionBaselineSnapshot( RunReplayPredictionState& prediction,
                                              const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                              Physics::PhysicsSceneObjectId rootId, int rootModelIndex );
bool PublishReplayPredictionBaselineRootTrajectory( RunReplayPredictionState& prediction );
void UpdateReplayPredictionBaselineDivergence( RunReplayPredictionState& prediction,
                                               const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount );
void UpdateReplayPredictionTrajectoryStore( RunReplayPredictionState& prediction,
                                            const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                            bool usingBuildFrames, Physics::PhysicsSceneObjectId rootId );
bool ReplayPredictionBodyHasVisibleLinearMotion( const RunReplayPredictionBodySample& body );
std::size_t BuildReplayPredictionAffectedBodyTrails( std::span<const RunReplayPredictionFrame> frames,
                                                     std::size_t frameCount, ReplayFrameIndex revealFrame,
                                                     Physics::PhysicsSceneObjectId rootId, int rootModelIndex,
                                                     std::span<const RunReplayPathTraceNode> futureNodes,
                                                     const SceneEntityStore& entities,
                                                     std::span<ReplayPredictionAffectedBodyTrail> outTrails );
bool ReplayPredictionBodyRestingPose( const std::vector<RunReplayPredictionFrame>& frames, std::size_t frameCount,
                                      Physics::PhysicsSceneObjectId id, int modelIndexHint,
                                      Math::Vector::Vector3& outPosition, Math::Orientation::Quaternion& outOrientation );
void ClearReplayPredictionFutureNodeCache( RunReplayPredictionState& prediction );
void RebuildReplayPredictionCommittedTreeAfterWorkerCompletion( RunReplayPredictionState& prediction,
                                                                const SceneEntityStore& entities,
                                                                Physics::PhysicsSceneObjectId rootId );
void PrepareReplayPredictionOverlay( RunReplayPredictionState& prediction, const SceneEntityStore& entities,
                                     const Physics::ColliderStore& colliderStore, Physics::PhysicsSceneObjectId targetId,
                                     Physics::ModelRowHint targetModelRow, bool targetAvailable, double budgetMilliseconds,
                                     ReplayPredictionUpdateResult& result );
} // namespace ReplayPredictionPublicationOperations
} // namespace Runtime
} // namespace SkullbonezCore
