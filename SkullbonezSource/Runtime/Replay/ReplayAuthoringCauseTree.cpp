/*
File: SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTree.cpp
Purpose:
  Contains replay cause-tree window input and focus behavior.

Summary:
  The cause tree is an explanatory replay UI over retained solver contacts and
  predicted movement. It owns window placement and drag/resize state, derives
  row hover from a disposable shared surface, and resolves camera focus from
  explicit prediction, solver, and live-store views.

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

float ReplayCauseTreeColliderRadius( const ColliderRecord& collider )
{
    return (std::max)( collider.boundingRadius, 1.0f );
}

float ReplayCauseTreeColliderRadiusForModelRow( const ColliderStore& colliderStore, int modelRow )
{
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForModelIndex( modelRow );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayCauseTreeColliderRadius( *collider );
    }

    const auto colliders = colliderStore.Records();
    if ( modelRow < 0 || modelRow >= static_cast<int>( colliders.size() ) )
    {
        return 1.0f;
    }
    return ReplayCauseTreeColliderRadius( colliders[static_cast<std::size_t>( modelRow )] );
}

float ReplayCauseTreeColliderRadiusForBody( const ColliderStore& colliderStore,
                                            const PhysicsBodyRecord& body,
                                            int fallbackModelRow )
{
    const PhysicsColliderHandle colliderHandle = colliderStore.HandleForBodyHandle( body.handle );
    if ( const ColliderRecord* collider = colliderStore.RecordForHandle( colliderHandle ) )
    {
        return ReplayCauseTreeColliderRadius( *collider );
    }
    return ReplayCauseTreeColliderRadiusForModelRow( colliderStore, fallbackModelRow );
}

// Concept: focus pose lookup is authoring interpretation over immutable replay
// publications plus live physics rows. Dense model rows remain radius hints;
// ReplayBodyId is the identity check in every source.
// Invariant: this helper stores no view and never repairs topology while input
// is active. The frame boundary prepared paired body/collider stores first.
bool ResolveReplayCauseTreeBodyPosition( ReplayBodyId id,
                                         bool predictionEnabled,
                                         ReplayBodyId predictionTargetId,
                                         ReplayBodyId pathTargetId,
                                         std::span<const RunReplayPredictionFrame> activePredictionFrames,
                                         const ReplaySolverFrameSample* solverSample,
                                         const PhysicsBodyStore& bodyStore,
                                         const ColliderStore& colliderStore,
                                         Vector3& outPosition,
                                         float* outRadius )
{
    if ( id.value == 0 )
    {
        return false;
    }

    if ( outRadius )
    {
        *outRadius = 1.0f;
    }

    const auto publishSampleRadius = [&]( ModelRowHint modelRow )
    {
        if ( !outRadius )
        {
            return;
        }
        const PhysicsBodyHandle liveBody = bodyStore.HandleForReplayBodyId( id.value, modelRow.value );
        const PhysicsBodyRecord* liveBodyRecord = bodyStore.RecordForHandle( liveBody );
        *outRadius = liveBodyRecord
                         ? ReplayCauseTreeColliderRadiusForBody( colliderStore, *liveBodyRecord, modelRow.value )
                         : ReplayCauseTreeColliderRadiusForModelRow( colliderStore, modelRow.value );
    };

    if ( predictionEnabled && !activePredictionFrames.empty() && predictionTargetId.value == pathTargetId.value )
    {
        for ( const RunReplayPredictionBodySample& body : activePredictionFrames.front().bodies )
        {
            if ( body.id.value == id.value )
            {
                outPosition = body.position;
                publishSampleRadius( body.modelRow );
                return true;
            }
        }
    }

    if ( solverSample )
    {
        for ( const ReplaySolverBodySample& body : solverSample->bodies )
        {
            if ( body.id.value == id.value )
            {
                outPosition = body.position;
                publishSampleRadius( body.modelRow );
                return true;
            }
        }
    }

    for ( const PhysicsBodyRecord& body : bodyStore.Records() )
    {
        if ( body.replayBodyId == id.value )
        {
            outPosition = body.position;
            if ( outRadius )
            {
                const int fallbackModelRow = bodyStore.ModelIndexForHandle( body.handle );
                *outRadius = ReplayCauseTreeColliderRadiusForBody( colliderStore, body, fallbackModelRow );
            }
            return true;
        }
    }
    return false;
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


bool ReplayAuthoring::TickCauseTreeInput( ReplayPresentation& presentationOwner,
                                          ReplayScrubber& scrubberOwner,
                                          const RunReplayPredictionState& prediction,
                                          std::span<const RunReplayPredictionFrame> activePredictionFrames,
                                          const ReplaySolverFrameSample* currentSolverSample,
                                          bool uiBlocksMouse,
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
    InputRouter& m_inputRouter = inputRouter;
    RuntimeInteractionController& m_interaction = interaction;
    const auto enterInspectionCamera = [&]()
    {
        EnterReplayInspectionCamera( presentationOwner,
                                     cameras,
                                     camera,
                                     normalizedCurrentMode,
                                     m_interaction,
                                     m_inputRouter,
                                     mousePickup );
    };
    const auto exitInspectionCamera = [&]()
    {
        ExitReplayInspectionCamera( presentationOwner,
                                    *this,
                                    cameras,
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
    // body focus resolves from explicit prediction, solver, and live-store
    // views captured for this input turn.
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const bool leftPressed = pointer.leftPressed;
    const bool leftReleased = pointer.leftReleased;
    BeginCauseTreeInputFrame();
    const ReplayBodyId pathTargetId = presentationOwner.PathVisualizer().targetId;
    const auto resolveCauseTreeBody = [&]( ReplayBodyId id, Vector3& outPosition, float* outRadius )
    {
        return ResolveReplayCauseTreeBodyPosition( id,
                                                   prediction.enabled,
                                                   prediction.simulation.targetId,
                                                   pathTargetId,
                                                   activePredictionFrames,
                                                   currentSolverSample,
                                                   bodyStore,
                                                   colliderStore,
                                                   outPosition,
                                                   outRadius );
    };

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
            const bool bodyResolved = resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            if ( !bodyResolved )
            {
                return;
            }
            focusKind = RunReplayCameraFocusKind::Body;
            break;
        }
        case RunReplayCauseTreeRowKind::Manifold:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.55f, 2.0f );
            focusKind = RunReplayCameraFocusKind::Manifold;
            break;
        case RunReplayCauseTreeRowKind::SolverRow:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
            targetPosition = row.point;
            targetRadius = (std::max)( targetRadius * 0.45f, 1.5f );
            focusKind = RunReplayCameraFocusKind::SolverRow;
            break;
        case RunReplayCauseTreeRowKind::PredictionContact:
        case RunReplayCauseTreeRowKind::PredictionMotion:
            resolveCauseTreeBody( row.id, targetPosition, &targetRadius );
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
        const bool hadReplayCameraFocus = presentationOwner.CameraView().focusKind != RunReplayCameraFocusKind::None;
        if ( !scrubberOwner.View().liveAdvanceHeld )
        {
            if ( scrubberOwner.SetLiveAdvanceHeld( true ) && !IsReplayCauseTreeToolOwner( m_interaction.Owner() ) )
            {
                interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                 WorldInteractionOwner::ReplayScrub,
                                                                 InteractionExitReason::EnterReplay );
            }
            presentationOwner.SetCameraPauseOwnership( true );
        }
        else if ( !hadReplayCameraFocus )
        {
            presentationOwner.SetCameraPauseOwnership( false );
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
        presentationOwner.ApplyCameraFocus( focus );
        SetCauseTreeFocus( rowIndex, row.id );

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
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
    };
    if ( editorModeEnabled || screenW <= 0 || screenH <= 0 )
    {
        endCauseTreeDragIfReleased();
        return false;
    }

    int focusedCameraRow = -1;
    if ( !BuildCauseTreeRows( presentationOwner.PathVisualizer(),
                              prediction,
                              activePredictionFrames,
                              currentSolverSample,
                              presentation,
                              bodyStore,
                              presentationOwner.CameraView(),
                              focusedCameraRow ) )
    {
        endCauseTreeDragIfReleased();
        return false;
    }
    if ( focusedCameraRow >= 0 )
    {
        presentationOwner.SetCameraFocusedRow( focusedCameraRow );
    }

    EnsureCauseTreeWindowPlacement( screenW, screenH );
    const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
    if ( !runtimePointer.hasClientPosition )
    {
        endCauseTreeDragIfReleased();
        return false;
    }
    const POINT mouse{ runtimePointer.clientX, runtimePointer.clientY };
    SetCauseTreePointer( mouse.x, mouse.y, uiBlocksMouse );
    ReplayCauseWindowSurface surface;
    BuildReplayCauseWindowSurface( CauseTree(), surface );
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
        MoveCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
        }
        return true;
    }

    if ( causeTreeDragMode() == 1 )
    {
        ResizeCauseTreeWindow( mouse.x, mouse.y, screenW, screenH );
        if ( leftReleased )
        {
            m_inputRouter.ReleaseNativeCapture();
            m_interaction.EndGestureIfKind( RuntimeInteractionGestureKind::ReplayCauseTreeDrag );
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
        ScrollCauseTreeWindow( -wheelRows * REPLAY_CAUSE_WINDOW_ROW_HEIGHT * 3.0f, screenW, screenH );
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Resize ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 1;
        if ( !m_interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                   WorldInteractionOwner::ReplayCauseTree,
                                                   gesture ) )
        {
            return false;
        }
        BeginCauseTreeResize( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( leftPressed && isHotControl( ReplayCauseWindowControl::Title ) )
    {
        RuntimeInteractionGesture gesture;
        gesture.kind = RuntimeInteractionGestureKind::ReplayCauseTreeDrag;
        gesture.button = RuntimePointerButton::Left;
        gesture.startX = mouse.x;
        gesture.startY = mouse.y;
        gesture.axis = 0;
        if ( !m_interaction.BeginOwnedToolGesture( RuntimeWorkspace::Replay,
                                                   WorldInteractionOwner::ReplayCauseTree,
                                                   gesture ) )
        {
            return false;
        }
        BeginCauseTreeMove( mouse.x, mouse.y );
        m_inputRouter.RequestNativeCapture();
        return true;
    }

    if ( isHotControl( ReplayCauseWindowControl::Content ) )
    {
        const float localY = static_cast<float>( mouse.y ) - content.y + CauseTree().scrollY;
        const int rowIndex = static_cast<int>( floorf( localY / REPLAY_CAUSE_WINDOW_ROW_HEIGHT ) );
        RunReplayCauseTreeRow selectedRow;
        if ( TryGetCauseTreeRow( rowIndex, selectedRow ) )
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
            const bool ownedSimulationPause = presentationOwner.ClearCameraFocus();
            ClearCauseTreeFocus();
            const ReplayScrubberView scrubber = scrubberOwner.View();
            if ( ownedSimulationPause && scrubber.liveAdvanceHeld && !scrubber.historicalSamplePaused )
            {
                scrubberOwner.SetLiveAdvanceHeld( false );
            }
            presentationOwner.ClearPathState();
            ResetCauseTreeRows();
            QueuePredictionCacheReset();
            exitInspectionCamera();
        }
    }

    return true;
}
