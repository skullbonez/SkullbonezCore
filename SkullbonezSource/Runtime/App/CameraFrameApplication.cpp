/*
File: SkullbonezSource/Runtime/App/CameraFrameApplication.cpp
Purpose:
  Applies operator camera-selection intent to the scene-owned camera collection.

Summary:
  App applies Camera's mode and generated-demo cycling state to Scene's camera
  poses. The update borrows pose/model owners for
  one synchronous selection pass and retains no cross-owner pointers. It also
  publishes the current mouse-angle scale for an earlier replay input phase.

Glossary:
  Generated-demo camera: One of the three legacy unattended camera slots.
  Manual camera: A workspace where pointer or attached-follow input owns view selection.

Invariants:
  - Replay, authored-scene, and manual camera ownership suppresses demo cycling.
  - Object-follow views sample model presentation positions without reopening physics storage.
  - CameraCollection receives frame delta, not a recurrence rate; total elapsed
    time owns finite-duration tween progress.

Related:
  - SkullbonezSource/Runtime/Camera/CameraControlState.h
  - SkullbonezSource/Runtime/Camera/CameraCollection.h
*/
#include "../Camera/CameraControlState.h"

#include "../Camera/CameraCollection.h"
#include "../Scene/AttachedCameraController.h"
#include "../Input/InputController.h"

#include "../../Assets/AssetKeys.h"
#include "../../Core/Config.h"
#include "../Scene/SceneWorld.h"
#include "../../World/Terrain.h"
#include "../../World/WorldEnvironment.h"
#include "../../Core/Profiler.h"


using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore::Runtime
{
void CameraControlState::UpdateViewingOrientation( Runtime::SceneWorld& world, bool replayCameraActive, bool sceneMode,
                                                   bool attachedActiveFollow, bool cameraLookCaptured,
                                                   float presentationAlpha, Core::Profiler* )
{
    Environment::CameraCollection& cameras = world.Cameras();

    if ( replayCameraActive )
    {
        PROFILE_SCOPED( "Frame/Replay/Camera" );
        cameraTime = 0.0f;
        return;
    }

    if ( sceneMode )
    {
        return;
    }

    if ( RunCameraModeUsesFlyControls( mode, attachedActiveFollow, director.grabbed ) || cameraLookCaptured )
    {
        cameraTime = 0.0f;
        return;
    }

    cameras.SelectCamera( DEMO_CAMERA_CYCLE_SLOTS[static_cast<std::size_t>( selectedCamera )], true );

    // Why: generated camera slots follow the first two stable scene identities.
    // Snapshot loading may regroup dense model rows by asset, so retaining row 0
    // or 1 would silently retarget a recorded camera after scene restoration.
    for ( uint32_t targetId = 1u; targetId <= 2u; ++targetId )
    {
        const std::size_t cameraSlot = static_cast<std::size_t>( targetId - 1u );

        if ( !cameras.IsCameraSelected( DEMO_CAMERA_CYCLE_SLOTS[cameraSlot] ) )
        {
            continue;
        }

        const int modelIndex = world.Entities().FindBySceneObjectId( Physics::PhysicsSceneObjectId { targetId } );
        Vector3 targetPosition;
        Quaternion targetOrientation;

        if ( modelIndex >= 0 &&
             world.TryGetPresentationPose( modelIndex, presentationAlpha, targetPosition, targetOrientation ) )
        {
            cameras.SetViewCoordinates( targetPosition );
        }
    }
}


void CameraControlState::AdvanceAutoCycleClock( bool sceneMode, float simulationDt )
{
    if ( sceneMode && autoCycleInterval > 0.0f )
    {
        autoCycleAccum += simulationDt;
    }
}


void CameraControlState::TickControls( Runtime::SceneWorld& world, AttachedCameraController& attachedCamera, float cameraDt,
                                       float presentationAlpha, float fluidSurfaceHeight, bool attachedOrbitOwnsCamera,
                                       bool flyControlsActive, bool editorModeEnabled, bool editorViewportLookActive,
                                       bool manualControlsActive, bool authoredScene )
{
    Environment::CameraCollection& cameras = world.Cameras();
    Geometry::Terrain& terrain = *world.Terrain().Get();
    const float mouseMovementQuantity = ( 1.0f / 60.0f ) * m_mouseSensitivity;
    mouseRadiansPerPixel = mouseMovementQuantity;
    const bool hasTravelInput = inputMoveForward || inputMoveBackward || inputMoveLeft || inputMoveRight;
    const bool demoCycleEnabled = !manualControlsActive && !editorViewportLookActive && !authoredScene &&
                                  !mouseLookOwnsCursor;
    AdvanceDemoCameraCycleClock( cameraDt, demoCycleEnabled );

    if ( !attachedOrbitOwnsCamera &&
         ( flyControlsActive || mouseLookOwnsCursor || editorViewportLookActive || hasTravelInput ) )
    {
        if ( ( !editorModeEnabled || editorViewportLookActive ) && ( inputXMove != 0 || inputYMove != 0 ) )
        {
            // Why: Win32 mouse deltas describe screen motion (right and down are
            // positive), while Camera rotates world vectors with the engine's
            // right-handed active convention. Inverse radians keep free look
            // and locked launcher orbit aligned with pointer motion.
            cameras.RotatePrimary( InputController::ResolveMouseLookRadians( inputXMove, mouseMovementQuantity ),
                                   InputController::ResolveMouseLookRadians( inputYMove, mouseMovementQuantity ) );
        }

        const float travelQuantity = cameraDt * m_keySpeed * travelSpeedMultiplier;

        if ( inputMoveForward )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Forward, travelQuantity );
        }

        if ( inputMoveLeft )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Left, travelQuantity );
        }

        if ( inputMoveBackward )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Backward, travelQuantity );
        }

        if ( inputMoveRight )
        {
            cameras.MovePrimary( Environment::Camera::TravelDirection::Right, travelQuantity );
        }

        cameras.ApplyPrimaryMovementBuffer();
    }

    // Passive generated-demo camera bounds do not own manual or pinned follow views.
    if ( !manualControlsActive && !editorViewportLookActive && !authoredScene )
    {
        const Vector3 translatedCameraPosition = cameras.GetCameraTranslation();
        const float terrainHeight = terrain.GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z,
                                                                false );

        // The world owner may move water after startup; the frame carries that
        // live plane while stable movement limits stay with CameraControlState.
        const float resolvedY = InputController::ResolvePassiveCameraY( translatedCameraPosition.y, terrainHeight,
                                                                        fluidSurfaceHeight, m_minCameraHeight,
                                                                        m_maxCameraHeight );

        if ( resolvedY != translatedCameraPosition.y )
        {
            cameras.AmmendPrimaryY( resolvedY );
        }
    }

    if ( RunCameraModeIsAttached( mode ) )
    {
        const float orbitYawDelta = static_cast<float>( inputXMove ) * mouseMovementQuantity;
        const float orbitPitchDelta = static_cast<float>( inputYMove ) * mouseMovementQuantity;

        (void)attachedCamera.TickFollow( world, orbitYawDelta, orbitPitchDelta, presentationAlpha );
    }

    cameras.SetTweenDeltaSeconds( cameraDt );
}
} // namespace SkullbonezCore::Runtime
