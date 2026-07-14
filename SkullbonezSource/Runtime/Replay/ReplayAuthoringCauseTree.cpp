/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp
Purpose:
  Contains replay cause-tree window input and focus behavior.

Summary:
  The cause tree is an explanatory replay UI over retained solver contacts and
  predicted movement. It owns window placement and drag/resize state, derives
  row hover from a disposable shared surface, and asks ReplayRuntime to resolve
  body positions for camera focus.

Glossary:
  Cause tree: Contact, solver-row, and predicted-motion graph explaining replay
    body influence.
  Focus row: Cause-tree row selected for replay inspection camera targeting.

Invariants:
  - Window drag and resize gestures must release pointer capture on mouse up.
  - A higher-priority UI block suppresses cause-window actions and draw hover.
  - Focus changes hold live replay advance so selected historical rows remain visible.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayPredictionDrawing.cpp
  - SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h
*/
#include "ReplayAuthoring.h"
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "../CameraCollection.h"
#include "../InputController.h"
#include "../InputRouter.h"
#include "../../Core/Profiler.h"
#include "../../Core/FatalError.h"
#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Runtime::ReplayOverlay;

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


void ReplayAuthoring::BeginCauseTreeInputFrame() noexcept
{
    m_causeTree.pointerBlocked = true;
}


void ReplayAuthoring::EnsureCauseTreeWindowPlacement( int screenWidth, int screenHeight ) noexcept
{
    EnsureReplayCauseWindowPlacement( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::SetCauseTreePointer( int mouseX, int mouseY, bool blocked ) noexcept
{
    m_causeTree.mouseX = mouseX;
    m_causeTree.mouseY = mouseY;
    m_causeTree.pointerBlocked = blocked;
}


void ReplayAuthoring::MoveCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.x = mouseX - m_causeTree.dragOffsetX;
    m_causeTree.y = mouseY - m_causeTree.dragOffsetY;
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ResizeCauseTreeWindow( int mouseX, int mouseY, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.width = m_causeTree.resizeStartWidth + ( mouseX - m_causeTree.resizeStartMouseX );
    m_causeTree.height = m_causeTree.resizeStartHeight + ( mouseY - m_causeTree.resizeStartMouseY );
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::ScrollCauseTreeWindow( float delta, int screenWidth, int screenHeight ) noexcept
{
    m_causeTree.scrollY += delta;
    ClampReplayCauseWindow( m_causeTree, screenWidth, screenHeight );
}


void ReplayAuthoring::BeginCauseTreeResize( int mouseX, int mouseY ) noexcept
{
    m_causeTree.resizeStartMouseX = mouseX;
    m_causeTree.resizeStartMouseY = mouseY;
    m_causeTree.resizeStartWidth = m_causeTree.width;
    m_causeTree.resizeStartHeight = m_causeTree.height;
}


void ReplayAuthoring::BeginCauseTreeMove( int mouseX, int mouseY ) noexcept
{
    m_causeTree.dragOffsetX = mouseX - m_causeTree.x;
    m_causeTree.dragOffsetY = mouseY - m_causeTree.y;
}


bool ReplayAuthoring::TryGetCauseTreeRow( int rowIndex, RunReplayCauseTreeRow& outRow ) const noexcept
{
    if ( rowIndex < 0 || rowIndex >= static_cast<int>( m_causeTree.rows.size() ) )
    {
        return false;
    }
    outRow = m_causeTree.rows[static_cast<std::size_t>( rowIndex )];
    return true;
}


void ReplayAuthoring::SetCauseTreeFocus( int rowIndex, ReplayBodyId focusedId ) noexcept
{
    m_causeTree.selectedRow = rowIndex;
    m_causeTree.focusedId = focusedId;
}


bool ReplayRuntime::TickCauseTreeInput( bool uiBlocksMouse,
                                        int wheelDelta,
                                        InputRouter& inputRouter,
                                        RuntimeInteractionController& interaction,
                                        const PhysicsBodyStore& bodyStore,
                                        const ColliderStore& colliderStore,
                                        std::span<const Rendering::RenderInstancePresentationRecord> presentation,
                                        Environment::CameraCollection* cameras,
                                        Geometry::Terrain* terrain,
                                        RunCameraState& camera,
                                        RunMousePickupState& mousePickup,
                                        RunCameraMode normalizedCurrentMode,
                                        RunCameraMode normalizedRestoreMode,
                                        bool attachedFollow,
                                        bool directorGrabbed,
                                        bool editorModeEnabled,
                                        int screenWidth,
                                        int screenHeight,
                                        bool& outEnterInteractive )
{
    ReplayRuntime& m_replayRuntime = *this;
    InputRouter& m_inputRouter = inputRouter;
    RuntimeInteractionController& m_interaction = interaction;
    const auto enterInspectionCamera = [&]()
    { EnterInspectionCamera( cameras, camera, normalizedCurrentMode, m_interaction, m_inputRouter, mousePickup ); };
    const auto exitInspectionCamera = [&]()
    {
        ExitInspectionCamera( cameras,
                              terrain,
                              camera,
                              normalizedRestoreMode,
                              attachedFollow,
                              directorGrabbed,
                              m_interaction,
                              m_inputRouter );
    };
    PROFILE_SCOPED( "Frame/Replay/CauseTree/Input" );
    // Concept: Cause-tree input owns the explanatory replay window state while
    // delegating body/sample interpretation to ReplayRuntime queries.
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    m_authoring.BeginCauseTreeInputFrame();

    const auto activateReplayCameraForCauseRow = [&]( const RunReplayCauseTreeRow& row, int rowIndex )
    {
        PROFILE_SCOPED( "Frame/Replay/CauseTree/Focus" );
        Vector3 targetPosition = row.point;
        float targetRadius = 2.0f;
        RunReplayCameraFocusKind focusKind = RunReplayCameraFocusKind::Body;
        // Lifetime: replay focus borrows already-prepared physics store views
        // for one UI action. Topology repair belongs to the runtime/frame
        // boundary, not this read-only cause-tree lookup.
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

        outEnterInteractive = true;
        const bool hadReplayCameraFocus = m_replayRuntime.HasCameraFocus();
        if ( !m_replayRuntime.ScrubberView().liveAdvanceHeld )
        {
            if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayCauseTreeToolOwner( m_interaction.Owner() ) )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayScrub,
                                                                 InteractionExitReason::EnterReplay );
            }
            m_replayRuntime.SetCameraPauseOwnership( true );
        }
        else if ( !hadReplayCameraFocus )
        {
            m_replayRuntime.SetCameraPauseOwnership( false );
        }
        enterInspectionCamera();

        ReplayCameraFocusRequest focus;
        focus.focusKind = focusKind;
        focus.focusedId = row.id;
        focus.counterpartId = row.counterpartId;
        focus.focusedRow = rowIndex;
        focus.focusRowKind = row.kind;
        focus.focusModelRow = row.modelRow;
        focus.focusCounterpartModelRow = row.counterpartModelRow;
        focus.focusContactIndex = row.contactIndex;
        focus.focusSolverRowIndex = row.solverRowIndex;
        focus.focusFeatureId = row.featureId;
        focus.focusTerrain = row.terrain;
        focus.targetPoint = targetPosition;
        focus.targetNormal = ReplayCauseTreeNormalizeOr( row.normal, Vector3( 0.0f, 1.0f, 0.0f ) );
        focus.impulseVector = row.impulse;
        focus.targetRadius = targetRadius;
        m_replayRuntime.ApplyCameraFocus( focus );
        m_authoring.SetCauseTreeFocus( rowIndex, row.id );

        if ( cameras )
        {
            const Vector3 eye = cameras->GetRenderCameraTranslation();
            Vector3 direction = ReplayCauseTreeNormalizeOr( eye - targetPosition, Vector3( 0.45f, 0.28f, 0.85f ) );
            direction = ReplayCauseTreeNormalizeOr( direction, Vector3( 0.45f, 0.28f, 0.85f ) );
            const float distance = (std::max)( 12.0f, targetRadius * 5.5f );
            const Vector3 newEye = targetPosition + direction * distance + Vector3( 0.0f, targetRadius * 0.35f, 0.0f );
            cameras->TweenPrimaryToPose( newEye, targetPosition, cameras->GetRenderCameraUp() );
            cameras->ResetRelativity();
        }
        InputController::ResetMouseLook( camera );
        m_inputRouter.RequestCursorVisible( true );
    };

    const int screenW = screenWidth;
    const int screenH = screenHeight;
    const auto causeTreeDragMode = [&]()
    {
        const RuntimeInteractionGesture& gesture = m_interaction.Gesture();
        return gesture.kind == RuntimeInteractionGestureKind::ReplayCauseTreeDrag ? gesture.axis : -1;
    };
    const auto endCauseTreeDragIfReleased = [&]()
    {
        if ( leftReleased && causeTreeDragMode() >= 0 )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
    };
    if ( editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    if ( !m_replayRuntime.BuildCauseTreeRows( presentation, bodyStore ) )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    m_authoring.EnsureCauseTreeWindowPlacement( screenW, screenH );
    const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
    if ( !runtimePointer.hasClientPosition )
    {
        endCauseTreeDragIfReleased();
        return false;
    }
    const POINT mouse{ runtimePointer.clientX, runtimePointer.clientY };
    m_authoring.SetCauseTreePointer( mouse.x, mouse.y, uiBlocksMouse );
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( m_replayRuntime.CauseTree(), surface );
    surface.ResolvePointer( mouse.x, mouse.y, uiBlocksMouse );
    const auto isHotControl = [&]( ReplayCauseWindowControl control )
    { return surface.hasHotControl && surface.hotControl == ReplayCauseWindowControlId( control ); };
    const RuntimeUiControl* contentControl =
        surface.Find( ReplayCauseWindowControlId( ReplayCauseWindowControl::Content ) );
    if ( !contentControl )
    {
        SB_FATAL( "ReplayCauseWindowSurface", "Content control is missing from the cause-window surface." );
    }
    const UI::UIRect content = contentControl->drawRect;

    if ( causeTreeDragMode() == 0 )
    {
        m_authoring.MoveCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
        return true;
    }

    if ( causeTreeDragMode() == 1 )
    {
        m_authoring.ResizeCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
        return true;
    }

    if ( uiBlocksMouse || !surface.consumesPointer )
    {
        return false;
    }

    if ( wheelDelta != 0 )
    {
        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                         WorldInteractionOwner::ReplayCauseTree,
                                                         InteractionExitReason::EnterReplay );
        const float wheelRows = static_cast<float>( wheelDelta ) / 120.0f;
        m_authoring.ScrollCauseTreeWindow( -wheelRows * REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f, screenW, screenH );
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Resize ) )
    {
        if ( !m_replayRuntime.BeginToolGesture( m_interaction,
                                                RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                                WorldInteractionOwner::ReplayCauseTree,
                                                RuntimePointerButton::Left,
                                                mouse.x,
                                                mouse.y,
                                                PhysicsBodyHandle{},
                                                1 ) )
        {
            return false;
        }
        m_authoring.BeginCauseTreeResize( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Title ) )
    {
        if ( !m_replayRuntime.BeginToolGesture( m_interaction,
                                                RuntimeInteractionGestureKind::ReplayCauseTreeDrag,
                                                WorldInteractionOwner::ReplayCauseTree,
                                                RuntimePointerButton::Left,
                                                mouse.x,
                                                mouse.y,
                                                PhysicsBodyHandle{},
                                                0 ) )
        {
            return false;
        }
        m_authoring.BeginCauseTreeMove( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( isHotControl( ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + m_replayRuntime.CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        RunReplayCauseTreeRow selectedRow;
        if ( m_authoring.TryGetCauseTreeRow( rowIndex, selectedRow ) )
        {
            if ( leftPressed )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayCauseTree,
                                                                 InteractionExitReason::EnterReplay );
                activateReplayCameraForCauseRow( selectedRow, rowIndex );
            }
        }
        else if ( leftPressed )
        {
            m_replayRuntime.ClearCauseTreeFocusSelection();
            exitInspectionCamera();
        }
    }

    return true;
}
