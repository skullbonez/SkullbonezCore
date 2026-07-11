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
#include "ReplayRuntime.h"
#include "../../Assets/AssetKeys.h"
#include "../Editor/EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../InputRouter.h"
#include "../Scene/SceneEntityStore.h"
#include "../../Core/Profiler.h"

#include "ReplayInteractionController.h"
#include "ReplayOverlayLayout.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngineStoreQueries.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::ReplayOverlay;
using SkullbonezCore::Math::Vector::Vector3;

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
    ModelRowHint modelRow;
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
    if ( !bodyHandle.IsValid() )
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
    outView.modelRow.value = modelIndex;
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
    if ( axis < 0 || axis > 2 || body.modelRow.value < 0 )
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
    if ( axis < 0 || axis > 2 || body.modelRow.value < 0 )
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


bool ReplayRuntime::TickVelocityEditInput( bool uiBlocksMouse,
                                           const PathPickInput& pointerRay,
                                           InputRouter& inputRouter,
                                           RuntimeInteractionController& interaction,
                                           PhysicsEngine& velocityPhysics,
                                           const SceneEntityStore& entities,
                                           const std::vector<Rendering::RenderInstancePresentationRecord>& presentation,
                                           Environment::CameraCollection* cameras,
                                           Geometry::Terrain* terrain,
                                           RunCameraState& camera,
                                           RunMousePickupState& mousePickup,
                                           RunCameraMode normalizedCurrentMode,
                                           RunCameraMode normalizedRestoreMode,
                                           bool attachedFollow,
                                           bool directorGrabbed,
                                           bool editorModeEnabled,
                                           bool scenePhysicsEnabled,
                                           int screenWidth,
                                           int screenHeight,
                                           double now,
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
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Input" );
    ReplayInteractionController replayInteraction;
    const RuntimeMouseEdges& pointer = m_inputRouter.UiSnapshot().mouse;
    const ReplayVelocityEditInputFrame inputFrame =
        replayInteraction.BeginVelocityEditInputFrame( pointer.leftDown, pointer.leftPressed, pointer.leftReleased );
    const bool leftDown = inputFrame.leftDown;
    const bool leftPressed = inputFrame.leftPressed;
    const bool leftReleased = inputFrame.leftReleased;
    const auto velocityDragActive = [&]()
    { return m_interaction.Gesture().kind == RuntimeInteractionGestureKind::ReplayVelocityDrag; };

    if ( !m_replayRuntime.VelocityEdit().enabled || editorModeEnabled || !scenePhysicsEnabled || screenWidth <= 0 ||
         screenHeight <= 0 )
    {
        const bool endDragGesture = velocityDragActive();
        replayInteraction.ResetVelocityEditInteraction( m_replayRuntime, true );
        if ( endDragGesture )
        {
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_inputRouter.ReleaseNativeCapture();
        }
        return false;
    }

    const Vector3& rayOrigin = pointerRay.rayOrigin;
    const Vector3& rayDirection = pointerRay.rayDirection;
    if ( !pointerRay.hasWorldRay )
    {
        if ( velocityDragActive() && ( leftReleased || !leftDown ) )
        {
            replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_inputRouter.ReleaseNativeCapture();
        }
        return velocityDragActive();
    }

    // Invariant: the frame boundary prepares paired physics rows before replay
    // input. This handler reads those explicit owners and never repairs legacy
    // model topology from inside an interaction hot path.
    const PhysicsBodyStore& velocityBodies = PhysicsEngineStoreQueries::BodyStore( velocityPhysics );
    const ColliderStore& velocityColliders = PhysicsEngineStoreQueries::Colliders( velocityPhysics );
    const bool velocityStoresReady =
        velocityBodies.Count() == velocityColliders.Count() && velocityBodies.Count() == entities.Count();
    const auto tryResolveVelocityBody = [&]( ReplayVelocityBodyView& outBody )
    {
        return velocityStoresReady &&
               TryResolveReplayVelocityBodyView( m_replayRuntime, velocityBodies, velocityColliders, outBody );
    };

    const auto applyReplayVelocityEditDrag = [&]( const Vector3& dragRayOrigin, const Vector3& dragRayDirection )
    {
        // Hazard: a drag can outlive its target if the scene reloads or the
        // edited body is removed. All capture and active-axis state must unwind
        // before any velocity math touches the model collection.
        ReplayVelocityBodyView body;
        const RuntimeInteractionGesture& gesture = m_interaction.Gesture();
        if ( !tryResolveVelocityBody( body ) || gesture.kind != RuntimeInteractionGestureKind::ReplayVelocityDrag ||
             gesture.axis < 0 )
        {
            replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_inputRouter.ReleaseNativeCapture();
            return;
        }

        Vector3 linearVelocity = m_replayRuntime.VelocityEdit().dragStartLinearVelocity;
        Vector3 angularVelocity = m_replayRuntime.VelocityEdit().dragStartAngularVelocity;
        if ( gesture.angular )
        {
            float currentAngle = 0.0f;
            if ( !TryReplayVelocityAngularRayAngle( body,
                                                    gesture.axis,
                                                    dragRayOrigin,
                                                    dragRayDirection,
                                                    currentAngle ) )
            {
                return;
            }
            const float angleDelta =
                WrapEditorAngleDelta( currentAngle - m_replayRuntime.VelocityEdit().dragStartAngle );
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartAngularVelocity, gesture.axis ) +
                angleDelta * ( REPLAY_VELOCITY_EDIT_ANGULAR_MAX / _PI );
            ReplayVelocitySetAxisComponent(
                angularVelocity,
                gesture.axis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_ANGULAR_MAX, REPLAY_VELOCITY_EDIT_ANGULAR_MAX ) );
        }
        else
        {
            float axisT = 0.0f;
            if ( !TryReplayVelocityAxisRayParameter( body, gesture.axis, dragRayOrigin, dragRayDirection, axisT ) )
            {
                return;
            }
            const float component =
                ReplayVelocityAxisComponent( m_replayRuntime.VelocityEdit().dragStartLinearVelocity, gesture.axis ) +
                ( axisT - m_replayRuntime.VelocityEdit().dragStartAxisT ) * ReplayVelocityLinearUnitsPerWorld();
            ReplayVelocitySetAxisComponent(
                linearVelocity,
                gesture.axis,
                std::clamp( component, -REPLAY_VELOCITY_EDIT_LINEAR_MAX, REPLAY_VELOCITY_EDIT_LINEAR_MAX ) );
        }

        replayInteraction.ApplyVelocityEditToBody(
            ReplayVelocityEditApplyContext{ m_replayRuntime,
                                            velocityPhysics,
                                            body.body,
                                            linearVelocity,
                                            angularVelocity,
                                            REPLAY_VELOCITY_EDIT_LINEAR_MAX,
                                            REPLAY_VELOCITY_EDIT_ANGULAR_MAX,
                                            now + REPLAY_SCRUBBER_VISIBLE_SECONDS } );
    };

    if ( velocityDragActive() )
    {
        if ( leftDown && !uiBlocksMouse )
        {
            applyReplayVelocityEditDrag( rayOrigin, rayDirection );
        }
        if ( leftReleased || !leftDown )
        {
            replayInteraction.EndVelocityEditDrag( m_replayRuntime );
            m_replayRuntime.EndToolGesture( m_interaction, RuntimeInteractionGestureKind::ReplayVelocityDrag );
            m_inputRouter.ReleaseNativeCapture();
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
        if ( prediction.build.complete && prediction.simulation.frames.size() >= 2 &&
             m_replayRuntime.PathVisualizer().hasTarget )
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
        const RuntimePointerEvent& runtimePointer = m_inputRouter.RuntimeSnapshot().pointer;
        ReplayVelocityBodyView body;
        if ( runtimePointer.hasClientPosition && tryResolveVelocityBody( body ) && !body.fixed )
        {
            const POINT mouse{ runtimePointer.clientX, runtimePointer.clientY };
            if ( m_replayRuntime.VelocityEdit().hotAngularAxis >= 0 )
            {
                float startAngle = 0.0f;
                if ( TryReplayVelocityAngularRayAngle( body,
                                                       m_replayRuntime.VelocityEdit().hotAngularAxis,
                                                       rayOrigin,
                                                       rayDirection,
                                                       startAngle ) )
                {
                    outEnterInteractive = true;
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                         WorldInteractionOwner::ReplayScrub,
                                                                         InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        enterInspectionCamera();
                    }
                    else
                    {
                        exitInspectionCamera();
                    }
                    if ( !m_replayRuntime.BeginToolGesture( m_interaction,
                                                            RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                                            WorldInteractionOwner::ReplayVelocityEdit,
                                                            RuntimePointerButton::Left,
                                                            mouse.x,
                                                            mouse.y,
                                                            body.body,
                                                            m_replayRuntime.VelocityEdit().hotAngularAxis,
                                                            true ) )
                    {
                        return false;
                    }
                    ReplayVelocityEditDragStart dragStart;
                    dragStart.angle = startAngle;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    replayInteraction.BeginVelocityEditDrag( m_replayRuntime, dragStart );
                    m_inputRouter.RequestNativeCapture();
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
                    outEnterInteractive = true;
                    if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && !IsReplayToolOwner( m_interaction.Owner() ) )
                    {
                        interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                                         WorldInteractionOwner::ReplayScrub,
                                                                         InteractionExitReason::EnterReplay );
                    }
                    if ( m_replayRuntime.ShouldUseInspectionCamera() )
                    {
                        enterInspectionCamera();
                    }
                    else
                    {
                        exitInspectionCamera();
                    }
                    if ( !m_replayRuntime.BeginToolGesture( m_interaction,
                                                            RuntimeInteractionGestureKind::ReplayVelocityDrag,
                                                            WorldInteractionOwner::ReplayVelocityEdit,
                                                            RuntimePointerButton::Left,
                                                            mouse.x,
                                                            mouse.y,
                                                            body.body,
                                                            m_replayRuntime.VelocityEdit().hotLinearAxis,
                                                            false ) )
                    {
                        return false;
                    }
                    ReplayVelocityEditDragStart dragStart;
                    dragStart.axisT = axisT;
                    dragStart.linearVelocity = body.linearVelocity;
                    dragStart.angularVelocity = body.angularVelocity;
                    armBaselineComparisonForDrag();
                    replayInteraction.BeginVelocityEditDrag( m_replayRuntime, dragStart );
                    m_inputRouter.RequestNativeCapture();
                    return true;
                }
            }
        }

        // Concept: velocity edit owns replay body targeting. A click on the
        // body itself should select the replay path target for the velocity
        // gizmo, not fall through to normal editor/world selection and clear it.
        ReplayRuntime::PathPickInput pickInput;
        pickInput.hasWorldRay = pointerRay.hasWorldRay;
        pickInput.rayOrigin = rayOrigin;
        pickInput.rayDirection = rayDirection;
        (void)m_replayRuntime.TryPickPathTarget( pickInput, entities, velocityBodies, velocityColliders, presentation );
        if ( m_replayRuntime.PathVisualizer().hasTarget )
        {
            outEnterInteractive = true;
            if ( m_replayRuntime.SetLiveAdvanceHeld( true ) && m_replayRuntime.ShouldUseInspectionCamera() )
            {
                enterInspectionCamera();
            }
            interaction.SetWorldInteractionOwnerInWorkspace( RuntimeWorkspace::Replay,
                                                             WorldInteractionOwner::ReplayVelocityEdit,
                                                             InteractionExitReason::EnterReplay );
            replayInteraction.SelectVelocityEditTarget( m_replayRuntime, now + REPLAY_SCRUBBER_VISIBLE_SECONDS );
        }
        return true;
    }

    return m_replayRuntime.VelocityEdit().hotLinearAxis >= 0 || m_replayRuntime.VelocityEdit().hotAngularAxis >= 0;
}


void ReplayRuntime::RenderVelocityEditOverlay( PhysicsEngine& velocityPhysics,
                                               bool editorModeEnabled,
                                               const RuntimeInteractionGesture& gesture,
                                               RunEditorTracer& tracer )
{
    PROFILE_SCOPED( "Frame/Replay/VelocityEdit/Overlay" );
    if ( !VelocityEdit().enabled || editorModeEnabled )
    {
        return;
    }

    ReplayVelocityBodyView body;
    if ( !TryResolveReplayVelocityBodyView(
             *this,
             SkullbonezCore::Physics::PhysicsEngineStoreQueries::BodyStore( velocityPhysics ),
             SkullbonezCore::Physics::PhysicsEngineStoreQueries::Colliders( velocityPhysics ),
             body ) ||
         body.fixed || !body.shape )
    {
        return;
    }
    tracer.AddReplayVelocityGizmo(
        body.position,
        body.orientation,
        *body.shape,
        body.radius,
        body.linearVelocity,
        body.angularVelocity,
        VelocityEdit().hotLinearAxis,
        VelocityEdit().hotAngularAxis,
        gesture.kind == RuntimeInteractionGestureKind::ReplayVelocityDrag ? gesture.axis : -1,
        gesture.kind == RuntimeInteractionGestureKind::ReplayVelocityDrag && gesture.angular );
}
