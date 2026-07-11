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
#include "Run.h"
#include "../Core/Profiler.h"
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
    m_camera.UpdateViewingOrientation( m_timers,
                                       m_sceneController.Cameras(),
                                       m_sceneController,
                                       m_replayRuntime.Camera().active,
                                       m_sceneController.State().isSceneMode,
                                       m_attachedCamera.State().activeFollow,
                                       m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook );

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_sceneController.Cameras().SetCamera();

    const CinematicRenderConfig& activeCinematic = ActiveSceneCinematicConfig( m_sceneController.State(), m_config );
    const bool cinematicRequested =
        IsSceneCinematicRenderingEnabled( m_sceneController.State(), m_config, m_launchOptions, m_debug, true );
    int attachedTargetIndex = -1;
    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        (void)m_attachedCamera.ResolveTargetIdentity( m_sceneController, attachedTargetIndex );
    }
    const RenderReplayOverlayView replayOverlay{ m_replayRuntime,
                                                 m_sceneController.Entities(),
                                                 m_sceneController.State().isScenePhysics,
                                                 m_interaction.Gesture(),
                                                 m_sceneController.State().currentFrame,
                                                 m_timers.simulationTimer.GetTimeSinceLastStart(),
                                                 m_timers.simulationTimer.GetTotalTime() };
    const float rayLinger = (std::max)( 0.0f, m_debug.physicsDebugContactLinger );
    const bool editorOverlayWorkVisible =
        m_runtimeTools.HasLingeredRayCastLine( rayLinger ) ||
        m_runtimeTools.HasSelectionOverlayWork( renderModels.modelCount, m_camera.mode ) ||
        m_runtimeTools.HasMousePickupOverlayWork( m_interaction.Gesture() ) ||
        m_replayRuntime.HasPathVisualizerTarget() || m_replayRuntime.HasCameraFocus() ||
        ( m_replayRuntime.VelocityEditActive() && !m_runtimeTools.Editor().editorModeEnabled ) ||
        m_runtimeTools.HasLauncherShots();
    const RenderToolOverlayView toolOverlay{
        m_runtimeTools,
        editorOverlayWorkVisible,
        m_runtimeTools.InspectGizmoInteractionActive( m_camera.mode, m_replayRuntime.InspectionActive() ),
        m_inputRouter.RuntimeSnapshot().pointer.controlDown,
        attachedTargetIndex,
        m_attachedCamera.State().activeFollow };
    RuntimeRenderFramePolicy framePolicy;
    framePolicy.textOnly = m_debug.isTextOnly;
    framePolicy.terrainHidden = m_debug.isTerrainHidden;
    framePolicy.collisionVisualizer = m_debug.isCollisionVisualizer;
    framePolicy.physicsDebugTransparent = m_debug.isPhysicsDebugTransparent;
    framePolicy.physicsDebugAlpha = m_debug.physicsDebugAlpha;
    framePolicy.waterHidden = m_debug.isWaterHidden;
    framePolicy.waterFlatDebug = m_debug.isWaterFlatDebug;
    framePolicy.waterNoReflect = m_debug.isWaterNoReflect;
    framePolicy.waterRTReflect = m_debug.isWaterRTReflect;
    framePolicy.waterFreezeDebug = m_debug.isWaterFreezeDebug;
    framePolicy.frozenWaterTime = m_debug.frozenWaterTime;
    framePolicy.broadphaseOverlay = m_debug.isBroadphaseOverlay;
    framePolicy.physicsDebugFlags = m_debug.physicsDebugFlags;
    framePolicy.physicsDebugPipelineStageCursor = m_debug.physicsDebugPipelineStageCursor;
    framePolicy.physicsDebugContactLinger = m_debug.physicsDebugContactLinger;
    framePolicy.simulationSeconds = m_timers.simulationTimer.GetTimeSinceLastStart();
    framePolicy.totalSimulationSeconds = m_timers.simulationTimer.GetTotalTime();
    m_renderer.RenderFrameEntry( RuntimeRenderer::FrameEntryContext{ m_renderBackendView,
                                                                     renderModels,
                                                                     m_sceneController,
                                                                     m_sceneController.Physics(),
                                                                     m_UI,
                                                                     framePolicy,
                                                                     replayOverlay,
                                                                     toolOverlay,
                                                                     activeCinematic,
                                                                     cinematicRequested,
                                                                     m_replayRuntime.Prediction().enabled } );
}
