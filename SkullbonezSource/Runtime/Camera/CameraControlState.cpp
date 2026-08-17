/*
File: SkullbonezSource/Runtime/Camera/CameraControlState.cpp
Purpose:
  Applies operator camera-selection intent to the scene-owned camera collection.

Summary:
  CameraControlState owns mode and generated-demo cycling state, while
  CameraCollection owns camera poses. The update borrows pose/model owners for
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
#include "CameraControlState.h"

#include "CameraCollection.h"
#include "AttachedCameraController.h"
#include "../Input/InputController.h"
#include "../App/RunTimerState.h"
#include "../../Assets/AssetKeys.h"
#include "../../Core/Config.h"
#include "../Scene/SceneWorld.h"
#include "../../World/Terrain.h"
#include "../../Core/Profiler.h"

using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore::Runtime
{
void CameraControlState::UpdateViewingOrientation( RunTimerState& timers, Runtime::SceneWorld& world,
                                                   bool replayCameraActive, bool sceneMode, bool attachedActiveFollow,
                                                   bool cameraLookCaptured, float presentationAlpha, Core::Profiler* )
{
    Environment::CameraCollection& cameras = world.Cameras();

    if ( replayCameraActive )
    {
        PROFILE_SCOPED( "Frame/Replay/Camera" );
        cameraTime = 0.0f;
        timers.cameraTimer.StopTimer();
        timers.cameraTimer.StartTimer();
        return;
    }

    if ( sceneMode )
    {
        return;
    }

    if ( RunCameraModeUsesFlyControls( mode, attachedActiveFollow, director.grabbed ) || cameraLookCaptured )
    {
        cameraTime = 0.0f;
        timers.cameraTimer.StopTimer();
        timers.cameraTimer.StartTimer();
        return;
    }

    timers.cameraTimer.StopTimer();
    cameraTime += static_cast<float>( timers.cameraTimer.GetElapsedTime() );
    timers.cameraTimer.StartTimer();

    if ( cameraTime > 5.0f )
    {
        selectedCamera = ( selectedCamera + 1 ) % 3;
        cameraTime = 0.0f;
    }

    cameras.SelectCamera( DEMO_CAMERA_CYCLE_SLOTS[static_cast<std::size_t>( selectedCamera )], true );

    // Why: the two authored tracking slots follow presentation rows 0 and 1;
    // a missing row leaves the previous view target intact for this frame.
    for ( int modelIndex = 0; modelIndex < 2; ++modelIndex )
    {
        if ( !cameras.IsCameraSelected( DEMO_CAMERA_CYCLE_SLOTS[static_cast<std::size_t>( modelIndex )] ) )
        {
            continue;
        }

        Vector3 targetPosition;
        Quaternion targetOrientation;

        if ( world.TryGetPresentationPose( modelIndex, presentationAlpha, targetPosition, targetOrientation ) )
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


void CameraControlState::TickControls( Runtime::SceneWorld& world, AttachedCameraController& attachedCamera,
                                       const SkullbonezCore::Core::EngineConfig& config, bool editorModeEnabled,
                                       bool viewportLookActive, bool sceneMode, float cameraDt, float presentationAlpha )
{
    Environment::CameraCollection& cameras = world.Cameras();
    Geometry::Terrain& terrain = *world.Terrain().Get();
    constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;
    mouseRadiansPerPixel = CAMERA_MOUSE_REFERENCE_DT * config.camera.mouseSensitivity;
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( mode ) && attachedCamera.State().activeFollow &&
                                         attachedCamera.State().submode != AttachedCameraSubmode::RagdollEyes;

    InputController::ApplyCameraMovement( *this, cameras, terrain,
                                          RuntimeCameraMovementInput { cameraDt * config.camera.keySpeed,
                                                                       CAMERA_MOUSE_REFERENCE_DT *
                                                                           config.camera.mouseSensitivity,
                                                                       config.camera.minCameraHeight,
                                                                       config.camera.maxCameraHeight,
                                                                       attachedOrbitOwnsCamera,
                                                                       RunCameraModeUsesFlyControls( mode,
                                                                                                     attachedCamera.State()
                                                                                                         .activeFollow,
                                                                                                     director.grabbed ),
                                                                       editorModeEnabled, viewportLookActive,
                                                                       RunCameraModeUsesManualControls( mode,
                                                                                                        attachedCamera
                                                                                                            .State()
                                                                                                            .activeFollow,
                                                                                                        director.grabbed ),
                                                                       sceneMode } );

    if ( RunCameraModeIsAttached( mode ) )
    {
        const float orbitYawDelta = static_cast<float>( input.xMove ) * CAMERA_MOUSE_REFERENCE_DT *
                                    config.camera.mouseSensitivity;

        const float orbitPitchDelta = static_cast<float>( input.yMove ) * CAMERA_MOUSE_REFERENCE_DT *
                                      config.camera.mouseSensitivity;

        (void)attachedCamera.TickFollow( world, orbitYawDelta, orbitPitchDelta, presentationAlpha );
    }

    cameras.SetTweenDeltaSeconds( cameraDt );
}
} // namespace SkullbonezCore::Runtime
