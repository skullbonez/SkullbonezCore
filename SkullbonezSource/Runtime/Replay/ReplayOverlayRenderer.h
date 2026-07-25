/*
File: SkullbonezSource/Runtime/Replay/ReplayOverlayRenderer.h
Purpose:
  Declares replay overlay drawing entry points used by the late UI/text pass.

Summary:
  RuntimeRenderer decides pass order, but replay owns UI drawing plus the
  retained prediction command-list cursors used by the geometry pass.

Glossary:
  UI (User Interface): Runtime controls and overlays drawn over the 3D scene.
  UI text pass: Late overlay pass that invokes replay overlay drawing after
    scene rendering.
  Replay overlay: UI draw pass for replay timeline, prediction controls, and
    cause-tree inspection.
  Retained prediction list: Append-only trajectory chunks reused until the
    prediction generation, source bank, palette, or topology changes.
  All-body path: Space-scene future trajectory selected by body identity rather
    than contact-derived causality.
  Overlay state view: Read-only replay publication borrowed for one late pass.
  Render context: Overlay state plus the render-command target and window facts.

Invariants:
  - Replay state reaches the context only through the published overlay view.
  - Published references and sample pointers remain valid for one frame only.
  - Overlay functions must not store references from the context.
  - A stable trajectory publication returns before traversing source records.
  - Legacy scrubber and cause-tree pixels draw only while the Legacy
    development surface owns presentation; ImGui consumes the same values in
    its own exclusive surface.

Related:
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#pragma once

#include "ReplayAuthoring.h"
#include "ReplayOverlayPackets.h"
#include "ReplayPrediction.h"
#include "ReplayPresentation.h"
#include "ReplayScrubber.h"
#include "../../Rendering/RenderInstanceStore.h"

#include <array>
#include <vector>

namespace SkullbonezCore::Rendering
{
class Dx12GeometryOwner;
class Dx12TextureOwner;
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Core
{
class Profiler;
}

namespace SkullbonezCore::Text
{
class TextBatch;
}

namespace SkullbonezCore::UI
{
class UIDrawList;
}

namespace SkullbonezCore::Physics
{
class ColliderStore;
class PhysicsBodyStore;
class PhysicsEngine;
} // namespace SkullbonezCore::Physics

namespace SkullbonezCore::Runtime
{
class EditorTracer;
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
    std::size_t retainedRangeIndex = Rendering::RETAINED_TRAJECTORY_MAX_DRAW_RANGES;
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
    // records with a denser, independently bounded sampling policy. Their
    // cursors must not consume the ordinary child-path cursor.
    std::array<ReplayPredictionDrawRecordCursor, MAX_RECORD_CURSORS> retainedTrailCursors = {};
    std::size_t recordCursorCount = 0;
    std::size_t retainedTrailCursorCount = 0;
    std::size_t retainedMarkerCount = 0;
    std::size_t baselinePoseCount = 0;
    std::size_t ordinaryRibbonCapacityRemaining = 0;
    std::size_t priorityRibbonCapacityRemaining = 0;
    Physics::PhysicsSceneObjectId targetId;
    ReplayFrameIndex revealFrame = 0;
    uint32_t generation = 0;
    uint32_t topologyVersion = 0;
    uint32_t trajectoryBuildTopologyVersion = 0;
    uint64_t trajectoryPublicationVersion = 0;
    std::size_t sampleStride = 1;
    ReplayPathColorMode colorMode = ReplayPathColorMode::LaneFlat;
    bool usingBuildFrames = false;
    bool showAllFuturePaths = false;
    bool saturated = false;
    bool valid = false;

    void Reset() noexcept
    {
        // Hazard: assigning this whole state from {} materializes a
        // hundreds-of-KiB temporary. Nested Debug render frames can then
        // exhaust the process stack before prediction drawing begins.
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
        revealFrame = 0;
        generation = 0;
        topologyVersion = 0;
        trajectoryBuildTopologyVersion = 0;
        trajectoryPublicationVersion = 0;
        sampleStride = 1;
        colorMode = ReplayPathColorMode::LaneFlat;
        usingBuildFrames = false;
        showAllFuturePaths = false;
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

constexpr bool IsReplayPredictionDrawListPublicationStable(
    bool reset,
    uint64_t retainedPublicationVersion,
    ReplayFrameIndex retainedRevealFrame,
    uint64_t incomingPublicationVersion,
    ReplayFrameIndex incomingRevealFrame
) noexcept
{
    return !reset && retainedPublicationVersion == incomingPublicationVersion &&
           retainedRevealFrame == incomingRevealFrame;
}

// An extending publication resumes exactly at its cursor. The point before the
// cursor remains the start of the next segment; no historical command is read
// or emitted again.
constexpr std::size_t ReplayPredictionFirstUnconsumedPoint( std::size_t consumedPointCount ) noexcept
{
    return consumedPointCount > 1u ? consumedPointCount : 1u;
}

constexpr bool ReplayPredictionDrawsAllBodyRecord(
    bool showAllFuturePaths,
    const ReplayTrajectoryRecordKey& key,
    uint16_t activeRootBranch,
    Physics::PhysicsSceneObjectId selectedId
) noexcept
{
    return showAllFuturePaths && key.lane == ReplayTrajectoryLane::FutureRoot &&
           key.branchOrdinal == activeRootBranch && key.bodyId.value != selectedId.value;
}

constexpr bool ReplayPredictionDrawsCausalChildRecord(
    bool showAllFuturePaths,
    const ReplayTrajectoryRecordKey& key,
    uint16_t activeChildBranchBase,
    uint16_t activeChildBranchEnd
) noexcept
{
    return !showAllFuturePaths &&
           ( key.lane == ReplayTrajectoryLane::FutureChildIncoming ||
             key.lane == ReplayTrajectoryLane::FutureChildOutgoing ) &&
           key.branchOrdinal >= activeChildBranchBase && key.branchOrdinal < activeChildBranchEnd;
}

constexpr bool ReplayPredictionUsesAuthoredBodyColor( bool showAllFuturePaths, ReplayTrajectoryLane lane ) noexcept
{
    return showAllFuturePaths && lane == ReplayTrajectoryLane::FutureRoot;
}

struct ReplayPathVisualizerRenderContext
{
    // Lifetime: every reference is a frame-local borrow from the render-tool
    // pass. Prediction scheduling and presentation-cache preparation have
    // already published for this frame, so drawing receives prediction as a
    // read-only borrow and cannot reach worker or reveal-clock authority.
    const ReplayPredictionPresentationView& prediction;
    Core::Profiler* profiler = nullptr;
    const RunReplayPathVisualizerState& pathVisualizer;
    SkullbonezCore::Physics::PhysicsEngine& physics;
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

void RenderReplayScrubberOverlay(
    Text::TextBatch& textBatch,
    UI::UIDrawList& drawList,
    const ReplayOverlayRenderContext& context
);
void RenderReplayInterceptOverlay(
    Text::TextBatch& textBatch,
    UI::UIDrawList& drawList,
    const ReplayOverlayRenderContext& context
);
void RenderReplayTripPlannerOverlay(
    Text::TextBatch& textBatch,
    UI::UIDrawList& drawList,
    const ReplayOverlayRenderContext& context
);
void RenderReplayPorkchopOverlay(
    Text::TextBatch& textBatch,
    UI::UIDrawList& drawList,
    const ReplayOverlayRenderContext& context
);
void RenderReplayCauseTreeOverlay(
    Text::TextBatch& textBatch,
    UI::UIDrawList& drawList,
    const ReplayOverlayRenderContext& context
);
// Appends only newly published/revealed trajectory points to a retained tracer.
// A generation, topology, record replacement, or palette change resets the
// bounded list; an unchanged publication token returns without traversing it.
ReplayPredictionDrawListUpdate UpdateReplayPredictionDrawList(
    const ReplayPredictionPresentationView& prediction,
    const RunReplayPathVisualizerState& pathVisualizer,
    const SceneEntityStore& entities,
    const Physics::ColliderStore& colliderStore,
    EditorTracer& drawList,
    ReplayPredictionDrawListState& state
);
// Emits only each active path's current unsampled endpoint. Completed segments
// stay in the retained append-only list, so the reveal remains continuous
// without rebuilding historical commands.
void AppendReplayPredictionProvisionalTails(
    const ReplayPredictionPresentationView& prediction,
    const RunReplayPathVisualizerState& pathVisualizer,
    const ReplayPredictionDrawListState& state,
    const Physics::ColliderStore& colliderStore,
    EditorTracer& tracer
);
ReplayPathVisualizerRenderResult RenderReplayPathVisualizer( const ReplayPathVisualizerRenderContext& context );
} // namespace SkullbonezCore::Runtime::ReplayOverlay
