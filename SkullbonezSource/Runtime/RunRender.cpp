/*
File: SkullbonezSource/Runtime/RunRender.cpp
Purpose:
  Sequences the application shell's camera update and one RuntimeRenderer frame.

Summary:
  Run finalizes camera state and sequences replay-owned presentation commands
  into an immutable visual packet. RuntimeRenderer owns pass order, backend
  resources, and submission of those already-published values.

Glossary:
  Render frame view: One-frame borrowed values consumed synchronously by
    RuntimeRenderer.
  Replay visual packet: Read-only tracer spans and metadata published after all
    replay overlay producers finish for the frame.
  Attached target: Stable scene selection followed by the attached camera.

Invariants:
  - Camera selection is finalized before render views are sampled.
  - Replay pose substitution and overlay publication finish before renderer
    submission; RuntimeRenderer cannot reach replay business authority.
  - Run performs top-level sequencing only; render passes never call back into
    Run or receive a Run pointer.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "Run.h"
#include "RuntimeOverlayDiagnostics.h"
#include "../Core/Profiler.h"
#include "Allocation/RuntimeAllocationTracker.h"
#include "Allocation/RuntimeReserveAllocator.h"
#include "RuntimeTuning.h"
#include "../UI/UI.h"

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::RunInternal;
using SkullbonezCore::Math::Vector::Vector3;
namespace RuntimeAllocation = SkullbonezCore::Runtime::Allocation;


void Run::Render( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha )
{
    const RunDebugState debug = m_overlayDiagnostics->PresentationSnapshot();
    m_renderer.SetUiTextRayTracingCapability( nullptr );

    // In text_only mode all 3D rendering is skipped. UiTextPass handles the display.
    if ( debug.isTextOnly )
    {
        return;
    }

    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    m_camera.UpdateViewingOrientation( m_timers,
                                       m_sceneController.Cameras(),
                                       m_sceneController,
                                       replayInput.inspectionCameraActive,
                                       m_sceneController.State().isSceneMode,
                                       m_attachedCamera.State().activeFollow,
                                       m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook,
                                       presentationAlpha );

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_sceneController.Cameras().SetCamera();

    const SkullbonezCore::Core::CinematicRenderConfig& activeCinematic =
        ActiveSceneCinematicConfig( m_sceneController.State(), m_config );
    const bool cinematicRequested =
        IsSceneCinematicRenderingEnabled( m_sceneController.State(), m_config, m_launchOptions, debug, true );
    int attachedTargetIndex = -1;
    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        (void)m_attachedCamera.ResolveTargetIdentity( m_sceneController, attachedTargetIndex );
    }
    const float rayLinger = (std::max)( 0.0f, debug.physicsDebugContactLinger );
    const bool editorOverlayWorkVisible =
        m_runtimeTools.HasLingeredRayCastLine( rayLinger ) ||
        m_runtimeTools.HasSelectionOverlayWork( renderModels.modelCount, m_camera.mode ) ||
        m_runtimeTools.HasMousePickupOverlayWork( m_interaction.Gesture() ) || replayInput.hasPathTarget ||
        replayInput.hasCameraFocus ||
        ( replayInput.velocityEditEnabled && !m_runtimeTools.Editor().editorModeEnabled ) ||
        m_runtimeTools.HasLauncherShots();
    const RenderToolOverlayView toolOverlay{
        m_runtimeTools,
        editorOverlayWorkVisible,
        m_runtimeTools.InspectGizmoInteractionActive( m_camera.mode, replayInput.inspectionActive ),
        m_inputRouter.RuntimeSnapshot().pointer.controlDown,
        attachedTargetIndex,
        m_attachedCamera.State().activeFollow };
    const RuntimeRenderFramePolicy framePolicy =
        m_overlayDiagnostics->BuildFramePolicy( m_timers.simulationTimer.GetTimeSinceLastStart(),
                                                m_timers.simulationTimer.GetTotalTime() );

    const bool renderReady = m_renderBackendView.renderCommands && m_renderBackendView.renderResources &&
                             m_renderBackendView.renderDiagnostics;
    if ( !renderReady )
    {
        m_replayRuntime.CancelRenderFrame( m_runtimeTools );
        return;
    }

    // Invariant: Run owns the cross-domain ordering. Model interpolation must
    // finish before replay substitutes read-only historical/future poses, and
    // every overlay producer must finish before the packet is published once.
    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_sceneController.PrepareRenderInstances( presentationAlpha );
    PROFILE_END( "Frame/Render/PrepareModels" );

    m_runtimeTools.PrepareOverlayTrace( m_sceneController,
                                        m_assets,
                                        ToolOverlayBuildInput{ framePolicy.physicsDebugContactLinger,
                                                               toolOverlay.inspectGizmoInteractionActive,
                                                               toolOverlay.controlDown,
                                                               m_interaction.Gesture(),
                                                               toolOverlay.attachedTargetIndex,
                                                               toolOverlay.attachedFollow } );
    const uint64_t replayGrowthEventCount = RuntimeAllocation::RuntimeReserveAllocator::GrowthEventCount();
    const bool debugTransparentBodyPass = debug.isPhysicsDebugTransparent && debug.physicsDebugAlpha < 1.0f;
    const ReplayRenderFrameView replayFrame =
        m_replayRuntime.PrepareRenderFrame( m_sceneController.MutableRenderInstances(),
                                            m_sceneController.RenderPresentationRecords(),
                                            m_sceneController.Physics(),
                                            m_sceneController.Entities(),
                                            m_runtimeTools,
                                            m_runtimeTools.EditorTracer(),
                                            renderModels.modelCount,
                                            m_runtimeTools.Editor().editorModeEnabled,
                                            m_interaction.Gesture(),
                                            m_sceneController.State().currentFrame,
                                            debug.isCollisionVisualizer,
                                            debugTransparentBodyPass,
                                            m_sceneController.Cameras().GetRenderCameraTranslation(),
                                            m_sceneController.Cameras().GetRenderCameraUp(),
                                            replayGrowthEventCount );
    const RenderReplayOverlayView replayOverlay{ replayFrame };
    const bool replayPredictionEnabled = replayFrame.predictionEnabled;
    const bool replaySubmissionRendered =
        m_renderer.RenderFrameEntry( RuntimeRenderer::FrameEntryContext{ m_renderBackendView,
                                                                         renderModels,
                                                                         *m_operatorUi,
                                                                         framePolicy,
                                                                         replayOverlay,
                                                                         toolOverlay,
                                                                         activeCinematic,
                                                                         presentationAlpha,
                                                                         cinematicRequested,
                                                                         replayPredictionEnabled } );
    m_replayRuntime.CompleteRenderFrame( replaySubmissionRendered,
                                         m_sceneController.State().currentFrame,
                                         replayGrowthEventCount,
                                         m_runtimeTools );
}
