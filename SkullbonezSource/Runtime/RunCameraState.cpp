/*
File: SkullbonezSource/Runtime/RunCameraState.cpp
Purpose:
  Applies operator camera-selection intent to the scene-owned camera collection.

Summary:
  RunCameraState owns mode and generated-demo cycling state, while
  CameraCollection owns camera poses. The update borrows pose/model owners for
  one synchronous selection pass and retains no cross-owner pointers.

Glossary:
  Generated-demo camera: One of the three legacy unattended camera slots.
  Manual camera: A workspace where pointer or attached-follow input owns view selection.

Invariants:
  - Replay, authored-scene, and manual camera ownership suppresses demo cycling.
  - Object-follow views sample model presentation positions without reopening physics storage.

Related:
  - SkullbonezSource/Runtime/RunCameraState.h
  - SkullbonezSource/Runtime/CameraCollection.h
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunCameraState.h"

#include "CameraCollection.h"
#include "AttachedCameraController.h"
#include "InputController.h"
#include "RunTimerState.h"
#include "../Assets/AssetKeys.h"
#include "../Core/Config.h"
#include "Scene/SceneController.h"
#include "../World/Terrain.h"
#include "../Core/Profiler.h"

using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Vector::Vector3;

namespace SkullbonezCore::Basics
{
void RunCameraState::UpdateViewingOrientation( RunTimerState& timers,
                                               Environment::CameraCollection& cameras,
                                               const Basics::SceneController& models,
                                               bool replayCameraActive,
                                               bool sceneMode,
                                               bool attachedActiveFollow,
                                               bool cameraLookCaptured,
                                               float presentationAlpha )
{
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

    constexpr int cameraSlots[] = { CAMERA_GAME_MODEL_1, CAMERA_GAME_MODEL_2, CAMERA_FREE };
    cameras.SelectCamera( cameraSlots[selectedCamera], true );

    // Why: the two authored tracking slots follow presentation rows 0 and 1;
    // a missing row leaves the previous view target intact for this frame.
    for ( int modelIndex = 0; modelIndex < 2; ++modelIndex )
    {
        if ( !cameras.IsCameraSelected( cameraSlots[modelIndex] ) )
        {
            continue;
        }
        Vector3 targetPosition;
        Quaternion targetOrientation;
        if ( models.TryGetPresentationPose( modelIndex, presentationAlpha, targetPosition, targetOrientation ) )
        {
            cameras.SetViewCoordinates( targetPosition );
        }
    }
}


void RunCameraState::AdvanceAutoCycleClock( bool sceneMode, float simulationDt )
{
    if ( sceneMode && autoCycleInterval > 0.0f )
    {
        autoCycleAccum += simulationDt;
    }
}


void RunCameraState::TickControls( Environment::CameraCollection& cameras,
                                   Geometry::Terrain& terrain,
                                   Basics::SceneController& models,
                                   AttachedCameraController& attachedCamera,
                                   const EngineConfig& config,
                                   bool editorModeEnabled,
                                   bool viewportLookActive,
                                   bool sceneMode,
                                   float cameraDt,
                                   float presentationAlpha )
{
    constexpr float CAMERA_MOUSE_REFERENCE_DT = 1.0f / 60.0f;
    const bool attachedOrbitOwnsCamera = RunCameraModeIsAttached( mode ) && attachedCamera.State().activeFollow &&
                                         attachedCamera.State().submode != AttachedCameraSubmode::RagdollEyes;
    InputController::ApplyCameraMovement(
        *this,
        cameras,
        terrain,
        RuntimeCameraMovementInput{
            cameraDt * config.camera.keySpeed,
            CAMERA_MOUSE_REFERENCE_DT * config.camera.mouseSensitivity,
            config.camera.minCameraHeight,
            config.camera.maxCameraHeight,
            attachedOrbitOwnsCamera,
            RunCameraModeUsesFlyControls( mode, attachedCamera.State().activeFollow, director.grabbed ),
            editorModeEnabled,
            viewportLookActive,
            RunCameraModeUsesManualControls( mode, attachedCamera.State().activeFollow, director.grabbed ),
            sceneMode } );
    if ( RunCameraModeIsAttached( mode ) )
    {
        const float orbitYawDelta =
            static_cast<float>( input.xMove ) * CAMERA_MOUSE_REFERENCE_DT * config.camera.mouseSensitivity;
        const float orbitPitchDelta =
            static_cast<float>( input.yMove ) * CAMERA_MOUSE_REFERENCE_DT * config.camera.mouseSensitivity;
        (void)attachedCamera.TickFollow( models, cameras, orbitYawDelta, orbitPitchDelta, presentationAlpha );
    }
    cameras.SetTweenSpeed( config.camera.cameraTweenRate * cameraDt );
}
} // namespace SkullbonezCore::Basics
