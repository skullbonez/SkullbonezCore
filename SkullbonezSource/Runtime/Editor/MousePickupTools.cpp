/*
File: SkullbonezSource/Runtime/Editor/MousePickupTools.cpp
Purpose:
  Implements manipulator-mode mouse pickup capture, target tracking, and physics impulse application.

Summary:
  Mouse pickup is a runtime tool owner distinct from editor placement. It
  borrows camera rays, stores the picked physics body handle for commands,
  captures the pointer while active, and releases cleanly at mode changes.

Glossary:
  Mouse pickup: Manipulator tool that drags a dynamic body toward a camera-facing target plane.
  Grab offset: World-space offset from body center to the point initially picked by the ray.
  Physics body handle: Generational id for the picked body-store row.
  Gesture body: Stable handle captured in the begin command and retained by the
    controller for the drag lifetime.

Invariants:
  - Pointer capture and interaction gesture state must end whenever pickup is canceled.
  - Picked handles are revalidated through PhysicsBodyStore before every
    velocity edit or impulse.
  - Pointer routing and fixed-step spring application mutate pickup state only
    through `RuntimeTools`; the application shell only sequences the owner.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "../Tools/RuntimeTools.h"
#include "../Input/InputRouter.h"
#include "../Interaction/RuntimeInteractionController.h"
#include "../Interaction/RuntimeInteractionCommands.h"
#include "../Interaction/RuntimePickService.h"
#include "../Scene/SceneWorld.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Physics/PhysicsEngine.h"

namespace
{
constexpr float MOUSE_PICKUP_DEAD_ZONE = 0.04f;
constexpr float MOUSE_PICKUP_STIFFNESS = 18.0f;
constexpr float MOUSE_PICKUP_DAMPING = 1.35f;
constexpr float MOUSE_PICKUP_MAX_IMPULSE = 260.0f;
} // namespace

namespace SkullbonezCore
{
namespace Runtime
{
using Math::Vector::Vector3;
using Math::Vector::VectorMagSquared;
using Physics::PhysicsBodyRecord;
using Physics::PhysicsBodyStore;

MousePickupPointerResult RuntimeTools::RouteMousePickupPointer( const RuntimePointerEvent& pointer,
                                                                bool hasWorldRay,
                                                                const Vector3& rayOrigin,
                                                                const Vector3& rayDirection,
                                                                bool hasClampedWorldRay,
                                                                const Vector3& clampedRayOrigin,
                                                                const Vector3& clampedRayDirection,
                                                                const Vector3& cameraEye,
                                                                const Vector3& cameraView,
                                                                const SceneWorld& world,
                                                                InputRouter& inputRouter,
                                                                RuntimeInteractionController& interaction )
{
    MousePickupPointerResult routeResult;
    const auto updatePickupTarget = [&]() -> bool
    {
        // Concept: Manipulator drag follows a camera-facing plane at the
        // captured grab depth. Rebuilding that plane from the current camera
        // lets forward/back camera movement change object depth without
        // introducing a mouse-driven depth jump.
        if ( !hasClampedWorldRay )
        {
            return false;
        }

        Vector3 cameraNormal = cameraView - cameraEye;
        const float normalLenSq = VectorMagSquared( cameraNormal );
        if ( normalLenSq <= TOLERANCE * TOLERANCE )
        {
            return false;
        }

        cameraNormal *= 1.0f / sqrtf( normalLenSq );

        m_mousePickup.planeNormal = cameraNormal;
        m_mousePickup.planePoint = cameraEye + cameraNormal * m_mousePickup.cameraPlaneDistance;

        const float denom = clampedRayDirection * cameraNormal;
        if ( fabsf( denom ) <= 1.0e-5f )
        {
            return false;
        }

        const float planeT = ( ( m_mousePickup.planePoint - clampedRayOrigin ) * cameraNormal ) / denom;
        if ( planeT < 0.0f )
        {
            return false;
        }

        m_mousePickup.targetPoint = clampedRayOrigin + clampedRayDirection * planeT;
        return true;
    };

    if ( interaction.Gesture().kind == RuntimeInteractionGestureKind::MousePickupDrag )
    {
        routeResult.consumed = true;
        if ( pointer.leftReleased || !pointer.leftDown )
        {
            CancelMousePickup( inputRouter, interaction );
            return routeResult;
        }

        updatePickupTarget();
        return routeResult;
    }

    if ( !pointer.leftPressed )
    {
        return routeResult;
    }

    if ( pointer.suppressWorldAction || pointer.uiWantsNativeMouseCursor )
    {
        return routeResult;
    }

    routeResult.consumed = true;

    if ( !hasWorldRay )
    {
        return routeResult;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ManipulatorPickup;
    request.bodyStore = &world.BodyStore();
    request.colliderStore = &world.Colliders();
    request.rayOrigin = rayOrigin;
    request.rayDirection = rayDirection;

    RuntimePickResult result;
    if ( !RuntimePickService::TryPickModel( request, result ) )
    {
        return routeResult;
    }

    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* pickedBody = bodyStore.RecordForHandle( result.body );
    const int pickedBodyIndex = bodyStore.ModelIndexForHandle( result.body );
    if ( !pickedBody || pickedBodyIndex < 0 )
    {
        return routeResult;
    }

    Vector3 cameraNormal = cameraView - cameraEye;
    const float normalLenSq = VectorMagSquared( cameraNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return routeResult;
    }

    cameraNormal *= 1.0f / sqrtf( normalLenSq );

    const Vector3 grabPoint = rayOrigin + rayDirection * result.rayT;
    const float cameraPlaneDistance = ( grabPoint - cameraEye ) * cameraNormal;
    if ( cameraPlaneDistance <= TOLERANCE )
    {
        return routeResult;
    }

    if ( !pointer.hasClientPosition )
    {
        routeResult.consumed = false;
        return routeResult;
    }

    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = pointer.clientX;
    gesture.startY = pointer.clientY;
    gesture.body = result.body;
    RuntimeGestureCommand command;
    command.gesture = gesture;
    command.reason = InteractionExitReason::EnterManipulator;
    RuntimeGestureEvent event;
    if ( !interaction.ApplyGestureCommand( command, event ) )
    {
        return routeResult;
    }

    m_mousePickup.body = result.body;
    m_mousePickup.planePoint = grabPoint;
    m_mousePickup.planeNormal = cameraNormal;
    m_mousePickup.cameraPlaneDistance = cameraPlaneDistance;
    const std::size_t hotIndex = static_cast<std::size_t>( pickedBodyIndex );
    const auto hotFields = bodyStore.HotFields();
    m_mousePickup.grabOffset = grabPoint - PhysicsBodyPosition( hotFields, hotIndex );
    m_mousePickup.targetPoint = grabPoint;
    m_mousePickup.preservedAngularVelocity = PhysicsBodyAngularVelocity( hotFields, hotIndex );
    m_mousePickup.lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    inputRouter.RequestNativeCapture();
    routeResult.enteredInteractive = true;
    updatePickupTarget();
    return routeResult;
}


void RuntimeTools::ApplyMousePickupPhysicsStep( SceneWorld& world,
                                                InputRouter& inputRouter,
                                                RuntimeInteractionController& interaction )
{
    if ( interaction.Gesture().kind != RuntimeInteractionGestureKind::MousePickupDrag )
    {
        return;
    }

    // Hazard: Pickup stores a live body handle. Revalidate it before every
    // physics write so deleted/reused body slots cannot receive a stale tool
    // impulse.
    RunMousePickupState& pickup = m_mousePickup;
    Physics::PhysicsEngine& physics = world.Physics();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( pickup.body );
    const int bodyIndex = bodyStore.ModelIndexForHandle( pickup.body );
    if ( !bodyRecord || bodyIndex < 0 )
    {
        CancelMousePickup( inputRouter, interaction );
        return;
    }

    const std::size_t hotIndex = static_cast<std::size_t>( bodyIndex );
    const auto hotFields = bodyStore.HotFields();
    if ( hotFields.fixed[hotIndex] != 0u )
    {
        CancelMousePickup( inputRouter, interaction );
        return;
    }

    const Vector3 bodyPosition = PhysicsBodyPosition( hotFields, hotIndex );
    const Vector3 linearVelocity = PhysicsBodyLinearVelocity( hotFields, hotIndex );
    if ( !physics.SetBodyVelocity( pickup.body, linearVelocity, pickup.preservedAngularVelocity, false ) )
    {
        CancelMousePickup( inputRouter, interaction );
        return;
    }

    const Vector3 grabPoint = bodyPosition + pickup.grabOffset;
    const Vector3 pull = pickup.targetPoint - grabPoint;
    const float pullLenSq = VectorMagSquared( pull );
    if ( pullLenSq <= MOUSE_PICKUP_DEAD_ZONE * MOUSE_PICKUP_DEAD_ZONE )
    {
        pickup.lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
        return;
    }

    Vector3 impulse = pull * MOUSE_PICKUP_STIFFNESS - linearVelocity * MOUSE_PICKUP_DAMPING;
    const float impulseLenSq = VectorMagSquared( impulse );
    if ( impulseLenSq > MOUSE_PICKUP_MAX_IMPULSE * MOUSE_PICKUP_MAX_IMPULSE )
    {
        impulse *= MOUSE_PICKUP_MAX_IMPULSE / sqrtf( impulseLenSq );
    }

    physics.ApplyBodyImpulse( pickup.body, impulse, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    pickup.lastImpulse = impulse;
}


void RuntimeTools::RestoreMousePickupAngularVelocity( SceneWorld& world,
                                                      InputRouter& inputRouter,
                                                      RuntimeInteractionController& interaction )
{
    if ( interaction.Gesture().kind != RuntimeInteractionGestureKind::MousePickupDrag )
    {
        return;
    }

    RunMousePickupState& pickup = m_mousePickup;
    Physics::PhysicsEngine& physics = world.Physics();
    const PhysicsBodyStore& bodyStore = world.BodyStore();
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( pickup.body );
    const int bodyIndex = bodyStore.ModelIndexForHandle( pickup.body );
    if ( !bodyRecord || bodyIndex < 0 )
    {
        CancelMousePickup( inputRouter, interaction );
        return;
    }

    const std::size_t hotIndex = static_cast<std::size_t>( bodyIndex );
    const auto hotFields = bodyStore.HotFields();
    if ( hotFields.fixed[hotIndex] != 0u )
    {
        CancelMousePickup( inputRouter, interaction );
        return;
    }

    if ( !physics.SetBodyVelocity( pickup.body,
                                   PhysicsBodyLinearVelocity( hotFields, hotIndex ),
                                   pickup.preservedAngularVelocity,
                                   false ) )
    {
        CancelMousePickup( inputRouter, interaction );
    }
}
} // namespace Runtime
} // namespace SkullbonezCore
