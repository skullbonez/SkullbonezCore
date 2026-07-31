/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoring.h
Purpose:
  Owns replay velocity-edit, causal-authoring, and branch-provenance state.

Summary:
  ReplayAuthoring retains operator edits and cause-tree selection. Held velocity
  samples publish a newest preview value; release publishes the single request
  that refreshes authoritative prediction.

Glossary:
  Velocity preview request: Fixed-size target and delta-v command that replaces
    its predecessor without scheduling simulation.

Invariants:
  - Cause rows retain Physics::PhysicsSceneObjectId as identity and dense rows only as hints.
  - Authoring receives prediction only as a read-only frame-local publication;
    the composition root consumes queued mutation requests in frame order.
  - Held velocity samples never set the prediction-refresh bit; the release
    edge sets it once after at least one accepted mutation.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayAuthoringPackets.h"
#include "ReplayRecorder.h"
#include "../../Core/Common.h"
#include "../../Physics/PhysicsHandles.h"

#include <vector>
#include <span>

namespace SkullbonezCore
{
namespace Core
{
class Profiler;
}
namespace Environment
{
class CameraCollection;
}
namespace Geometry
{
class Terrain;
}
namespace Rendering
{
struct RenderInstancePresentationRecord;
}
namespace Physics
{
class PhysicsEngine;
class PhysicsBodyStore;
class ColliderStore;
} // namespace Physics
namespace Runtime
{
class InputRouter;
class ReplayPresentation;
class ReplayScrubber;
class EditorTracer;
class RuntimeInteractionController;
class SceneEntityStore;
struct ReplayPathPickInput;
struct ReplayKeyboardVelocityEditInput;
struct ReplayKeyboardVelocityEditResult;
struct RunReplayCameraState;
struct RunReplayPathVisualizerState;
struct CameraControlState;
struct RunMousePickupState;
enum class RunCameraMode;
enum class ReplayInspectionCameraAction : uint8_t;
struct RuntimeInteractionGesture;
struct ReplayAuthoringPredictionRequest
{
    Physics::PhysicsSceneObjectId velocityPreviewTargetId;
    Math::Vector::Vector3 velocityPreviewDelta = Math::Vector::ZERO_VECTOR;
    bool enablePrediction = false;
    bool refreshPrediction = false;
    bool clearPredictionCache = false;
    bool prepareVelocityMutationBaseline = false;
    bool updateVelocityPreview = false;
    bool finishVelocityPreview = false;
};

struct ReplayVelocityEditDragStart
{
    Physics::PhysicsSceneObjectId targetId;
    float axisT = 0.0f;
    float angle = 0.0f;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
};

struct ReplayAuthoringMemoryStats
{
    uint64_t ownerBytes = 0;
    uint64_t causeRowCapacityBytes = 0;
    std::size_t causeRowCount = 0;
};

class ReplayAuthoring
{
  public:
    explicit ReplayAuthoring( Core::Profiler* profiler = nullptr ) : m_profiler( profiler )
    {
    }

    Core::Profiler* ProfilerBorrow() const noexcept
    {
        return m_profiler;
    }

    const RunReplayCauseTreeState& CauseTree() const noexcept
    {
        return m_causeTree;
    }

    ReplayAuthoringMemoryStats CollectMemoryStats() const noexcept
    {
        ReplayAuthoringMemoryStats stats;
        stats.ownerBytes = sizeof( m_causeTree );
        stats.causeRowCapacityBytes = static_cast<uint64_t>( m_causeTree.rows.capacity() ) * sizeof( RunReplayCauseTreeRow );
        stats.causeRowCount = m_causeTree.rows.size();
        return stats;
    }

    // Clears generated explanation rows and their selection while preserving
    // the operator's window placement.
    void ResetCauseTreeRows() noexcept
    {
        m_causeTree.rows.clear();
        m_causeTree.selectedRow = -1;
        m_causeTree.scrollY = 0.0f;
    }

    void ClearCauseTreeFocus() noexcept
    {
        m_causeTree.focusedId = Physics::PhysicsSceneObjectId {};
        m_causeTree.selectedRow = -1;
    }

    void ReserveCauseTreeRows( std::size_t capacity )
    {
        m_causeTree.rows.reserve( capacity );
    }

    // Starts a bounded, allocation-free rebuild. Append returns false instead
    // of growing beyond the startup reserve.
    void BeginCauseTreeRowBuild() noexcept
    {
        m_causeTree.rows.clear();
    }
    bool CauseTreeRowCapacityCovers( std::size_t count ) const noexcept
    {
        return count <= m_causeTree.rows.capacity();
    }
    bool AppendCauseTreeRow( const RunReplayCauseTreeRow& row )
    {

        if ( m_causeTree.rows.size() >= m_causeTree.rows.capacity() )
        {
            return false;
        }

        m_causeTree.rows.push_back( row );
        return true;
    }
    void FailCauseTreeRowBuild() noexcept
    {
        m_causeTree.rows.clear();
        m_causeTree.selectedRow = -1;
    }
    void SetCauseTreeSelectedRow( int rowIndex ) noexcept
    {
        m_causeTree.selectedRow = rowIndex;
    }

    // Cause-window commands retain layout mutation inside the authoring owner;
    // input and rendering consume only the published const state.
    void BeginCauseTreeInputFrame() noexcept;
    void EnsureCauseTreeWindowPlacement( int screenWidth, int screenHeight ) noexcept;
    void SetCauseTreePointer( int mouseX, int mouseY, bool blocked ) noexcept;
    void MoveCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept;
    void ResizeCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept;
    void ScrollCauseTreeWindow( float delta, int screenWidth, int screenHeight ) noexcept;
    void BeginCauseTreeResize( int mouseX, int mouseY ) noexcept;
    void BeginCauseTreeMove( int mouseX, int mouseY ) noexcept;
    bool TryGetCauseTreeRow( int rowIndex, RunReplayCauseTreeRow& outRow ) const noexcept;
    void SetCauseTreeFocus( int rowIndex, Physics::PhysicsSceneObjectId focusedId ) noexcept;
    bool TickCauseTreeInput( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner, InputRouter& inputRouter,
                             RuntimeInteractionController& interaction, bool rowsReady, bool uiBlocksMouse, int wheelDelta,
                             bool editorModeEnabled, int screenWidth, int screenHeight, int& outFocusRow,
                             bool& outExitInspectionCamera );

    // Unwinds a stale drag when velocity editing cannot run this frame. The
    // following gizmo and target-pick phases are invoked only when this succeeds.
    bool PrepareVelocityEditInput( bool editorModeEnabled, bool scenePhysicsEnabled, int screenWidth, int screenHeight,
                                   InputRouter& inputRouter, RuntimeInteractionController& interaction );
    bool TickVelocityEditInput( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner,
                                const ReplayPathPickInput& pointerRay, bool uiBlocksMouse, double now,
                                InputRouter& inputRouter, RuntimeInteractionController& interaction,
                                Physics::PhysicsEngine& physics, std::size_t entityCount, bool& outEnterInteractive,
                                bool& outPathPickRequested, ReplayInspectionCameraAction& outInspectionCameraAction );
    bool TryPickVelocityEditTarget( ReplayPresentation& presentationOwner, ReplayScrubber& scrubberOwner,
                                    const ReplaySolverFrameSample* currentSolverSample, const SceneEntityStore& entities,
                                    std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                    Physics::PhysicsEngine& physics, const ReplayPathPickInput& pointerRay,
                                    RuntimeInteractionController& interaction, double now, bool& outEnterInteractive,
                                    ReplayInspectionCameraAction& outInspectionCameraAction );
    ReplayKeyboardVelocityEditResult ApplyKeyboardVelocityEdit( const ReplayKeyboardVelocityEditInput& input,
                                                                ReplayScrubber& scrubberOwner,
                                                                const ReplayPresentation& presentationOwner );
    const RunReplayVelocityEditState& VelocityEdit() const noexcept
    {
        return m_velocityEdit;
    }
    const ReplayBranchInfo& Branch() const noexcept
    {
        return m_branch;
    }

    // Starts a new live lineage after restoring a retained solver sample. The
    // returned parent id is the value that the timeline records in its branch
    // event; callers never receive mutable provenance state.
    uint32_t BeginRestoredBranch( const ReplayBranchInfo& sourceBranch, ReplayFrameIndex sourceFrame,
                                  uint64_t sourceSolverHash ) noexcept
    {
        const uint32_t currentBranchId = m_branch.branchId;
        const uint32_t parentBranchId = sourceBranch.branchId != 0 ? sourceBranch.branchId
                                                                   : ( currentBranchId != 0 ? currentBranchId : 1u );

        ReplayBranchInfo restoredBranch;
        restoredBranch.branchId = ( currentBranchId > parentBranchId ? currentBranchId : parentBranchId ) + 1u;
        restoredBranch.parentBranchId = parentBranchId;
        restoredBranch.startFrame = 0;
        restoredBranch.sourceFrame = sourceFrame;
        restoredBranch.sourceSolverHash = sourceSolverHash;
        m_branch = restoredBranch;
        return parentBranchId;
    }

    void ResetBranch() noexcept
    {
        m_branch = ReplayBranchInfo {};
    }

    bool SetVelocityEditEnabled( bool enabled ) noexcept
    {

        if ( m_velocityEdit.enabled == enabled )
        {
            return false;
        }

        m_velocityEdit.enabled = enabled;
        m_velocityEdit.hotLinearAxis = -1;
        m_velocityEdit.hotAngularAxis = -1;

        if ( enabled )
        {
            QueuePredictionRefresh( true );
        }
        else
        {
            (void)FinishVelocityEditDrag();
        }

        return true;
    }

    void ClearVelocityEditInputState() noexcept
    {
        (void)FinishVelocityEditDrag();
        m_velocityEdit.keyboardAltWasDown = false;
        m_velocityEdit.hotLinearAxis = -1;
        m_velocityEdit.hotAngularAxis = -1;
    }

    void ResetVelocityEdit() noexcept
    {
        m_velocityEdit = RunReplayVelocityEditState {};
    }

    void ObserveVelocityEditAltKey( bool isDown ) noexcept
    {
        m_velocityEdit.keyboardAltWasDown = isDown;
    }

    void SetVelocityEditHoverAxes( int linearAxis, int angularAxis ) noexcept
    {
        m_velocityEdit.hotLinearAxis = linearAxis;
        m_velocityEdit.hotAngularAxis = angularAxis;
    }

    void BeginVelocityEditDrag( const ReplayVelocityEditDragStart& start ) noexcept
    {
        m_velocityEdit.dragTargetId = start.targetId;
        m_velocityEdit.dragChanged = false;
        m_velocityEdit.dragStartAxisT = start.axisT;
        m_velocityEdit.dragStartAngle = start.angle;
        m_velocityEdit.dragStartLinearVelocity = start.linearVelocity;
        m_velocityEdit.dragStartAngularVelocity = start.angularVelocity;
    }

    // Invariant: held pointer samples update only this newest-state value.
    // Prediction generation remains untouched until FinishVelocityEditDrag()
    // observes the release edge.
    void QueueVelocityEditPreview( Physics::PhysicsSceneObjectId targetId,
                                   const Math::Vector::Vector3& velocityDelta ) noexcept
    {
        m_velocityEdit.dragChanged = true;
        m_pendingPrediction.velocityPreviewTargetId = targetId;
        m_pendingPrediction.velocityPreviewDelta = velocityDelta;
        m_pendingPrediction.updateVelocityPreview = true;
    }

    bool FinishVelocityEditDrag() noexcept
    {

        if ( !m_velocityEdit.dragChanged )
        {
            return false;
        }

        m_velocityEdit.dragChanged = false;
        m_pendingPrediction.finishVelocityPreview = true;
        QueuePredictionRefresh();
        return true;
    }

    // Appends the authoring-owned velocity gizmo from value-selected replay
    // identity. Presentation supplies the target but cannot mutate edit state.
    void AppendVelocityEditOverlay( Physics::PhysicsSceneObjectId targetId, Physics::ModelRowHint targetModelRow,
                                    Physics::PhysicsEngine& physics, bool editorModeEnabled,
                                    const RuntimeInteractionGesture& gesture, EditorTracer& tracer ) const;

    // Concept: authoring publishes a value command instead of holding a
    // prediction pointer or callback. Multiple edits before consumption fold
    // into one newest-state refresh request.
    void QueuePredictionRefresh( bool enablePrediction = false ) noexcept
    {
        m_pendingPrediction.enablePrediction |= enablePrediction;
        m_pendingPrediction.refreshPrediction = true;
    }

    void QueuePredictionCacheReset() noexcept
    {
        m_pendingPrediction.clearPredictionCache = true;
        m_pendingPrediction.refreshPrediction = true;
    }

    void QueueVelocityMutationBaselinePreparation() noexcept
    {
        m_pendingPrediction.prepareVelocityMutationBaseline = true;
    }

    ReplayAuthoringPredictionRequest TakePredictionRequest() noexcept
    {
        const ReplayAuthoringPredictionRequest request = m_pendingPrediction;
        m_pendingPrediction = ReplayAuthoringPredictionRequest {};
        return request;
    }

  private:

    // Lifetime: startup-bound diagnostics borrow; null when profiling is disabled.
    Core::Profiler* m_profiler;
    RunReplayCauseTreeState m_causeTree;
    RunReplayVelocityEditState m_velocityEdit;
    ReplayBranchInfo m_branch;
    ReplayAuthoringPredictionRequest m_pendingPrediction;
};

} // namespace Runtime
} // namespace SkullbonezCore
