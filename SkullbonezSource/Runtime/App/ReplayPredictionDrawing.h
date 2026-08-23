/*
File: SkullbonezSource/Runtime/App/ReplayPredictionDrawing.h
Purpose:
  Declares retained and frame-local drawing over immutable Prediction publication.

Summary:
  App presentation owns the cursors that turn published trajectory prefixes into a
  retained draw list. Callers supply Replay-owned path selection values and
  frame-local scene/render borrows; no draw operation can schedule prediction
  work or mutate a Replay owner.

Glossary:
  Record cursor: Retained progress through one versioned trajectory record.
  Retained trail: Independently sampled marker path for a completed outgoing
    trajectory.

Invariants:
  - Draw cursors and retained command storage belong to App presentation.
  - Stable publication returns before traversing trajectory records.
  - Preview delta changes never rebuild retained non-selected paths.
  - All references in render contexts are synchronous frame borrows.

Related:
  - SkullbonezSource/Runtime/App/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/App/ReplayPredictionRetainedGeometry.h
  - SkullbonezSource/Runtime/Replay/ReplayPathPackets.h
  - SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../Prediction/ReplayPredictionView.h"
#include "ReplayPredictionRetainedGeometry.h"
#include "../Replay/ReplayPathPackets.h"
#include "../Replay/ReplayTrajectoryPackets.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SkullbonezCore::Core
{
class Profiler;
}

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsEngine;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class EditorTracer;
class SceneEntityStore;
} // namespace SkullbonezCore::Runtime

namespace SkullbonezCore::Runtime::ReplayOverlay
{
struct ReplayPredictionDrawRecordCursor
{
    ReplayTrajectoryRecordKey key;
    uint32_t recordVersion = 0;
    std::size_t sourceRecordIndex = 0;
    std::size_t consumedPointCount = 0;
    std::size_t lastSelectedPointIndex = 0;
    std::size_t retainedRangeIndex = PREDICTION_TRAJECTORY_RANGE_CAPACITY;
    uint32_t retainedRangeChunkCount = 0;
    float authoredColorR = 1.0f;
    float authoredColorG = 1.0f;
    float authoredColorB = 1.0f;
    bool usesAuthoredColor = false;
    bool entryMarkerAppended = false;
    bool endMarkerAppended = false;
};

struct ReplayPredictionDrawListState
{
    // 200 future nodes can publish incoming/outgoing records for both build and
    // committed banks, plus root/baseline/past rows.
    static constexpr std::size_t MAX_RECORD_CURSORS = 2048;

    std::array<ReplayPredictionDrawRecordCursor, MAX_RECORD_CURSORS> recordCursors = {};

    // Retained marker trails are a second presentation of child-outgoing
    // records with a denser, independently bounded sampling policy.
    std::array<ReplayPredictionDrawRecordCursor, MAX_RECORD_CURSORS> retainedTrailCursors = {};
    std::size_t recordCursorCount = 0;
    std::size_t retainedTrailCursorCount = 0;
    std::size_t retainedMarkerCount = 0;
    std::size_t baselinePoseCount = 0;
    std::size_t ordinaryRibbonCapacityRemaining = 0;
    std::size_t priorityRibbonCapacityRemaining = 0;
    Physics::PhysicsSceneObjectId targetId;
    Physics::PhysicsSceneObjectId velocityPreviewTargetId;
    ReplayFrameIndex revealFrame = 0;
    uint32_t generation = 0;
    uint32_t topologyVersion = 0;
    uint32_t trajectoryBuildTopologyVersion = 0;
    uint64_t trajectoryPublicationVersion = 0;
    std::size_t sampleStride = 1;
    ReplayPathColorMode colorMode = ReplayPathColorMode::LaneFlat;
    ReplayPredictionPathPresentation pathPresentation = ReplayPredictionPathPresentation::SelectedCausalTree;
    bool usingBuildFrames = false;
    bool velocityPreviewActive = false;
    bool saturated = false;
    bool valid = false;

    void Reset() noexcept
    {
        // Hazard: assigning this whole state from {} materializes a
        // hundreds-of-KiB temporary and can exhaust nested Debug render stacks.
        for ( ReplayPredictionDrawRecordCursor& cursor : recordCursors )
        {
            cursor = {};
        }

        for ( ReplayPredictionDrawRecordCursor& cursor : retainedTrailCursors )
        {
            cursor = {};
        }

        recordCursorCount = 0;
        retainedTrailCursorCount = 0;
        retainedMarkerCount = 0;
        baselinePoseCount = 0;
        ordinaryRibbonCapacityRemaining = 0;
        priorityRibbonCapacityRemaining = 0;
        targetId = {};
        velocityPreviewTargetId = {};
        revealFrame = 0;
        generation = 0;
        topologyVersion = 0;
        trajectoryBuildTopologyVersion = 0;
        trajectoryPublicationVersion = 0;
        sampleStride = 1;
        colorMode = ReplayPathColorMode::LaneFlat;
        pathPresentation = ReplayPredictionPathPresentation::SelectedCausalTree;
        usingBuildFrames = false;
        velocityPreviewActive = false;
        saturated = false;
        valid = false;
    }
};

struct ReplayPredictionDrawListUpdate
{
    bool reset = false;
    bool appended = false;
    bool stable = false;
};

constexpr bool IsReplayPredictionDrawListPublicationStable( bool reset, uint64_t retainedPublicationVersion,
                                                            ReplayFrameIndex retainedRevealFrame,
                                                            uint64_t incomingPublicationVersion,
                                                            ReplayFrameIndex incomingRevealFrame ) noexcept
{
    return !reset && retainedPublicationVersion == incomingPublicationVersion && retainedRevealFrame == incomingRevealFrame;
}

constexpr std::size_t ReplayPredictionFirstUnconsumedPoint( std::size_t consumedPointCount ) noexcept
{
    return consumedPointCount > 1u ? consumedPointCount : 1u;
}

constexpr bool ReplayPredictionDrawsAllBodyRecord( ReplayPredictionPathPresentation pathPresentation,
                                                   const ReplayTrajectoryRecordKey& key, uint16_t activeRootBranch,
                                                   Physics::PhysicsSceneObjectId selectedId ) noexcept
{
    return ReplayPredictionPathPresentationShowsAllBodies( pathPresentation ) &&
           key.lane == ReplayTrajectoryLane::FutureRoot && key.branchOrdinal == activeRootBranch &&
           key.bodyId.value != selectedId.value;
}

constexpr bool ReplayPredictionDrawsCausalChildRecord( ReplayPredictionPathPresentation pathPresentation,
                                                       const ReplayTrajectoryRecordKey& key, uint16_t activeChildBranchBase,
                                                       uint16_t activeChildBranchEnd ) noexcept
{
    return !ReplayPredictionPathPresentationShowsAllBodies( pathPresentation ) &&
           ( key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
             key.lane == ReplayTrajectoryLane::FutureChildOutgoing ) &&
           key.branchOrdinal >= activeChildBranchBase && key.branchOrdinal < activeChildBranchEnd;
}

constexpr bool ReplayPredictionUsesAuthoredBodyColor( ReplayPredictionPathPresentation pathPresentation,
                                                      ReplayTrajectoryLane lane ) noexcept
{
    return ReplayPredictionPathPresentationShowsAllBodies( pathPresentation ) && lane == ReplayTrajectoryLane::FutureRoot;
}

struct ReplayPathVisualizerRenderContext
{
    // Lifetime: every reference is a frame-local borrow after Prediction has
    // published for this frame.
    const ReplayPredictionPresentationView& prediction;
    Core::Profiler* profiler = nullptr;
    const RunReplayPathVisualizerState& pathVisualizer;
    Physics::PhysicsEngine& physics;
    const SceneEntityStore& entities;
    EditorTracer& tracer;
    ReplayFrameIndex presentFrame = 0;
    bool hasPresentSample = false;
    bool drawPredictionOverlay = true;
};

struct ReplayPathVisualizerRenderResult
{
    bool retainedRefreshBudgetExpired = false;
};

ReplayPredictionDrawListUpdate UpdateReplayPredictionDrawList( const ReplayPredictionPresentationView& prediction,
                                                               const RunReplayPathVisualizerState& pathVisualizer,
                                                               const SceneEntityStore& entities,
                                                               const Physics::ColliderStore& colliderStore,
                                                               ReplayPredictionRetainedGeometry& retainedGeometry,
                                                               EditorTracer& retainedMarkers,
                                                               ReplayPredictionDrawListState& state );
void AppendReplayPredictionProvisionalTails( const ReplayPredictionPresentationView& prediction,
                                             const RunReplayPathVisualizerState& pathVisualizer,
                                             const ReplayPredictionDrawListState& state,
                                             const Physics::ColliderStore& colliderStore, EditorTracer& tracer );
ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
