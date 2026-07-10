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
  - Mouse pickup remains a standalone Run implementation file; shared editor
    declarations stay on Run and RuntimeTools.

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

bool Run::TickMousePickupInput( const RuntimeMouseEdges& mouseEdges, bool suppressWorldActionThisFrame )
{
    if ( !RunCameraModeIsManipulator( m_camera.mode ) || m_runtimeTools.Editor().editorModeEnabled ||
         m_replayRuntime.InspectionActive() )
    {
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return false;
    }

    const auto UpdatePickupTarget = [&]() -> bool
    {
        // Concept: Manipulator drag follows a camera-facing plane at the
        // captured grab depth. Rebuilding that plane from the current camera
        // lets forward/back camera movement change object depth without
        // introducing a mouse-driven depth jump.
        Vector3 rayOrigin;
        Vector3 rayDirection;
        if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection, true ) )
        {
            return false;
        }

        Vector3 cameraNormal = m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
        const float normalLenSq = VectorMagSquared( cameraNormal );
        if ( normalLenSq <= TOLERANCE * TOLERANCE )
        {
            return false;
        }
        cameraNormal *= 1.0f / sqrtf( normalLenSq );

        m_runtimeTools.MousePickup().planeNormal = cameraNormal;
        m_runtimeTools.MousePickup().planePoint =
            m_systems.cameras->GetCameraTranslation() + cameraNormal * m_runtimeTools.MousePickup().cameraPlaneDistance;

        const float denom = rayDirection * cameraNormal;
        if ( fabsf( denom ) <= 1.0e-5f )
        {
            return false;
        }

        const float planeT = ( ( m_runtimeTools.MousePickup().planePoint - rayOrigin ) * cameraNormal ) / denom;
        if ( planeT < 0.0f )
        {
            return false;
        }

        m_runtimeTools.MousePickup().targetPoint = rayOrigin + rayDirection * planeT;
        return true;
    };

    if ( m_runtimeTools.MousePickup().active )
    {
        if ( mouseEdges.leftReleased || !mouseEdges.leftDown )
        {
            m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
            return true;
        }
        UpdatePickupTarget();
        return true;
    }

    if ( !mouseEdges.leftPressed )
    {
        return false;
    }
    if ( suppressWorldActionThisFrame || m_UI.WantsNativeMouseCursor() )
    {
        return false;
    }

    Vector3 rayOrigin;
    Vector3 rayDirection;
    if ( !TryBuildMouseWorldRay( rayOrigin, rayDirection ) )
    {
        return true;
    }

    RuntimePickRequest request;
    request.purpose = RuntimePickPurpose::ManipulatorPickup;
    request.bodyStore = &m_sceneController.Models().BodyStore();
    request.colliderStore = &m_sceneController.Models().Colliders();
    request.rayOrigin = rayOrigin;
    request.rayDirection = rayDirection;

    RuntimePickResult result;
    if ( !RuntimePickService::TryPickModel( request, result ) )
    {
        return true;
    }

    const PhysicsBodyStore& bodyStore = m_sceneController.Models().BodyStore();
    const PhysicsBodyRecord* pickedBody = bodyStore.RecordForHandle( result.body );
    const int pickedIndex = bodyStore.ModelIndexForHandle( result.body );
    if ( !pickedBody || pickedIndex != result.modelIndex )
    {
        return true;
    }

    Vector3 cameraNormal = m_systems.cameras->GetCameraView() - m_systems.cameras->GetCameraTranslation();
    const float normalLenSq = VectorMagSquared( cameraNormal );
    if ( normalLenSq <= TOLERANCE * TOLERANCE )
    {
        return true;
    }
    cameraNormal *= 1.0f / sqrtf( normalLenSq );

    const Vector3 grabPoint = rayOrigin + rayDirection * result.rayT;
    const float cameraPlaneDistance = ( grabPoint - m_systems.cameras->GetCameraTranslation() ) * cameraNormal;
    if ( cameraPlaneDistance <= TOLERANCE )
    {
        return true;
    }
    m_runtimeTools.MousePickup().active = true;
    m_runtimeTools.MousePickup().mouseCaptured = true;
    m_runtimeTools.MousePickup().body = result.body;
    m_runtimeTools.MousePickup().planePoint = grabPoint;
    m_runtimeTools.MousePickup().planeNormal = cameraNormal;
    m_runtimeTools.MousePickup().cameraPlaneDistance = cameraPlaneDistance;
    m_runtimeTools.MousePickup().grabOffset = grabPoint - pickedBody->position;
    m_runtimeTools.MousePickup().targetPoint = grabPoint;
    m_runtimeTools.MousePickup().preservedAngularVelocity = pickedBody->angularVelocity;
    m_runtimeTools.MousePickup().lastImpulse = SkullbonezCore::Math::Vector::ZERO_VECTOR;
    m_inputRouter.RequestNativeCapture();
    const DeviceInputFrame& deviceFrame = m_inputRouter.DeviceFrame();
    if ( !deviceFrame.hasClientPosition )
    {
        m_inputRouter.ReleaseNativeCapture();
        m_runtimeTools.CancelMousePickup( m_inputRouter, m_interaction );
        return false;
    }
    RuntimeInteractionGesture gesture;
    gesture.kind = RuntimeInteractionGestureKind::MousePickupDrag;
    gesture.button = RuntimePointerButton::Left;
    gesture.startX = deviceFrame.clientX;
    gesture.startY = deviceFrame.clientY;
    gesture.modelIndex = pickedIndex;
    m_interaction.BeginGesture( gesture,
                                RuntimePointerCaptureOwner::ToolGesture,
                                InteractionExitReason::EnterManipulator );
    EnterInteractiveSceneRun();
    UpdatePickupTarget();
    return true;
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
