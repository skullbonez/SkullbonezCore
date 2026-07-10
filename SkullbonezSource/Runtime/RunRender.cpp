/*
File: SkullbonezSource/Runtime/RunRender.cpp
Purpose:
  Sequences the application shell's camera update and one RuntimeRenderer frame.

Mental model:
  Run prepares immutable scene, replay, tool, and cinematic frame views after
  camera selection. RuntimeRenderer owns every render decision, pass, resource
  lifetime, overlay record, and submission detail behind that boundary.

Glossary:
  Render frame view: One-frame borrowed values consumed synchronously by
    RuntimeRenderer.
  Attached target: Stable scene selection followed by the attached camera.

Invariants:
  - Camera selection is finalized before render views are sampled.
  - Run performs top-level sequencing only; render passes never call back into
    Run or receive a Run pointer.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Basics::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;


void Run::Render( const RuntimeRenderModelFrameView& renderModels )
{
    m_renderer.SetUiTextRayTracingCapability( nullptr );

    // In text_only mode all 3D rendering is skipped. UiTextPass handles the display.
    if ( m_debug.isTextOnly )
    {
        return;
    }

    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    SetViewingOrientation();

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_sceneController.Cameras().SetCamera();

    const CinematicRenderConfig& activeCinematic = RuntimeActiveCinematicConfig( SceneState(), m_config );
    const bool cinematicRequested =
        RuntimeCinematicRenderingEnabled( SceneState(), m_config, m_launchOptions, m_debug, true );
    int attachedTargetIndex = -1;
    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        (void)TryResolveAttachedCameraTarget( attachedTargetIndex );
    }
    const RenderReplayOverlayView replayOverlay{ m_replayRuntime,
                                                 m_sceneController.Entities(),
                                                 SceneState().isScenePhysics,
                                                 SceneState().currentFrame,
                                                 m_timers.simulationTimer.GetTimeSinceLastStart(),
                                                 m_timers.simulationTimer.GetTotalTime() };
    const RenderToolOverlayView toolOverlay{ m_runtimeTools,
                                             InspectGizmoInteractionActive(),
                                             m_inputRouter.DeviceFrame().keys.IsDown( VK_CONTROL ),
                                             attachedTargetIndex,
                                             m_attachedCamera.activeFollow };
    m_renderer.RenderFrameEntry( RuntimeRenderer::FrameEntryContext{ m_renderBackendView,
                                                                     renderModels,
                                                                     m_sceneController.Models(),
                                                                     m_sceneController.Physics(),
                                                                     m_UI,
                                                                     replayOverlay,
                                                                     toolOverlay,
                                                                     activeCinematic,
                                                                     cinematicRequested,
                                                                     m_replayRuntime.Prediction().enabled } );
}


SbResult Run::RebuildRegisteredRenderResources()
{
    return m_renderer.RebuildRegisteredRenderResources(
        RuntimeRenderer::RegisteredResourceRebuildContext{ m_renderBackendView.renderResources,
                                                           m_systems.assets,
                                                           *m_systems.textures,
                                                           m_config } );
}


void Run::SetViewingOrientation()
{
    if ( m_replayRuntime.Camera().active )
    {
        PROFILE_SCOPED( "Frame/Replay/Camera" );
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    // In scene mode, use the authored camera without generated-demo tracking or cycling.
    if ( SceneState().isSceneMode )
    {
        return;
    }

    // Momentary right-mouse camera look should not fight generated camera cycling.
    if ( RunCameraModeUsesFlyControls( m_camera.mode, m_attachedCamera.activeFollow, m_camera.director.grabbed ) ||
         MouseLookOwnsCursor() )
    {
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    // Maintain the camera timer and cycle generated-demo camera slots.
    m_timers.cameraTimer.StopTimer();
    m_camera.cameraTime += static_cast<float>( m_timers.cameraTimer.GetElapsedTime() );
    m_timers.cameraTimer.StartTimer();
    if ( m_camera.cameraTime > 5.0f )
    {
        ++m_camera.selectedCamera;
        if ( m_camera.selectedCamera == 3 )
        {
            m_camera.selectedCamera = 0;
        }
        m_camera.cameraTime = 0.0f;
    }

    switch ( m_camera.selectedCamera )
    {
    case 0:
        m_sceneController.Cameras().SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_sceneController.Cameras().SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_sceneController.Cameras().SelectCamera( CAMERA_FREE, true );
        break;
    }

    // Object-follow cameras keep their eye fixed and retarget their view point
    // to the tracked model each frame.
    if ( m_sceneController.Cameras().IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        Vector3 targetPosition;
        if ( m_sceneController.Models().TryGetModelPosition( 0, targetPosition ) )
        {
            m_sceneController.Cameras().SetViewCoordinates( targetPosition );
        }
    }
    if ( m_sceneController.Cameras().IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        Vector3 targetPosition;
        if ( m_sceneController.Models().TryGetModelPosition( 1, targetPosition ) )
        {
            m_sceneController.Cameras().SetViewCoordinates( targetPosition );
        }
    }
}


void Run::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_sceneController.Cameras().IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_sceneController.Cameras().GetCameraTranslation( hash );
        const float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            m_config.minCameraHeight;
        m_sceneController.Cameras().RelativeUpdate( hash, minY, m_config.maxCameraHeight );
    }
}
