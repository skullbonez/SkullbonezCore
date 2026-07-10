/*
File: SkullbonezSource/Runtime/Editor/RunMousePickupTools.cpp
Purpose:
  Implements manipulator-mode mouse pickup capture, target tracking, and physics impulse application.

Mental model:
  Mouse pickup is a runtime tool owner distinct from editor placement. It
  borrows camera rays, stores the picked physics body handle for commands,
  captures the pointer while active, and releases cleanly at mode changes.

Glossary:
  Mouse pickup: Manipulator tool that drags a dynamic body toward a camera-facing target plane.
  Grab offset: World-space offset from body center to the point initially picked by the ray.
  Physics body handle: Generational id for the picked body-store row.
  Gesture model row: Dense model row copied into RuntimeInteractionGesture when
    the drag starts; live pickup state uses the physics body handle instead.

Invariants:
  - Pointer capture and interaction gesture state must end whenever pickup is canceled.
  - Picked handles are revalidated through PhysicsBodyStore before every
    velocity edit or impulse.
  - Pointer routing mutates pickup state only through `RuntimeTools`; Run keeps
    only the later physics-step hooks until that boundary is extracted.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "../RunInternal.h"
#include "../RuntimePickService.h"
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
namespace Basics
{
using Physics::PhysicsBodyRecord;
using Physics::PhysicsBodyStore;

MousePickupPointerResult RuntimeTools::RouteMousePickupPointer( const MousePickupPointerInput& input,
                                                                const GameObjects::GameModelCollection& collection,
                                                                InputRouter& inputRouter,
                                                                RuntimeInteractionController& interaction )
{
    MousePickupPointerResult routeResult;
    if ( !input.manipulatorMode || input.editorMode || input.replayInspection )
    {
        CancelMousePickup( inputRouter, interaction );
        return routeResult;
    }

    const auto updatePickupTarget = [&]() -> bool
    {
        // Concept: Manipulator drag follows a camera-facing plane at the
        // captured grab depth. Rebuilding that plane from the current camera
        // lets forward/back camera movement change object depth without
        // introducing a mouse-driven depth jump.
        if ( !input.hasClampedWorldRay )
        {
            return false;
        }

        Vector3 cameraNormal = input.cameraView - input.cameraEye;
        const float normalLenSq = VectorMagSquared( cameraNormal );
        if ( normalLenSq <= TOLERANCE * TOLERANCE )
        {
            return false;
        }
        cameraNormal *= 1.0f / sqrtf( normalLenSq );

        m_mousePickup.planeNormal = cameraNormal;
        m_mousePickup.planePoint = input.cameraEye + cameraNormal * m_mousePickup.cameraPlaneDistance;

        const float denom = input.clampedRayDirection * cameraNormal;
        if ( fabsf( denom ) <= 1.0e-5f )
        {
            return false;
        }

        const float planeT = ( ( m_mousePickup.planePoint - input.clampedRayOrigin ) * cameraNormal ) / denom;
        if ( planeT < 0.0f )
        {
            return false;
        }

        m_mousePickup.targetPoint = input.clampedRayOrigin + input.clampedRayDirection * planeT;
        return true;
    };

    if ( m_mousePickup.active )
    {
        routeResult.consumed = true;
        if ( input.leftReleased || !input.leftDown )
        {
            CancelMousePickup( inputRouter, interaction );
            return routeResult;
        }
        updatePickupTarget();
        return routeResult;
    }

    if ( !input.leftPressed )
    {
        return routeResult;
    }
    if ( input.suppressWorldAction || input.uiWantsNativeCursor )
    {
        return routeResult;
    }
    routeResult.consumed = true;

    if ( !input.hasWorldRay )
    {
        return routeResult;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ManipulatorPickup;
    request.bodyStore = &collection.BodyStore();
    request.colliderStore = &collection.Colliders();
    request.rayOrigin = input.rayOrigin;
    request.rayDirection = input.rayDirection;

    RuntimePickResult result;
    if ( !RuntimePickService::TryPickModel( request, result ) )
    {
        return routeResult;
    }

    const PhysicsBodyStore& bodyStore = collection.BodyStore();
    const PhysicsBodyRecord* pickedBody = bodyStore.RecordForHandle( result.body );
    const int pickedIndex = bodyStore.ModelIndexForHandle( result.body );
    if ( !pickedBody || pickedIndex != result.modelIndex )
    {
        return routeResult;
    }

    Vector3 cameraNormal = input.cameraView - input.cameraEye;
    const float normalLenSq = VectorMagSquared( cameraNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return routeResult;
    }
    cameraNormal *= 1.0f / sqrtf( normalLenSq );

    const Vector3 grabPoint = input.rayOrigin + input.rayDirection * result.rayT;
    const float cameraPlaneDistance = ( grabPoint - input.cameraEye ) * cameraNormal;
    if ( cameraPlaneDistance <= TOLERANCE )
    {
        return routeResult;
    }
    m_mousePickup.active = true;
    m_mousePickup.mouseCaptured = true;
    m_mousePickup.body = result.body;
    m_mousePickup.planePoint = grabPoint;
    m_mousePickup.planeNormal = cameraNormal;
    m_mousePickup.cameraPlaneDistance = cameraPlaneDistance;
    m_mousePickup.grabOffset = grabPoint - pickedBody->position;
    m_mousePickup.targetPoint = grabPoint;
    m_mousePickup.preservedAngularVelocity = pickedBody->angularVelocity;
    m_mousePickup.lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    inputRouter.RequestNativeCapture();
    if ( !input.hasClientPosition )
    {
        inputRouter.ReleaseNativeCapture();
        CancelMousePickup( inputRouter, interaction );
        routeResult.consumed = false;
        return routeResult;
    }
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = input.clientX;
    gesture.startY = input.clientY;
    gesture.modelIndex = pickedIndex;
    interaction.BeginGesture( gesture,
                              RuntimePointerCaptureOwner::ToolGesture,
                              InteractionExitReason::EnterManipulator );
    routeResult.enteredInteractive = true;
    updatePickupTarget();
    return routeResult;
}


void Run::ApplyMousePickupPhysicsStep()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    // Hazard: Pickup stores a live body handle. Revalidate it before every
    // physics write so deleted/reused body slots cannot receive a stale tool
    // impulse.
    RunMousePickupState& pickup = m_runtimeTools.MousePickup();
    const PhysicsBodyStore& bodyStore = m_sceneController.Models().BodyStore();
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( pickup.body );
    if ( !bodyRecord )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return;
    }

    if ( bodyRecord->isFixed )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return;
    }
    const Vector3 bodyPosition = bodyRecord->position;
    const Vector3 linearVelocity = bodyRecord->linearVelocity;
    if ( !m_sceneController.Physics().SetBodyVelocity( pickup.body,
                                                       linearVelocity,
                                                       pickup.preservedAngularVelocity,
                                                       false ) )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
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

    m_sceneController.Physics().ApplyBodyImpulse( pickup.body, impulse, SkullbonezCore::Math::Vector::ZERO_VECTOR );
    pickup.lastImpulse = impulse;
}


void Run::RestoreMousePickupAngularVelocity()
{
    if ( !m_runtimeTools.MousePickup().active )
    {
        return;
    }

    RunMousePickupState& pickup = m_runtimeTools.MousePickup();
    const PhysicsBodyStore& bodyStore = m_sceneController.Models().BodyStore();
    const PhysicsBodyRecord* bodyRecord = bodyStore.RecordForHandle( pickup.body );
    if ( !bodyRecord )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return;
    }

    if ( bodyRecord->isFixed )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return;
    }

    if ( !m_sceneController.Physics().SetBodyVelocity( pickup.body,
                                                       bodyRecord->linearVelocity,
                                                       pickup.preservedAngularVelocity,
                                                       false ) )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
    }
}
} // namespace Basics
} // namespace SkullbonezCore
