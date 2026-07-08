/*
File: SkullbonezSource/Runtime/Replay/RunReplayCauseTreeTools.cpp
Purpose:
  Contains replay cause-tree window input and focus behavior.

Mental model:
  The cause tree is an explanatory replay UI over retained solver contacts and
  predicted movement. It owns window drag/resize/row hover state and asks
  ReplayRuntime to resolve body positions for camera focus.

Glossary:
  Cause tree: Contact, solver-row, and predicted-motion graph explaining replay
    body influence.
  Focus row: Cause-tree row selected for replay inspection camera targeting.

Invariants:
  - Window drag and resize gestures must release pointer capture on mouse up.
  - Focus changes hold live replay advance so selected historical rows remain visible.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "../RunInternal.h"
#include "../InputController.h"
#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../UI/UIInput.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;

namespace
{
bool IsReplayCauseTreeToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


Vector3 ReplayCauseTreeNormalizeOr( Vector3 value, const Vector3& fallback )
{
    const float magSq = VectorMagSquared( value );
    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }
    value /= sqrtf( magSq );
    return value;
}
} // namespace


bool Run::TickReplayCauseTreeInput( HWND hwnd, bool uiBlocksMouse, int wheelDelta )
{
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );
    // Concept: Cause-tree input owns the explanatory replay window state while
    // delegating body/sample interpretation to ReplayRuntime queries.
    const bool leftDown = Input::IsLeftMouseDown();
    const ReplayRuntime::PointerButtonEdges inputEdges = m_replayRuntime.BeginCauseTreeInputFrame( leftDown );
    const bool leftPressed = inputEdges.leftPressed;
    const bool leftReleased = inputEdges.leftReleased;

    const auto activateReplayCameraForCauseRow = [&]( const RunReplayCauseTreeRow& row, int rowIndex )
    {
        PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
        Vector3 targetPosition = row.point;
        float targetRadius = 2.0f;
        RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::Body;
        // Lifetime: replay focus borrows already-prepared physics store views
        // for one UI action. Topology repair belongs to the runtime/frame
        // boundary, not this read-only cause-tree lookup.
        const auto& colliderStore = m_cGameModelCollection.GetPhysicsEngine().Colliders();
        const auto& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
        switch ( row.kind )
        {
        case RunReplayCauseTreeRowKind::Body:
        {
            const bool bodyResolved = m_replayRuntime.ResolveCauseTreeBodyPosition( row.id,
                                                                                    bodyStore,
                                                                                    colliderStore,
                                                                                    targetPosition,
                                                                                    &targetRadius );
            if ( !bodyResolved )
            {
                return;
            }
            focusKind = RunReplayCameraFocusKind::Body;
            break;
        }
        case RunReplayCauseTreeRowKind::Manifold:
            m_replayRuntime.ResolveCauseTreeBodyPosition( row.id,
                                                          bodyStore,
                                                          colliderStore,
                                                          targetPosition,
                                                          &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.55f, 2.0f );
            focusKind = RunReplayCameraFocusKind::Manifold;
            break;
        case RunReplayCauseTreeRowKind::SolverRow:
            m_replayRuntime.ResolveCauseTreeBodyPosition( row.id,
                                                          bodyStore,
                                                          colliderStore,
                                                          targetPosition,
                                                          &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
            focusKind = RunReplayCameraFocusKind::SolverRow;
            break;
        case RunReplayCauseTreeRowKind::PredictionContact:
        case RunReplayCauseTreeRowKind::PredictionMotion:
            m_replayRuntime.ResolveCauseTreeBodyPosition( row.id,
                                                          bodyStore,
                                                          colliderStore,
                                                          targetPosition,
                                                          &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
            focusKind = row.kind == RunReplayCauseTreeRowKind::PredictionContact
                            ? RunReplayCameraFocusKind::PredictionContact
                            : RunReplayCameraFocusKind::PredictionMotion;
            break;
        default:
            return;
        }

        if ( VectorMagSquared( targetPosition ) <= TOLERANCE * TOLERANCE &&
             row.kind != RunReplayCauseTreeRowKind::Body )
        {
            return;
        }

        EnterInteractiveSceneRun();
        const bool hadReplayCameraFocus = m_replayRuntime.Camera().focusKind != RunReplayCameraFocusKind::None;
        if ( !m_replayRuntime.Scrubber().liveAdvanceHeld )
        {
            if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayCauseTreeToolOwner( m_interaction.Owner() ) )
            {
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                    InteractionExitReason::EnterReplay );
            }
            m_replayRuntime.Camera().ownsSimulationPause = true;
        }
        else if ( !hadReplayCameraFocus )
        {
            m_replayRuntime.Camera().ownsSimulationPause = false;
        }
        EnterReplayInspectionCamera();

        m_replayRuntime.Camera().focusKind = focusKind;
        m_replayRuntime.Camera().focusedId = row.id;
        m_replayRuntime.Camera().counterpartId = row.counterpartId;
        m_replayRuntime.Camera().focusedRow = rowIndex;
        m_replayRuntime.Camera().focusRowKind = row.kind;
        m_replayRuntime.Camera().focusModelIndex = row.modelIndex;
        m_replayRuntime.Camera().focusCounterpartModelIndex = row.counterpartModelIndex;
        m_replayRuntime.Camera().focusContactIndex = row.contactIndex;
        m_replayRuntime.Camera().focusSolverRowIndex = row.solverRowIndex;
        m_replayRuntime.Camera().focusFeatureId = row.featureId;
        m_replayRuntime.Camera().focusTerrain = row.terrain;
        m_replayRuntime.Camera().targetPoint = targetPosition;
        m_replayRuntime.Camera().targetNormal = ReplayCauseTreeNormalizeOr( row.normal, Vector3( 0.0f, 1.0f, 0.0f ) );
        m_replayRuntime.Camera().impulseVector = row.impulse;
        m_replayRuntime.Camera().targetRadius = targetRadius;
        m_replayRuntime.CauseTree().focusedId = row.id;
        m_replayRuntime.CauseTree().selectedRow = rowIndex;

        if ( m_systems.cameras )
        {
            const Vector3 eye = m_systems.cameras->GetRenderCameraTranslation();
            Vector3 direction = ReplayCauseTreeNormalizeOr( eye - targetPosition, Vector3( 0.45f, 0.28f, 0.85f ) );
            direction = ReplayCauseTreeNormalizeOr( direction, Vector3( 0.45f, 0.28f, 0.85f ) );
            const float distance = (std::max)( 12.0f, targetRadius * 5.5f );
            const Vector3 newEye = targetPosition + direction * distance + Vector3( 0.0f, targetRadius * 0.35f, 0.0f );
            m_systems.cameras->TweenPrimaryToPose( newEye, targetPosition, m_systems.cameras->GetRenderCameraUp() );
            m_systems.cameras->ResetRelativity();
        }
        InputController::ResetMouseLook( m_camera );
        Input::SetSystemCursorVisible( true );
    };

    const int screenW = RuntimeWindowScreenWidth( m_systems, m_config );
    const int screenH = RuntimeWindowScreenHeight( m_systems, m_config );
    const auto endCauseTreeDragIfReleased = [&]()
    {
        if ( leftReleased &&
             ( m_replayRuntime.CauseTree().draggingWindow || m_replayRuntime.CauseTree().resizingWindow ) )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().draggingWindow = false;
            m_replayRuntime.CauseTree().resizingWindow = false;
        }
    };
    if ( m_runtimeTools.Editor().editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    const auto& bodyStore = m_cGameModelCollection.GetPhysicsEngine().BodyStore();
    if ( !m_replayRuntime.BuildCauseTreeRows( m_cGameModelCollection.RenderPresentationRecords(), bodyStore ) )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    EnsureReplayCauseWindowPlacement( m_replayRuntime.CauseTree(), screenW, screenH );
    const POINT mouse = Input::GetClientMouseCoordinates();
    const UI::UIRect panel = ReplayCauseWindowRect( m_replayRuntime.CauseTree() );
    const UI::UIRect title = ReplayCauseWindowTitleRect( m_replayRuntime.CauseTree() );
    const UI::UIRect content = ReplayCauseWindowContentRect( m_replayRuntime.CauseTree() );
    const UI::UIRect resize = ReplayCauseWindowResizeRect( m_replayRuntime.CauseTree() );

    if ( m_replayRuntime.CauseTree().draggingWindow )
    {
        m_replayRuntime.CauseTree().x = mouse.x - m_replayRuntime.CauseTree().dragOffsetX;
        m_replayRuntime.CauseTree().y = mouse.y - m_replayRuntime.CauseTree().dragOffsetY;
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        if ( leftReleased )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().draggingWindow = false;
        }
        return true;
    }

    if ( m_replayRuntime.CauseTree().resizingWindow )
    {
        m_replayRuntime.CauseTree().width =
            m_replayRuntime.CauseTree().resizeStartWidth + ( mouse.x - m_replayRuntime.CauseTree().resizeStartMouseX );
        m_replayRuntime.CauseTree().height =
            m_replayRuntime.CauseTree().resizeStartHeight + ( mouse.y - m_replayRuntime.CauseTree().resizeStartMouseY );
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        if ( leftReleased )
        {
            UI::InputControl::EndMouseCapture();
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
            m_replayRuntime.CauseTree().resizingWindow = false;
        }
        return true;
    }

    const bool insidePanel = panel.Contains( mouse.x, mouse.y );
    if ( uiBlocksMouse || !insidePanel )
    {
        return false;
    }

    if ( wheelDelta != 0 )
    {
        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayCauseTree,
                                                            InteractionExitReason::EnterReplay );
        const float wheelRows = static_cast<float>( wheelDelta ) / 120.0f;
        m_replayRuntime.CauseTree().scrollY -= wheelRows * REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f;
        ClampReplayCauseWindow( m_replayRuntime.CauseTree(), screenW, screenH );
        return true;
    }

    if ( leftPressed && resize.Contains( mouse.x, mouse.y ) )
    {
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                WorldInteractionOwner::ReplayCauseTree,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y,
                                -1,
                                1 );
        m_replayRuntime.CauseTree().resizingWindow = true;
        m_replayRuntime.CauseTree().resizeStartMouseX = mouse.x;
        m_replayRuntime.CauseTree().resizeStartMouseY = mouse.y;
        m_replayRuntime.CauseTree().resizeStartWidth = m_replayRuntime.CauseTree().width;
        m_replayRuntime.CauseTree().resizeStartHeight = m_replayRuntime.CauseTree().height;
        UI::InputControl::BeginMouseCapture( hwnd );
        return true;
    }

    if ( leftPressed && title.Contains( mouse.x, mouse.y ) )
    {
        BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                WorldInteractionOwner::ReplayCauseTree,
                                RuntimePointerButton::Left,
                                mouse.x,
                                mouse.y,
                                -1,
                                0 );
        m_replayRuntime.CauseTree().draggingWindow = true;
        m_replayRuntime.CauseTree().dragOffsetX = mouse.x - m_replayRuntime.CauseTree().x;
        m_replayRuntime.CauseTree().dragOffsetY = mouse.y - m_replayRuntime.CauseTree().y;
        UI::InputControl::BeginMouseCapture( hwnd );
        return true;
    }

    if ( content.Contains( mouse.x, mouse.y ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + m_replayRuntime.CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        if ( rowIndex >= 0 && rowIndex < static_cast<int>( m_replayRuntime.CauseTree().rows.size() ) )
        {
            m_replayRuntime.CauseTree().hoveredRow = rowIndex;
            if ( leftPressed )
            {
                SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayCauseTree,
                                                                    InteractionExitReason::EnterReplay );
                activateReplayCameraForCauseRow( m_replayRuntime.CauseTree().rows[static_cast<std::size_t>( rowIndex )],
                                                 rowIndex );
            }
        }
        else if ( leftPressed )
        {
            m_replayRuntime.ClearCauseTreeFocusSelection();
            ExitReplayInspectionCamera();
        }
    }

    return true;
}
