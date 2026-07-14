/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoring.h
Purpose:
  Owns replay velocity-edit, causal-authoring, and branch-provenance state.

Summary:
  ReplayAuthoring retains operator edits and cause-tree selection, then publishes
  value requests when those edits require prediction to refresh.

Glossary:
  Cause row: One replay explanation row.

Invariants:
  - Cause rows retain ReplayBodyId as identity and dense rows only as hints.
  - Authoring never reaches into prediction state; the composition root consumes
    queued value requests in frame order.

Related:
  - ReplayRuntime.h
  - ReplayRecorder.h
*/
#pragma once

#include "ReplayIdentity.h"
#include "ReplayRecorder.h"
#include "../../Core/Common.h"
#include "../../Physics/PhysicsHandles.h"

#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
// Invariant: replay input clamping and editor visualization share these scales
// so the velocity gizmo cannot advertise values that authoring rejects.
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_MAX = 140.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_ANGULAR_MAX = 5.0f;
inline constexpr float REPLAY_VELOCITY_EDIT_LINEAR_EXTRA = 36.0f;

struct RunReplayCauseTreeRow
{
    RunReplayCauseTreeRowKind kind = RunReplayCauseTreeRowKind::Body;
    ReplayBodyId id;
    ReplayBodyId parentId;
    ReplayBodyId counterpartId;
    ReplayFrameIndex firstFrame = 0;
    int depth = 0;
    Physics::ModelRowHint modelRow;
    Physics::ModelRowHint counterpartModelRow;
    int contactIndex = -1;
    int solverRowIndex = -1;
    int pipelineIndex = -1;
    int featureId = 0;
    int manifoldPointCount = 0;
    float penetration = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float warmStartImpulse = 0.0f;
    float bias = 0.0f;
    float effectiveMass = 0.0f;
    float frictionLimit = 0.0f;
    Math::Vector::Vector3 point = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 normal = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    Math::Vector::Vector3 impulse = Math::Vector::ZERO_VECTOR;
    bool prediction = false;
    bool terrain = false;
    bool warmStarted = false;
    char name[64] = {};
    char detail[160] = {};
};

struct RunReplayCauseTreeState
{
    // Runtime allocation policy: replay cause rows are rebuilt during input and
    // render, so the vector reserves its full replay/physics budget at startup
    // and builders fail closed instead of growing on a frame.
    std::vector<RunReplayCauseTreeRow> rows;
    int selectedRow = -1;
    ReplayBodyId focusedId;
    bool hasWindowPlacement = false;
    int x = 0;
    int y = 0;
    int width = 380;
    int height = 420;
    float scrollY = 0.0f;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    int resizeStartMouseX = 0;
    int resizeStartMouseY = 0;
    int resizeStartWidth = 0;
    int resizeStartHeight = 0;
    int mouseX = 0;
    int mouseY = 0;
    bool pointerBlocked = true; // Frame input says a higher-priority UI owns this pointer.
};

struct RunReplayVelocityEditState
{
    bool enabled = false;
    bool keyboardAltWasDown = false;
    int hotLinearAxis = -1;
    int hotAngularAxis = -1;
    float dragStartAxisT = 0.0f;
    float dragStartAngle = 0.0f;
    Math::Vector::Vector3 dragStartLinearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 dragStartAngularVelocity = Math::Vector::ZERO_VECTOR;
};

struct ReplayAuthoringPredictionRequest
{
    bool enablePrediction = false;
    bool refreshPrediction = false;
};

struct ReplayVelocityEditDragStart
{
    float axisT = 0.0f;
    float angle = 0.0f;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
};

class ReplayAuthoring
{
  public:
    RunReplayCauseTreeState& CauseTree() noexcept
    {
        return m_causeTree;
    }
    const RunReplayCauseTreeState& CauseTree() const noexcept
    {
        return m_causeTree;
    }
    RunReplayVelocityEditState& VelocityEdit() noexcept
    {
        return m_velocityEdit;
    }
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
    uint32_t BeginRestoredBranch( const ReplayBranchInfo& sourceBranch,
                                  ReplayFrameIndex sourceFrame,
                                  uint64_t sourceSolverHash ) noexcept
    {
        const uint32_t currentBranchId = m_branch.branchId;
        const uint32_t parentBranchId =
            sourceBranch.branchId != 0 ? sourceBranch.branchId : ( currentBranchId != 0 ? currentBranchId : 1u );

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
        m_branch = ReplayBranchInfo{};
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
        return true;
    }

    void ClearVelocityEditInputState() noexcept
    {
        m_velocityEdit.keyboardAltWasDown = false;
        m_velocityEdit.hotLinearAxis = -1;
        m_velocityEdit.hotAngularAxis = -1;
    }

    void SetVelocityEditHoverAxes( int linearAxis, int angularAxis ) noexcept
    {
        m_velocityEdit.hotLinearAxis = linearAxis;
        m_velocityEdit.hotAngularAxis = angularAxis;
    }

    void BeginVelocityEditDrag( const ReplayVelocityEditDragStart& start ) noexcept
    {
        m_velocityEdit.dragStartAxisT = start.axisT;
        m_velocityEdit.dragStartAngle = start.angle;
        m_velocityEdit.dragStartLinearVelocity = start.linearVelocity;
        m_velocityEdit.dragStartAngularVelocity = start.angularVelocity;
    }

    // Concept: authoring publishes a value command instead of holding a
    // prediction pointer or callback. Multiple edits before consumption fold
    // into one newest-state refresh request.
    void QueuePredictionRefresh( bool enablePrediction = false ) noexcept
    {
        m_pendingPrediction.enablePrediction |= enablePrediction;
        m_pendingPrediction.refreshPrediction = true;
    }

    ReplayAuthoringPredictionRequest TakePredictionRequest() noexcept
    {
        const ReplayAuthoringPredictionRequest request = m_pendingPrediction;
        m_pendingPrediction = ReplayAuthoringPredictionRequest{};
        return request;
    }

  private:
    RunReplayCauseTreeState m_causeTree;
    RunReplayVelocityEditState m_velocityEdit;
    ReplayBranchInfo m_branch;
    ReplayAuthoringPredictionRequest m_pendingPrediction;
};

} // namespace Runtime
} // namespace SkullbonezCore
