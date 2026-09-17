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
#include "../Startup/Window.h"
#include "../Diagnostics/RuntimeOverlayDiagnostics.h"
#include "../../Core/Profiler.h"
#include "../../Core/Allocation/RuntimeAllocationTracker.h"
#include "../../Core/Allocation/RuntimeReserveAllocator.h"
#include "../Scene/SceneCinematicPolicy.h"
#include "../UI/GameUI/UI.h"

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Math::Vector::Vector3;
namespace CoreAllocation = SkullbonezCore::Core::Allocation;

namespace
{
GrassFootprint RecordedGrassFootprint( const ReplayBodyPresentationSample& body )
{
    GrassFootprint footprint;
    footprint.sceneObjectId = body.id.value;
    footprint.shape = body.shapeKind == ReplayBodyShapeKind::Box ? GrassFootprintShape::Box : GrassFootprintShape::Sphere;
    footprint.halfExtents = body.shape.halfExtents;
    footprint.orientation = SkullbonezCore::Math::Orientation::Quaternion( body.orientation[0], body.orientation[1], body.orientation[2], body.orientation[3] );
    const auto rotation = footprint.orientation.GetOrientationMatrix();
    footprint.center = body.position + rotation * body.shape.localCenter;
    footprint.axes = { rotation * Vector3( 1, 0, 0 ), rotation * Vector3( 0, 1, 0 ), rotation * Vector3( 0, 0, 1 ) };
    return footprint;
}
void StampRecordedGrass( GrassPresentation& grass, const ReplayPresentationSample& sample, const ReplayPresentationSample* previous, SkullbonezCore::Geometry::Terrain& terrain )
{
    grass.SetSampleTime( sample.frameIndex, sample.physicsDt, sample.world.fluidHeight );
    for ( std::size_t row = 0; row < sample.bodies.size(); ++row )
    {
        const auto& body = sample.bodies[row];
        if ( body.shapeKind != ReplayBodyShapeKind::Sphere && body.shapeKind != ReplayBodyShapeKind::Box )
        {
            continue;
        }
        if ( !body.shape.Available() )
        {
            grass.MarkHistoryMissing();
            continue;
        }
        const auto footprint = RecordedGrassFootprint( body );
        // Capture keeps body order stable between ordinary ticks. A topology
        // change ends the sweep; matching by dense row alone is never sufficient.
        const auto* prior = previous && row < previous->bodies.size() ? &previous->bodies[row] : nullptr;
        if ( body.sweepContinuous && prior && prior->id == body.id && prior->shapeKind == body.shapeKind && prior->shape.Digest( body.id.value ) == body.shape.Digest( body.id.value ) )
        {
            const auto oldFootprint = RecordedGrassFootprint( *prior );
            grass.StampSwept( footprint, &oldFootprint, terrain );
        }
        else
        {
            grass.StampSwept( footprint, nullptr, terrain );
        }
    }
}
void ReconstructGrass( GrassPresentation& grass, ReplayRuntime& replay, const ReplayPresentationSample& target, GrassTimeCursor::Context context, SkullbonezCore::Geometry::Terrain& terrain )
{
    // Copy target metadata before reading the timeline's separate exact-frame
    // scratch. Only the last finite recovery window can contribute pressure.
    const auto selectedTick = target.frameIndex;
    const float physicsDt = target.physicsDt;
    const float waterHeight = target.world.fluidHeight;
    const auto terrainFingerprint = terrain.ContentFingerprint();
    context.recording = replay.PresentationRevision();
    context.branch = target.branch.branchId;
    grass.SetScene( context, true, waterHeight );
    const auto update = grass.SelectHistory( context, selectedTick );
    if ( update == GrassTimeCursor::Update::Unchanged )
    {
        return;
    }
    // Replay retains terrain identity, not another mutable terrain mesh. Roots
    // are reconstructible only when today's surface matches recorded evidence.
    if ( target.world.terrainFingerprint == 0 || target.world.terrainFingerprint != terrainFingerprint || !std::isfinite( physicsDt ) || physicsDt <= 0 )
    {
        grass.MarkHistoryMissing();
        return;
    }
    const auto recovery = static_cast<uint64_t>( std::ceil( grass.RecoverySeconds() / physicsDt ) );
    const auto start = update == GrassTimeCursor::Update::Advance ? selectedTick : selectedTick > recovery ? selectedTick - recovery : 0;
    const ReplayPresentationSample* previous = start > 0 ? replay.PresentationSampleAtFrame( start - 1 ) : nullptr;
    for ( auto tick = start; tick <= selectedTick; ++tick )
    {
        const auto* sample = replay.PresentationSampleAtFrame( tick );
        if ( !sample || sample->branch.branchId != context.branch || sample->physicsDt != physicsDt || sample->world.terrainFingerprint != terrainFingerprint )
        {
            grass.MarkHistoryMissing();
            previous = nullptr;
            continue;
        }
        StampRecordedGrass( grass, *sample, previous, terrain );
        previous = sample;
    }
    grass.SetSampleTime( selectedTick, physicsDt, waterHeight );
}
} // namespace

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
    policy.gravityGrid = overlay.gravityGrid;
    policy.gravityField = overlay.gravityField;
    policy.physicsDebugFlags = overlay.physicsDebugFlags;
    policy.physicsDebugPipelineStageCursor = overlay.physicsDebugPipelineStageCursor;
    policy.physicsImpulseScale = overlay.physicsImpulseScale;
    policy.physicsImpulseThreshold = overlay.physicsImpulseThreshold;
    policy.physicsDebugContactLinger = overlay.physicsDebugContactLinger;
    policy.simulationSeconds = overlay.simulationSeconds;
    policy.totalSimulationSeconds = overlay.totalSimulationSeconds;
    const auto preview = m_operatorUi->PointImpulsePreview();
    const auto& world = m_sceneController.Scene();
    const Physics::PhysicsPointImpulse request { world.BodyStore().HandleForSceneObjectId( { preview.sceneObjectId } ),
                                                 { preview.impulse[0], preview.impulse[1], preview.impulse[2] },
                                                 { preview.point[0], preview.point[1], preview.point[2] },
                                                 preview.local };
    Physics::PhysicsPointImpulseWorld resolved;
    const auto replay = m_replayRuntime.BuildInputView();
    if ( m_replayRuntime.LivePhysicsEditable() && world.Physics().ResolvePointImpulse( request, resolved ) )
    {
        policy.physicsDebugFlags |= Physics::PHYSICS_DEBUG_TEST_IMPULSE;
        policy.physicsTestImpulse = { std::array<float, 3> { resolved.center.x, resolved.center.y, resolved.center.z }, { resolved.point.x, resolved.point.y, resolved.point.z }, { resolved.impulse.x, resolved.impulse.y, resolved.impulse.z } };
    }
    return policy;
}

void Run::Render( const RuntimeRenderFrameViews& renderFrame, float presentationAlpha )
{
    // Fatal invariant: RuntimeRenderer is a mandatory composition owner. Require it once
    // before any render-phase state is prepared; there is no recoverable frame
    // cancellation path for a missing process renderer.
    RuntimeRenderer& renderer = Renderer( "Render" );
    const OverlayDebugState debug = m_overlayDiagnostics->PresentationSnapshot();
    renderer.ResourceLifecycle().SetUiTextDxrReflectionPreviewTexture( 0 );
    if ( ComparisonUiActive() && ( m_comparisonLoad.Pending() || !m_comparisonLoad.Error().empty() ) )
    {
        return;
    }
    if ( ComparisonUiActive() )
    {
        if ( m_comparison.Active() )
        {
            RenderComparison();
        }
        return;
    }

    // In text_only mode all 3D rendering is skipped. UiTextPass handles the display.
    if ( debug.isTextOnly )
    {
        return;
    }

    const ReplayInputView replayInput = m_replayRuntime.BuildInputView();
    RunEditorPlacementState& editor = m_editorTools.Editor();
    ToolEditorOverlayValues toolEditor;
    toolEditor.editorModeEnabled = editor.editorModeEnabled;
    toolEditor.placementModeEnabled = editor.placementModeEnabled;
    toolEditor.placementPreviewVisible = editor.placementPreviewVisible;
    toolEditor.velocityEditEnabled = editor.velocityEditEnabled;
    toolEditor.velocityEditAngular = editor.velocityEditAngular;
    toolEditor.objectType = editor.objectType;
    toolEditor.hotGizmoAxis = editor.hotGizmoAxis;
    toolEditor.hotRotationAxis = editor.hotRotationAxis;
    toolEditor.placementTerrainPoint = editor.placementTerrainPoint;
    toolEditor.placementCenter = editor.placementCenter;
    toolEditor.placementRayOrigin = editor.placementRayOrigin;
    toolEditor.placementRayHit = editor.placementRayHit;
    toolEditor.placementScale = editor.placementScale;
    toolEditor.placementOrientation = editor.placementOrientation;
    toolEditor.selectionCount = ProjectEditorOverlaySelection( editor, m_sceneController.Scene(), toolEditor.selectionBodies, toolEditor.selectionColliders );
    if ( editor.velocityEditEnabled && editor.selectedBody.IsValid() )
    {
        toolEditor.selectionCount = 1;
        toolEditor.selectionBodies[0] = editor.selectedBody;
        toolEditor.selectionColliders[0] = editor.selectedCollider;
    }

    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    m_camera.UpdateViewingOrientation( m_sceneController.Scene(),
                                       replayInput.inspectionCameraActive,
                                       m_sceneController.State().isSceneMode,
                                       m_attachedCamera.State().activeFollow,
                                       m_interaction.PointerCapture() == RuntimePointerCaptureOwner::CameraLook,
                                       presentationAlpha,
                                       m_profiler );

    // Selected camera state is copied into the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_sceneController.Scene().Cameras().SetCamera();

    RenderCameraLighting renderCamera;
    m_window.UpdateProjectionForCurrentClient();
    renderCamera.baseView = m_sceneController.Scene().Cameras().GetViewMatrix();
    renderCamera.projection = m_window.GetProjectionMatrix();
    renderCamera.viewProjection = renderCamera.projection * renderCamera.baseView;
    renderCamera.eye = m_sceneController.Scene().Cameras().GetRenderCameraTranslation();
    renderCamera.viewCenter = m_sceneController.Scene().Cameras().GetRenderCameraView();
    renderCamera.up = m_sceneController.Scene().Cameras().GetRenderCameraUp();

    const bool authoredRendering = IsSceneCinematicRenderingEnabled( m_sceneController.State(), m_config, m_launchOptions, debug.isTextOnly, true );
    const auto activeCinematic = renderer.ResolveCinematicLook( ActiveSceneCinematicConfig( m_sceneController.State(), m_config ), authoredRendering );
    const bool cinematicRequested = activeCinematic.enabled && !debug.isTextOnly;

    int attachedTargetIndex = -1;

    if ( RunCameraModeIsAttached( m_camera.mode ) )
    {
        (void)m_attachedCamera.ResolveTargetIdentity( m_sceneController.Scene(), attachedTargetIndex );
    }

    const float rayLinger = (std::max)( 0.0f, debug.physicsDebugContactLinger );
    const bool editorOverlayWorkVisible = m_runtimeTools.HasLingeredRayCastLine( rayLinger ) ||
                                          m_runtimeTools.HasSelectionOverlayWork( toolEditor, renderFrame.modelPresentation.modelCount, m_camera.mode ) ||
                                          m_runtimeTools.HasMousePickupOverlayWork( m_interaction.Gesture() ) || replayInput.hasPathTarget || replayInput.hasCameraFocus ||
                                          ( replayInput.velocityEditEnabled && !m_editorTools.Editor().editorModeEnabled ) || m_runtimeTools.HasLauncherShots();

    const bool inspectGizmoInteractionActive = m_editorTools.InspectGizmoInteractionActive( m_camera.mode, replayInput.inspectionActive );
    const bool controlDown = m_inputRouter.RuntimeSnapshot().pointer.controlDown;
    RenderToolOverlayView toolOverlay;
    toolOverlay.editorOverlayWorkVisible = editorOverlayWorkVisible;
    const std::span<const LauncherLaserShotSnapshot> launcherShots = m_runtimeTools.Laser().PresentationShots();
    toolOverlay.launcherShotCount = (std::min)( launcherShots.size(), toolOverlay.launcherShots.size() );

    for ( std::size_t i = 0; i < toolOverlay.launcherShotCount; ++i )
    {
        const LauncherLaserShotSnapshot& source = launcherShots[i];
        RenderToolOverlayView::LauncherShot& destination = toolOverlay.launcherShots[i];
        destination.start = source.start;
        destination.end = source.end;
        destination.cameraRight = source.cameraRight;
        destination.cameraUp = source.cameraUp;
        destination.ageSeconds = source.ageSeconds;
        destination.lifetimeSeconds = source.lifetimeSeconds;
        destination.active = source.active;
        destination.hit = source.hit;
    }

    RuntimeRenderFramePolicy framePolicy = ProjectRenderFramePolicy( m_overlayDiagnostics->BuildFramePolicy( m_timers.SceneElapsedSeconds(), m_timers.SimulationTotalSeconds() ) );

    // Invariant: Run owns the cross-domain ordering. Model interpolation must
    // finish before replay substitutes read-only historical/future poses, and
    // every overlay producer must finish before the packet is published once.
    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_sceneController.Scene().PrepareRenderInstances( presentationAlpha );
    PROFILE_END( "Frame/Render/PrepareModels" );

    m_runtimeTools.PrepareOverlayTrace( m_sceneController.Scene(), toolEditor, ToolOverlayBuildInput { framePolicy.physicsDebugContactLinger,
                                                                inspectGizmoInteractionActive,
                                                                controlDown,
                                                                m_interaction.Gesture(),
                                                                attachedTargetIndex,
                                                                m_attachedCamera.State().activeFollow } );
    m_editorTools.AppendPlacementGhost( m_runtimeTools.Tracer(), m_assets );

    const uint64_t replayGrowthEventCount = CoreAllocation::RuntimeReserveAllocator::GrowthEventCount();
    const bool debugTransparentBodyPass = debug.isPhysicsDebugTransparent && debug.physicsDebugAlpha < 1.0f;
    const ReplayFrameSelection replaySelection = m_replayRuntime.ApplyRenderPose( m_sceneController.Scene().MutableRenderInstances(), m_sceneController.Scene().Physics(), m_runtimeTools );
    renderer.UpdateGravityField( m_sceneController.Scene().MutableRenderInstances(), renderFrame.debug, framePolicy );

    m_replayRuntime.PrepareRenderOverlay( m_sceneController.Scene().Physics(),
                                          m_sceneController.Scene().Entities(),
                                          m_runtimeTools.Tracer(),
                                          m_config.ordinaryRender.replayTrajectory,
                                          m_editorTools.Editor().editorModeEnabled,
                                          m_interaction.Gesture(),
                                          m_sceneController.State().currentFrame,
                                          m_sceneController.Scene().RenderPresentationRecords() );

    m_replayRuntime.PublishRenderPacket( m_runtimeTools.Tracer(),
                                         m_sceneController.Scene().Cameras().GetRenderCameraTranslation(),
                                         m_sceneController.Scene().Cameras().GetRenderCameraUp(),
                                         replayGrowthEventCount );

    const ReplayRenderFrameViews replayFrame = m_replayRuntime.BuildRenderFrameViews( replaySelection,
                                                                                      m_sceneController.Scene().Physics(),
                                                                                      renderFrame.modelPresentation.modelCount,
                                                                                      debug.isCollisionVisualizer,
                                                                                      debugTransparentBodyPass );
    const Rendering::RetainedGeometryPacket continuousOverlay = m_continuousForecast.PreparePresentation();
    const std::string* grassScenePath = m_sceneController.CurrentPath();
    renderer.Grass().Configure( m_config.ordinaryRender.grass );
    renderer.HistoricalGrass().Configure( m_config.ordinaryRender.grass );
    framePolicy.grassEnabled = ( ( grassScenePath && grassScenePath->empty() ) || renderer.Grass().FixtureEnabled( m_sceneController.LifecyclePacket().generation ) ) &&
                               m_config.ordinaryRender.grass.quality >= .5f && !( cinematicRequested && activeCinematic.terrainReliefEnabled && activeCinematic.terrainRelief > 0 );
    // Visual-only relief moves the rendered ground away from Physics. Such a
    // surface is ineligible for rooted, collider-driven grass until those
    // surfaces share one geometry owner; ordinary Demo terrain remains enabled.
    GrassTimeCursor::Context grassContext;
    grassContext.generation = m_sceneController.LifecyclePacket().generation;
    grassContext.recording = m_replayRuntime.LiveRecordingEpoch();
    grassContext.branch = m_replayRuntime.CaptureBranchId();
    grassContext.terrainRevision = m_sceneController.Scene().Terrain().Get() ? m_sceneController.Scene().Terrain().Get()->EditRevision() : 0;
    renderer.Grass().SetScene( grassContext, framePolicy.grassEnabled, m_sceneController.Scene().Environment().GetFluidSurfaceHeight() );

    if ( framePolicy.grassEnabled && m_sceneController.Scene().Terrain().Get() )
    {
        renderer.Grass().RefreshHeld( m_sceneController.Scene().MutableRenderInstances(), m_sceneController.Scene().Colliders(), *m_sceneController.Scene().Terrain().Get() );
    }

    framePolicy.grassHistorical = replayFrame.time.presentationSample || replayFrame.time.solverSample || replayFrame.time.predictionFrame;
    framePolicy.grassHistoryAvailable = !replayFrame.time.predictionFrame;
    if ( framePolicy.grassEnabled && framePolicy.grassHistorical && m_sceneController.Scene().Terrain().Get() )
    {
        const auto* sample = replayFrame.time.presentationSample;
        if ( !sample && replayFrame.time.solverSample )
        {
            sample = m_replayRuntime.PresentationSampleAtFrame( replayFrame.time.solverSample->frameIndex );
        }
        if ( sample && !replayFrame.time.predictionFrame )
        {
            ReconstructGrass( renderer.HistoricalGrass(), m_replayRuntime, *sample, grassContext, *m_sceneController.Scene().Terrain().Get() );
        }
        else
        {
            framePolicy.grassHistoryAvailable = false;
        }
    }


    Gameplay::TornadoVisualTimeCandidates visualTime;
    visualTime.simulationSourceSeconds = framePolicy.simulationSeconds;
    visualTime.liveAdvanceHeld = replayFrame.time.liveAdvanceHeld;

    if ( replayFrame.time.presentationSample )
    {
        visualTime.hasPresentation = true;
        visualTime.presentationSeconds = replayFrame.time.presentationSample->simulationSeconds;
    }

    if ( replayFrame.time.solverSample )
    {
        visualTime.hasSolver = true;
        visualTime.solverSeconds = replayFrame.time.solverSample->simulationSeconds;
        visualTime.solverSystemSeconds = replayFrame.time.solverSample->worldSnapshot.tornadoSystemElapsedSeconds;
    }

    if ( replayFrame.time.predictionFrame )
    {
        visualTime.hasPrediction = true;
        visualTime.predictionSeconds = replayFrame.time.predictionFrame->simulationSeconds;
        visualTime.predictionSystemSeconds = replayFrame.time.predictionFrame->tornadoSystemElapsedSeconds;
    }

    Rendering::WorldRenderExtensionRegistration worldExtension;

    // Runtime allocation policy: Gameplay preallocates its bounded visual
    // maximum during owner construction. Steady rendering receives no
    // allocation-phase exemption.
    RuntimeRenderer::WorldFrameSubmission worldSubmission { renderFrame.modelPresentation,
                                                            renderFrame.debug.collision,
                                                            renderCamera,
                                                            m_sceneController.Scene().Terrain().Get(),
                                                            framePolicy,
                                                            worldExtension,
                                                            *replayFrame.render.visualPacket,
                                                            replayFrame.render.focusModelMask,
                                                            replayFrame.render.focusFadeActive,
                                                            activeCinematic,
                                                            cinematicRequested };
    const RuntimeRenderer::OverlayFrameSubmission overlaySubmission { renderFrame.debug.physics,
                                                                      renderFrame.worldExtensionDebug,
                                                                      replayFrame.render.contactPresentation,
                                                                      continuousOverlay,
                                                                      toolOverlay };
    const auto& cameras = m_sceneController.Scene().Cameras();
    const auto panes = SkullbonezCore::UI::GameLayout::EditorPaneRects( m_operatorUi->PresentationBounds().viewport );
    const auto perspective = renderCamera.projection;
    bool replaySubmissionRendered = true;
    const int paneCount = cameras.FourViews() ? 4 : 1;
    m_editorPaneOverlayRendered = {};
    // Invariant: model preparation, future simulation and packet publication
    // occur once above. Each view consumes that same immutable overlay packet.
    for ( int pane = 0; pane < paneCount; ++pane )
    {
        // The extension releases its borrowed configuration after each draw;
        // renew that borrow using the same simulation time for every pane.
        worldExtension = m_sceneController.Scene().Tornado().PrepareVisualFrame( visualTime );
        if ( cameras.FourViews() )
        {
            const auto pose = cameras.EditorPane( pane );
            renderCamera.eye = pose.eye;
            renderCamera.viewCenter = pose.focus;
            renderCamera.up = pose.up;
            renderCamera.baseView = SkullbonezCore::Math::Transformation::Matrix4::LookAt( pose.eye, pose.focus, pose.up );
            auto lens = perspective;
            lens.m[0] = lens.m[5] * panes[pane].h / panes[pane].w;
            renderCamera.projection = cameras.EditorPaneProjection( pane, lens );
            renderCamera.viewProjection = renderCamera.projection * renderCamera.baseView;
            const auto& rect = panes[pane];
            worldSubmission.viewport = { static_cast<LONG>( rect.x ), static_cast<LONG>( rect.y ), static_cast<LONG>( rect.x + rect.w ), static_cast<LONG>( rect.y + rect.h ) };
            worldSubmission.clearBackbuffer = pane == 0;
        }
        RuntimeRenderer::WorldOverlayTransaction worldOverlay = renderer.BeginWorldFrame( worldSubmission );
        m_editorPaneOverlayRendered[pane] = worldOverlay.SubmitOverlays( overlaySubmission );
        replaySubmissionRendered = m_editorPaneOverlayRendered[pane] && replaySubmissionRendered;
    }
    m_window.SetPresentationProjection( cameras.EditorPaneProjection( cameras.ActiveEditorPane(), perspective ) );

    m_replayRuntime.CompleteRenderFrame( replaySubmissionRendered, m_sceneController.State().currentFrame, replayGrowthEventCount, m_runtimeTools );
}
