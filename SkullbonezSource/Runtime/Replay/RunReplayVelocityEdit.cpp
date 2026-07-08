/*
File: SkullbonezSource/Runtime/Replay/RunReplayVelocityEdit.cpp
Purpose:
  Implements replay velocity-edit picking, dragging, mutation, and overlay drawing.

Mental model:
  Velocity edit is a replay-owned interaction mode. It turns mouse rays into linear or angular
  velocity edits, then marks prediction dirty so the path preview reflects the changed live state.

Glossary:
  Velocity gizmo: Replay overlay that exposes linear axes and angular rings for one body.
  Live advance: Replay scrubber mode that lets edited live physics advance while tools stay active.
  Replay velocity body view: Local value view resolved from PhysicsBodyStore and
    ColliderStore for one replay target; model index is a UI/presentation hint
    after replay identity resolves to a PhysicsBodyHandle.

Invariants:
  - Pointer capture must end whenever the drag exits or the edited target becomes invalid.
  - Edited velocities are clamped before waking or mutating the physics body.
  - Hit testing, drag-start values, and gizmo drawing must read store rows, not
    the post-step GameModel body mirror.
  - Velocity-edit helper functions are file-local to this translation unit.

Related:
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"

#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../UI/UIInput.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;
using namespace SkullbonezCore::Basics::ReplayOverlay;

namespace
{
bool IsReplayToolOwner( WorldInteractionOwner owner )
{
    return owner == WorldInteractionOwner::ReplayScrub || owner == WorldInteractionOwner::ReplayVelocityEdit ||
           owner == WorldInteractionOwner::ReplayPrediction || owner == WorldInteractionOwner::ReplayBranchTarget ||
           owner == WorldInteractionOwner::ReplayCauseTree;
}


Vector3 EditorAxisVector( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 0.0f, 1.0f );
    default:
        return SkullbonezCore::Math::Vector::ZERO_VECTOR;
    }
}


float ReplayVelocityLinearBaseLength( float modelRadius )
{
    return (std::max)( 10.0f, modelRadius + 7.0f );
}


float ReplayVelocityLinearVisualAxisT( float modelRadius, float velocityComponent )
{
    const float sign = velocityComponent < 0.0f ? -1.0f : 1.0f;
    const float t = std::clamp( fabsf( velocityComponent ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
    return sign * ( ReplayVelocityLinearBaseLength( modelRadius ) + t * REPLAY_VELOCITY_EDIT_LINEAR_EXTRA );
}


float ReplayVelocityLinearUnitsPerWorld()
{
    return REPLAY_VELOCITY_EDIT_LINEAR_MAX / REPLAY_VELOCITY_EDIT_LINEAR_EXTRA;
}


float ReplayVelocityAngularBaseRadius( float modelRadius )
{
    return (std::max)( 11.0f, modelRadius + 6.0f );
}


float ReplayVelocityAngularVisualRadius( float modelRadius, float angularComponent )
{
    const float t = std::clamp( fabsf( angularComponent ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
    return ReplayVelocityAngularBaseRadius( modelRadius ) + t * (std::max)( 5.0f, modelRadius * 0.85f );
}


float ReplayVelocityAxisComponent( const Vector3& value, int axis )
{
    if ( axis == 0 )
    {
        return value.x;
    }
    if ( axis == 1 )
    {
        return value.y;
    }
    return value.z;
}


void ReplayVelocitySetAxisComponent( Vector3& value, int axis, float component )
{
    if ( axis == 0 )
    {
        value.x = component;
    }
    else if ( axis == 1 )
    {
        value.y = component;
    }
    else
    {
        value.z = component;
    }
}


Vector3 EditorRotationRingBasisA( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 1.0f, 0.0f );
    case 1:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 2:
        return Vector3( 1.0f, 0.0f, 0.0f );
    default:
        return Vector3( 1.0f, 0.0f, 0.0f );
    }
}


Vector3 EditorRotationRingBasisB( int axis )
{
    switch ( axis )
    {
    case 0:
        return Vector3( 0.0f, 0.0f, 1.0f );
    case 1:
        return Vector3( 1.0f, 0.0f, 0.0f );
    case 2:
        return Vector3( 0.0f, 1.0f, 0.0f );
    default:
        return Vector3( 0.0f, 1.0f, 0.0f );
    }
}


float WrapEditorAngleDelta( float delta )
{
    while ( delta > _PI )
    {
        delta -= 2.0f * _PI;
    }
    while ( delta < -_PI )
    {
        delta += 2.0f * _PI;
    }
    return delta;
}


float DistanceRayToSegmentSquared( const Vector3& rayOrigin,
                                   const Vector3& rayDirection,
                                   const Vector3& segmentA,
                                   const Vector3& segmentB )
{
    const Vector3 segment = segmentB - segmentA;
    const float segmentLenSq = segment * segment;
    if ( segmentLenSq <= TOLERANCE * TOLERANCE )
    {
        const Vector3 toPoint = segmentA - rayOrigin;
        const float rayT = (std::max)( 0.0f, toPoint * rayDirection );
        return VectorMagSquared( rayOrigin + rayDirection * rayT - segmentA );
    }

    const Vector3 w0 = rayOrigin - segmentA;
    const float a = rayDirection * rayDirection;
    const float b = rayDirection * segment;
    const float c = segmentLenSq;
    const float d = rayDirection * w0;
    const float e = segment * w0;
    const float denom = a * c - b * b;

    float rayT = 0.0f;
    float segmentT = 0.0f;
    if ( fabsf( denom ) > 1e-5f )
    {
        rayT = ( b * e - c * d ) / denom;
        segmentT = ( a * e - b * d ) / denom;
    }

    if ( rayT < 0.0f )
    {
        rayT = 0.0f;
        segmentT = std::clamp( e / c, 0.0f, 1.0f );
    }
    else if ( segmentT < 0.0f )
    {
        segmentT = 0.0f;
        rayT = (std::max)( 0.0f, -d / a );
    }
    else if ( segmentT > 1.0f )
    {
        segmentT = 1.0f;
        rayT = (std::max)( 0.0f, ( b - d ) / a );
    }

    const Vector3 rayPoint = rayOrigin + rayDirection * rayT;
    const Vector3 segmentPoint = segmentA + segment * segmentT;
    return VectorMagSquared( rayPoint - segmentPoint );
}

struct ReplayVelocityBodyView
{
    PhysicsBodyHandle body;
    int modelIndex = -1;
    Vector3 position = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Quaternion orientation = IDENTITY_QUATERNION;
    Vector3 linearVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    Vector3 angularVelocity = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    const CollisionShape* shape = nullptr;
    float radius = 1.0f;
    bool fixed = false;
};


static bool TryResolveReplayVelocityBodyView( const ReplayRuntime& replayRuntime,
                                              const PhysicsBodyStore& bodyStore,
                                              const ColliderStore& colliderStore,
                                              ReplayVelocityBodyView& outView )
{
    outView = ReplayVelocityBodyView{};
    const PhysicsBodyHandle bodyHandle = replayRuntime.ResolveVelocityEditBodyHandle( bodyStore );
    const int modelIndex = bodyStore.ModelIndexForHandle( bodyHandle );
    if ( modelIndex < 0 || modelIndex >= bodyStore.Count() )
    {
        return false;
    }
    const PhysicsBodyRecord* body = bodyStore.RecordForHandle( bodyHandle );
    const ColliderRecord* collider =
        body ? colliderStore.RecordForHandle( colliderStore.HandleForBodyHandle( body->handle ) ) : nullptr;
    if ( !body || !collider )
    {
        return false;
    }

    // Invariant: replay velocity edit resolves identity to a body handle before
    // it reads pose, velocity, or shape rows. modelIndex remains only for UI
    // gesture metadata and collider pairing while replay/editor identity moves
    // away from transient GameModel order.
    outView.body = bodyHandle;
    outView.modelIndex = modelIndex;
    outView.position = body->position;
    outView.orientation = body->orientation;
    outView.linearVelocity = body->linearVelocity;
    outView.angularVelocity = body->angularVelocity;
    outView.shape = &collider->shape;
    outView.radius = (std::max)( 1.0f, (std::max)( body->boundingRadius, collider->boundingRadius ) );
    outView.fixed = body->isFixed;
    return outView.shape != nullptr;
}


int HitReplayVelocityLinearAxis( const ReplayVelocityBodyView& body,
                                 const Vector3& rayOrigin,
                                 const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float radius = body.radius;
    const float threshold = (std::max)( 1.15f, radius * 0.12f );
    const float thresholdSq = threshold * threshold;
    int bestAxis = -1;
    float bestDistanceSq = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( body.linearVelocity, axis );
        const Vector3 endpoint = origin + axisVector * ReplayVelocityLinearVisualAxisT( radius, component );
        const float distanceSq = DistanceRayToSegmentSquared( rayOrigin, rayDirection, origin, endpoint );
        if ( distanceSq <= thresholdSq && distanceSq < bestDistanceSq )
        {
            bestDistanceSq = distanceSq;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


int HitReplayVelocityAngularAxis( const ReplayVelocityBodyView& body,
                                  const Vector3& rayOrigin,
                                  const Vector3& rayDirection )
{
    if ( body.fixed )
    {
        return -1;
    }

    const Vector3 origin = body.position;
    const float modelRadius = body.radius;
    int bestAxis = -1;
    float bestDiff = FLT_MAX;
    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 normal = EditorAxisVector( axis );
        const float denom = normal * rayDirection;
        if ( fabsf( denom ) <= 1e-4f )
        {
            continue;
        }

        const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
        if ( rayT < 0.0f )
        {
            continue;
        }

        const float ringRadius =
            ReplayVelocityAngularVisualRadius( modelRadius, ReplayVelocityAxisComponent( body.angularVelocity, axis ) );
        const float threshold = (std::max)( 1.10f, ringRadius * 0.08f );
        const Vector3 hitPoint = rayOrigin + rayDirection * rayT;
        const Vector3 radial = hitPoint - origin;
        const float radialDistance = VectorMag( radial - normal * ( radial * normal ) );
        const float diff = fabsf( radialDistance - ringRadius );
        if ( diff <= threshold && diff < bestDiff )
        {
            bestDiff = diff;
            bestAxis = axis;
        }
    }
    return bestAxis;
}


bool TryReplayVelocityAxisRayParameter( const ReplayVelocityBodyView& body,
                                        int axis,
                                        const Vector3& rayOrigin,
                                        const Vector3& rayDirection,
                                        float& outAxisT )
{
    if ( axis < 0 || axis > 2 || body.modelIndex < 0 )
    {
        return false;
    }

    const Vector3 axisOrigin = body.position;
    const Vector3 axisVector = EditorAxisVector( axis );
    const Vector3 w = axisOrigin - rayOrigin;
    const float b = axisVector * rayDirection;
    const float d = axisVector * w;
    const float e = rayDirection * w;
    const float denom = 1.0f - b * b;
    if ( fabsf( denom ) <= 1e-5f )
    {
        return false;
    }

    outAxisT = ( b * e - d ) / denom;
    return true;
}


bool TryReplayVelocityAngularRayAngle( const ReplayVelocityBodyView& body,
                                       int axis,
                                       const Vector3& rayOrigin,
                                       const Vector3& rayDirection,
                                       float& outAngle )
{
    if ( axis < 0 || axis > 2 || body.modelIndex < 0 )
    {
        return false;
    }

    const Vector3 origin = body.position;
    const Vector3 normal = EditorAxisVector( axis );
    const float denom = normal * rayDirection;
    if ( fabsf( denom ) <= 1e-4f )
    {
        return false;
    }

    const float rayT = ( normal * ( origin - rayOrigin ) ) / denom;
    if ( rayT < 0.0f )
    {
        return false;
    }

    Vector3 radial = rayOrigin + rayDirection * rayT - origin;
    radial -= normal * ( radial * normal );
    const float radialLenSq = radial * radial;
    if ( radialLenSq <= TOLERANCE * TOLERANCE )
    {
        return false;
    }
    radial = radial * ( 1.0f / sqrtf( radialLenSq ) );

    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    outAngle = atan2f( radial * basisB, radial * basisA );
    return true;
}
} // namespace


bool Run::TickReplayVelocityEditInput( HWND hwnd, bool uiBlocksMouse )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    ReplayInteractionController replayInteraction;
    const ReplayVelocityEditInputFrame inputFrame =
        replayInteraction.BeginVelocityEditInputFrame( m_replayRuntime, Input::IsLeftMouseDown() );
    const bool leftDown = inputFrame.leftDown;
    const bool leftPressed = inputFrame.leftPressed;
    const bool leftReleased = inputFrame.leftReleased;

    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled ||
         !SceneState().isScenePhysics || RuntimeWindowScreenWidth( m_systems, m_config ) <= 0 ||
         RuntimeWindowScreenHeight( m_systems, m_config ) <= 0 )
    {
        const ReplayVelocityEditResetResult resetResult =
            replayInteraction.ResetVelocityEditInteraction( m_replayRuntime, true );
        if ( resetResult.endDragGesture )
        {
            EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
        }
        if ( resetResult.releaseMouseCapture )
        {
            UI::InputControl::EndMouseCapture();
        }
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        if ( m_replayRuntime.VelocityEdit().dragging && ( leftReleased || !leftDown ) )
        {
            const ReplayVelocityEditResetResult resetResult = replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            if ( resetResult.endDragGesture )
            {
                EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            }
            if ( resetResult.releaseMouseCapture )
            {
                UI::InputControl::EndMouseCapture();
            }
        }
        return m_replayRuntime.VelocityEdit().dragging;
    }

    // Why: velocity edit resolves replay identity through store-owned handles.
    // Run owns the cold repair edge before the resolver reads body/collider rows.
    const bool velocityStoresReady = m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology();
    PhysicsEngine& velocityPhysics = m_cGameModelCollection.GetPhysicsEngine();
    const auto tryResolveVelocityBody = [&]( ReplayVelocityBodyView& outBody )
    {
        return velocityStoresReady && TryResolveReplayVelocityBodyView( m_replayRuntime,
                                                                        velocityPhysics.BodyStore(),
                                                                        velocityPhysics.Colliders(),
                                                                        outBody );
    };

    const auto applyReplayVelocityEditDrag = [&]( const Vector3& dragRayOrigin, const Vector3& dragRayDirection )
    {
        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        ReplayVelocityBodyView body;
        if ( !tryResolveVelocityBody( body ) || m_replayRuntime.VelocityEdit().activeAxis < 0 )
        {
            const ReplayVelocityEditResetResult resetResult = replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            if ( resetResult.endDragGesture )
            {
                EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            }
            if ( resetResult.releaseMouseCapture )
            {
                UI::InputControl::EndMouseCapture();
            }
            return;
        }

        Vector3 linearVelocity = m_replayRuntime.VelocityEdit().dragStartLinearVelocity;
        Vector3 angularVelocity = m_replayRuntime.VelocityEdit().dragStartAngularVelocity;
        if ( m_replayRuntime.VelocityEdit().draggingAngular )
        {
            float currentAngle = 0.0f;
            if ( !TryReplayVelocityAngularRayAngle( body,
                                                    m_replayRuntime.VelocityEdit().activeAxis,
                                                    dragRayOrigin,
                                                    dragRayDirection,
                                                    currentAngle ) )
            {
                return;
            }
            const float angleDelta =
                WrapEditorAngleDelta( currentAngle - m_replayRuntime.VelocityEdit().dragStartAngle );
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartAngularVelocity,
                                             m_replayRuntime.VelocityEdit().activeAxis ) +
                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
            ReplayVelocitySetAxisComponent(
                angularVelocity,
                m_replayRuntime.VelocityEdit().activeAxis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
        }
        else
        {
            float axisT = 0.0f;
            if ( !TryReplayVelocityAxisRayParameter( body,
                                                     m_replayRuntime.VelocityEdit().activeAxis,
                                                     dragRayOrigin,
                                                     dragRayDirection,
                                                     axisT ) )
            {
                return;
            }
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartLinearVelocity,
                                             m_replayRuntime.VelocityEdit().activeAxis ) +
                ( axisT - m_replayRuntime.VelocityEdit().dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
            ReplayVelocitySetAxisComponent(
                linearVelocity,
                m_replayRuntime.VelocityEdit().activeAxis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
        }

        replayInteraction.ApplyVelocityEditToBody( ReplayVelocityEditApplyContext{
            m_replayRuntime,
            m_cGameModelCollection,
            body.body,
            linearVelocity,
            angularVelocity,
            REPLAY_VELOCITY_EDIT_LINEAR_MAX,
            REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
            m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS } );
    };

    if ( m_replayRuntime.VelocityEdit().dragging )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            applyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            const ReplayVelocityEditResetResult resetResult = replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            if ( resetResult.endDragGesture )
            {
                EndReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag );
            }
            if ( resetResult.releaseMouseCapture )
            {
                UI::InputControl::EndMouseCapture();
            }
        }
        return true;
    }

    ReplayVelocityBodyView hotBody;
    const bool hasHotBody = !uiBlocksMouse && tryResolveVelocityBody( hotBody );
    const int hotAngularAxis = hasHotBody ? HitReplayVelocityAngularAxis( hotBody, rayOrigin, rayDirection ) : -1;
    const int hotLinearAxis =
        ( !hasHotBody || hotAngularAxis >= 0 ) ? -1 : HitReplayVelocityLinearAxis( hotBody, rayOrigin, rayDirection );
    replayInteraction.SetVelocityEditHoverAxes( m_replayRuntime, hotLinearAxis, hotAngularAxis );

    const auto armBaselineComparisonForDrag = [&]()
    {
        RunReplayPredictionState& prediction = m_replayRuntime.Prediction();
        if ( prediction.build.complete && prediction.frames.size() >= 2 && m_replayRuntime.PathVisualizer().hasTarget )
        {
            // Why: the old future must be retained before the first drag tick
            // dirties prediction. The visualizer owns the actual capture so it
            // can reuse the same rest-pose and replay-reserve rules as drawing.
            prediction.baseline.valid = false;
            prediction.baseline.comparisonActive = true;
            prediction.baseline.divergenceValid = false;
            prediction.baseline.divergenceUnits = 0.0f;
        }
    };

    if ( !uiBlocksMouse && leftPressed )
    {
        const POINT mouse = Input::GetClientMouseCoordinates();
        ReplayVelocityBodyView body;
        if ( tryResolveVelocityBody( body ) && !body.fixed )
        {
            if ( m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( body,
                                                       m_replayRuntime.VelocityEdit().hotAngularAxis,
                                                       rayOrigin,
                                                       rayDirection,
                                                       startAngle ) )
                {
                    EnterInteractiveSceneRun();
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                            InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        EnterReplayInspectionCamera();
                    }
                    else
                    {
                        ExitReplayInspectionCamera();
                    }
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            body.modelIndex,
                                            m_replayRuntime.VelocityEdit().hotAngularAxis,
                                            true );
                    ReplayVelocityEditDragStart dragStart;
                    dragStart.modelIndex = body.modelIndex;
                    dragStart.axis = m_replayRuntime.VelocityEdit().hotAngularAxis;
                    dragStart.angular = true;
                    dragStart.angle = startAngle;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    replayInteraction.BeginVelocityEditDrag( m_replayRuntime, dragStart );
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
            else if ( m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 )
            {
                float axisT = 0.0f;
                if ( TryReplayVelocityAxisRayParameter( body,
                                                        m_replayRuntime.VelocityEdit().hotLinearAxis,
                                                        rayOrigin,
                                                        rayDirection,
                                                        axisT ) )
                {
                    EnterInteractiveSceneRun();
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayScrub,
                                                                            InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        EnterReplayInspectionCamera();
                    }
                    else
                    {
                        ExitReplayInspectionCamera();
                    }
                    BeginReplayToolGesture( RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                            WorldInteractionOwner::ReplayVelocityEdit,
                                            RuntimePointerButton::Left,
                                            mouse.x,
                                            mouse.y,
                                            body.modelIndex,
                                            m_replayRuntime.VelocityEdit().hotLinearAxis,
                                            false );
                    ReplayVelocityEditDragStart dragStart;
                    dragStart.modelIndex = body.modelIndex;
                    dragStart.axis = m_replayRuntime.VelocityEdit().hotLinearAxis;
                    dragStart.angular = false;
                    dragStart.axisT = axisT;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    replayInteraction.BeginVelocityEditDrag( m_replayRuntime, dragStart );
                    if ( !m_replayRuntime.VelocityEdit().mouseCaptured )
                    {
                        UI::InputControl::BeginMouseCapture( hwnd );
                        m_replayRuntime.VelocityEdit().mouseCaptured = true;
                    }
                    return true;
                }
            }
        }

        // Concept: velocity edit owns replay body targeting. A click on the
        // body itself should select the replay path target for the velocity
        // gizmo, not fall through to normal editor/world selection and clear it.
        (void)TryPickReplayPathTargetFromMouse( false, false );
        if ( m_replayRuntime.PathVisualizer().hasTarget )
        {
            EnterInteractiveSceneRun();
            if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && m_replayRuntime.ShouldUseInspectionCamera() )
            {
                EnterReplayInspectionCamera();
            }
            SetWorldInteractionOwnerAfterInteractionTransition( WorldInteractionOwner::ReplayVelocityEdit,
                                                                InteractionExitReason::EnterReplay );
            replayInteraction.SelectVelocityEditTarget(
                m_replayRuntime,
                m_timers.simulationTimer.GetTotalTime() + REPLAY_SCRUBBER_VISIBLE_SECONDS );
        }
        return true;
    }

    return m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0;
}


void Run::RenderReplayVelocityEditOverlay( RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !m_replayRuntime.VelocityEdit().enabled || m_runtimeTools.Editor().editorModeEnabled )
    {
        return;
    }

    ReplayVelocityBodyView body;
    if ( !m_cGameModelCollection.RepairPhysicsBodyAndColliderTopology() )
    {
        return;
    }
    PhysicsEngine& velocityPhysics = m_cGameModelCollection.GetPhysicsEngine();
    if ( !TryResolveReplayVelocityBodyView( m_replayRuntime,
                                            velocityPhysics.BodyStore(),
                                            velocityPhysics.Colliders(),
                                            body ) ||
         body.fixed || !body.shape )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo( body.position,
                                   body.orientation,
                                   *body.shape,
                                   body.radius,
                                   body.linearVelocity,
                                   body.angularVelocity,
                                   m_replayRuntime.VelocityEdit().hotLinearAxis,
                                   m_replayRuntime.VelocityEdit().hotAngularAxis,
                                   m_replayRuntime.VelocityEdit().activeAxis,
                                   m_replayRuntime.VelocityEdit().draggingAngular );
}
