/*
File: SkullbonezSource/Runtime/App/RunRender.cpp
Purpose:
  Sequences the application shell's camera update and one RuntimeRenderer frame.

Summary:
  Run finalizes camera state and sequences already-published Replay and
  Planning presentation values into one immutable frame view. RuntimeRenderer
  owns pass order, backend resources, and submission of those detached values.

Glossary:
  Render frame view: One-frame borrowed values consumed synchronously by
    RuntimeRenderer.
  Replay visual packet: Read-only tracer spans and metadata published after all
    replay overlay producers finish for the frame.
  Retained geometry packet: Feature-neutral ribbons and line markers whose
    producing Planning owner has already fixed sampling, colors, and coherence.
  Attached target: Stable scene selection followed by the attached camera.

Invariants:
  - Camera selection is finalized before render views are sampled.
  - Replay pose substitution and overlay publication finish before renderer
    submission; RuntimeRenderer cannot reach replay business authority.
  - Continuous-orbit packing completes before submission; the renderer borrows
    one coherent published bank and never reads the producer ring directly.
  - RuntimeRenderer is a mandatory process owner; Render requires it once at
    entry and never advertises a recoverable missing-renderer frame.
  - Run performs top-level sequencing only; render passes never call back into
    Run or receive a Run pointer.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
*/
#include "Run.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../../Core/Profiler.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../../UI/UI.h"

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

RuntimeRenderFramePolicy Run::ProjectRenderFramePolicy( const RuntimeOverlayFramePolicy& overlay )
{
    RuntimeRenderFramePolicy policy;
    policy.textOnly = overlay.textOnly;
    policy.terrainHidden = overlay.terrainHidden;
    policy.collisionVisualizer = overlay.collisionVisualizer;
    policy.physicsDebugTransparent = overlay.physicsDebugTransparent;
    policy.physicsDebugAlpha = overlay.physicsDebugAlpha;
    policy.waterHidden = overlay.waterHidden;
    policy.waterFlatDebug = overlay.waterFlatDebug;
    policy.waterNoReflect = overlay.waterNoReflect;
    policy.waterRTReflect = overlay.waterRTReflect;
    policy.waterFreezeDebug = overlay.waterFreezeDebug;
    policy.frozenWaterTime = overlay.frozenWaterTime;
    policy.broadphaseOverlay = overlay.broadphaseOverlay;
    policy.physicsDebugFlags = overlay.physicsDebugFlags;
    policy.physicsDebugPipelineStageCursor = overlay.physicsDebugPipelineStageCursor;
    policy.physicsDebugContactLinger = overlay.physicsDebugContactLinger;
    policy.simulationSeconds = overlay.simulationSeconds;
    policy.totalSimulationSeconds = overlay.totalSimulationSeconds;
    return policy;
}

void Run::Render( const RuntimeRenderModelFrameView& renderModels, float presentationAlpha )
{
    // Fatal invariant: RuntimeRenderer is a mandatory composition owner. Require it once
    // before any render-phase state is prepared; there is no recoverable frame
    // cancellation path for a missing process renderer.
    RuntimeRenderer& renderer = Renderer( "Render" );
    const OverlayDebugState debug = m_overlayDiagnostics->PresentationSnapshot();
    renderer.ResourceLifecycle().SetUiTextDxrReflectionPreviewTexture( 0 );

    // In text_only mode all 3D rendering is skipped. UiTextPass handles the display.
    if ( debug.isTextOnly )
    {
        return;
    }

    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    const EditorOverlayFacts editorFacts = m_editorTools.BuildOverlayFacts( m_sceneController.Scene() );
    ToolEditorOverlayValues toolEditor;
    toolEditor.editorModeEnabled = editorFacts.editorModeEnabled;
    toolEditor.placementModeEnabled = editorFacts.placementModeEnabled;
    toolEditor.placementPreviewVisible = editorFacts.placementPreviewVisible;
    toolEditor.objectType = editorFacts.objectType;
    toolEditor.hotGizmoAxis = editorFacts.hotGizmoAxis;
    toolEditor.hotRotationAxis = editorFacts.hotRotationAxis;
    toolEditor.placementTerrainPoint = editorFacts.placementTerrainPoint;
    toolEditor.placementCenter = editorFacts.placementCenter;
    toolEditor.placementRayOrigin = editorFacts.placementRayOrigin;
    toolEditor.placementRayHit = editorFacts.placementRayHit;
    toolEditor.placementScale = editorFacts.placementScale;
    toolEditor.placementOrientation = editorFacts.placementOrientation;
    toolEditor.selectionCount = editorFacts.selectionCount;
    for ( std::size_t i = 0; i < editorFacts.selectionCount; ++i )
    {
        toolEditor.selectionBodies[i] = editorFacts.selectionBodies[i];
        toolEditor.selectionColliders[i] = editorFacts.selectionColliders[i];
    }

    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    m_camera.UpdateViewingOrientation( m_sceneController.Scene(), replayInput.inspectionCameraActive,
                                       m_sceneController.State().isSceneMode, m_attachedCamera.State().activeFollow,
                                       m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook,
                                       presentationAlpha, m_profiler );

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_sceneController.Scene().Cameras().SetCamera();

    const SkullbonezCore::Core::CinematicRenderConfig&
        activeCinematic = ActiveSceneCinematicConfig( m_sceneController.State(), m_config );

    const bool cinematicRequested = IsSceneCinematicRenderingEnabled( m_sceneController.State(), m_config, m_launchOptions,
                                                                       debug.isTextOnly, true );

    int attachedTargetIndex = -1;

    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        (void)m_attachedCamera.ResolveTargetIdentity( m_sceneController.Scene(), attachedTargetIndex );
    }

    const float rayLinger = (std::max)( 0.0f, debug.physicsDebugContactLinger );
    const bool editorOverlayWorkVisible = m_runtimeTools.HasLingeredRayCastLine( rayLinger ) ||
                                          m_runtimeTools.HasSelectionOverlayWork( toolEditor, renderModels.modelCount,
                                                                                  m_camera.mode ) ||
                                          m_runtimeTools.HasMousePickupOverlayWork( m_interaction.Gesture() ) ||
                                          replayInput.hasPathTarget || replayInput.hasCameraFocus ||
                                          ( replayInput.velocityEditEnabled &&
                                            !m_editorTools.Editor().editorModeEnabled ) ||
                                          m_runtimeTools.HasLauncherShots();

    const RenderToolOverlayView toolOverlay { m_runtimeTools,
                                              editorOverlayWorkVisible,
                                              m_editorTools.InspectGizmoInteractionActive( m_camera.mode,
                                                                                            replayInput.inspectionActive ),
                                              m_inputRouter.RuntimeSnapshot().pointer.controlDown,
                                              attachedTargetIndex,
                                              m_attachedCamera.State().activeFollow };

    const RuntimeRenderFramePolicy framePolicy =
        ProjectRenderFramePolicy( m_overlayDiagnostics->BuildFramePolicy( m_timers.SceneElapsedSeconds(),
                                                                           m_timers.SimulationTotalSeconds() ) );

    // Invariant: Run owns the cross-domain ordering. Model interpolation must
    // finish before replay substitutes read-only historical/future poses, and
    // every overlay producer must finish before the packet is published once.
    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_sceneController.Scene().PrepareRenderInstances( presentationAlpha );
    PROFILE_END( "Frame/Render/PrepareModels" );

    m_runtimeTools.PrepareOverlayTrace( m_sceneController.Scene(), toolEditor,
                                        ToolOverlayBuildInput { framePolicy.physicsDebugContactLinger,
                                                                toolOverlay.inspectGizmoInteractionActive,
                                                                toolOverlay.controlDown, m_interaction.Gesture(),
                                                                toolOverlay.attachedTargetIndex,
                                                                toolOverlay.attachedFollow } );
    m_editorTools.AppendPlacementGhost( m_runtimeTools.Tracer(), m_assets );

    const uint64_t replayGrowthEventCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventCount();
    const bool debugTransparentBodyPass = debug.isPhysicsDebugTransparent && debug.physicsDebugAlpha < 1.0f;
    const ReplayFrameSelection replaySelection = m_replayRuntime
                                                     .ApplyRenderPose( m_sceneController.Scene().MutableRenderInstances(),
                                                                       m_sceneController.Scene().Physics(), m_runtimeTools );

    m_replayRuntime.PrepareRenderOverlay( m_sceneController.Scene().Physics(), m_sceneController.Scene().Entities(),
                                          m_runtimeTools.Tracer(), m_config.ordinaryRender.replayTrajectory,
                                          m_editorTools.Editor().editorModeEnabled, m_interaction.Gesture(),
                                          m_sceneController.State().currentFrame,
                                          m_sceneController.Scene().RenderPresentationRecords() );

    m_replayRuntime.PublishRenderPacket( m_runtimeTools.Tracer(),
                                         m_sceneController.Scene().Cameras().GetRenderCameraTranslation(),
                                         m_sceneController.Scene().Cameras().GetRenderCameraUp(), replayGrowthEventCount );

    const ReplayRenderFrameView replayFrame = m_replayRuntime.BuildRenderFrameView( replaySelection,
                                                                                    m_sceneController.Scene().Physics(),
                                                                                    renderModels.modelCount,
                                                                                    debug.isCollisionVisualizer,
                                                                                    debugTransparentBodyPass );
    const Rendering::RetainedGeometryPacket continuousOverlay = m_continuousForecast.PreparePresentation();


    Gameplay::TornadoVisualTimeCandidates visualTime;
    visualTime.simulationSourceSeconds = framePolicy.simulationSeconds;
    visualTime.liveAdvanceHeld = replayFrame.liveAdvanceHeld;

    if ( replayFrame.presentationSample )
    {
        visualTime.hasPresentation = true;
        visualTime.presentationSeconds = replayFrame.presentationSample->simulationSeconds;
    }

    if ( replayFrame.solverSample )
    {
        visualTime.hasSolver = true;
        visualTime.solverSeconds = replayFrame.solverSample->simulationSeconds;
        visualTime.solverSystemSeconds = replayFrame.solverSample->worldSnapshot.tornadoSystemElapsedSeconds;
    }

    if ( replayFrame.predictionFrame )
    {
        visualTime.hasPrediction = true;
        visualTime.predictionSeconds = replayFrame.predictionFrame->simulationSeconds;
        visualTime.predictionSystemSeconds = replayFrame.predictionFrame->tornadoSystemElapsedSeconds;
    }

    Rendering::WorldRenderExtensionRegistration worldExtension;

    // Runtime allocation policy: Gameplay preallocates its bounded visual
    // maximum during owner construction. Steady rendering receives no
    // allocation-phase exemption.
    worldExtension = m_sceneController.Scene().Tornado().PrepareVisualFrame( visualTime );
    const bool replaySubmissionRendered = renderer.RenderFrameEntry( RuntimeRenderer::FrameEntryContext { renderModels, framePolicy, replayFrame, continuousOverlay, toolOverlay,
                                                                                                          worldExtension, activeCinematic, cinematicRequested } );

    m_replayRuntime.CompleteRenderFrame( replaySubmissionRendered, m_sceneController.State().currentFrame,
                                         replayGrowthEventCount, m_runtimeTools );
}
