/*
File: SkullbonezSource/Runtime/RunRender.cpp
Purpose:
  Coordinates render passes for the active scene.

Mental model:
  Renderer-facing code builds one frame context and runs named passes in
  the same order the image is produced.

Glossary:
  Render pass: Named slice of DrawPrimitives() with explicit inputs, outputs,
  and resource ownership.
  DXR (DirectX Raytracing): DX12 API used here for optional raytraced water
  reflection dispatch.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  HDR (High Dynamic Range): Floating-point scene color that preserves bright
  lighting until the tonemap pass resolves it to display color.
  FBO (Framebuffer Object): Engine-neutral off-screen render target wrapper.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh or procedural object owned by the DX12 backend.
  TLAS (Top-Level Acceleration Structure): Raytracing scene-instance table built
  before reflection rays are dispatched.

Invariants:
  - RuntimeRenderer::RenderFrame() owns pass order. Pass classes may bind targets and
    restore local render state, but they must not present or advance the frame.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Run.h owns the runtime state borrowed by RuntimeRenderHost.
  - SkullbonezSource/Rendering/RenderPipeline.h owns executed frame graph diagnostics.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "../Rendering/RenderPipeline.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

// The cinematic settings can come from two places:
//  1. a .scene.json file, when a test/preview scene is loaded, or
//  2. the normal engine config, when the app is running without a scene override.
// This helper hides that choice so the render code below can just ask for "the
// current cinematic look" without caring where it was authored.
CinematicRenderConfig& Run::ActiveCinematicConfig()
{
    return SceneState().isSceneMode ? SceneState().cinematicRender : Cfg().cinematicRender;
}


const CinematicRenderConfig& Run::ActiveCinematicConfig() const
{
    return SceneState().isSceneMode ? SceneState().cinematicRender : Cfg().cinematicRender;
}


bool Run::IsCinematicRenderingEnabled() const
{
    // Command line switches win over config/scene values. That lets us launch
    // the same scene in plain mode or cinematic mode while debugging.
    const bool enabled = m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : ActiveCinematicConfig().enabled;

    // Text-only mode deliberately skips all 3D rendering, so cinematic mode must
    // also stay off there. The UI text renderer handles that path by itself.
    return enabled && IsGfxReady() && !m_debug.isTextOnly;
}


void Run::ApplyReplayRenderStateForFrame()
{
    if ( const RunReplayPredictionFrame* predictionFrame = CurrentReplayPredictionScrubFrame() )
    {
        m_replayRuntime.ApplyPredictionFrameForRender( m_cGameModelCollection, *predictionFrame );
    }
    else if ( const ReplayPresentationSample* replaySample = CurrentReplayScrubSample() )
    {
        m_replayRuntime.ApplyPresentationSampleForRender( m_cGameModelCollection, *replaySample );
    }
    else if ( const ReplaySolverFrameSample* solverSample = CurrentReplaySolverScrubSample() )
    {
        m_replayRuntime.ApplySolverSampleForRender( m_cGameModelCollection, *solverSample );
        ApplyReplayLauncherVisualSampleForRender( solverSample->launcherVisual );
    }
}


void Run::RestoreReplayRenderStateForFrame()
{
    m_replayRuntime.RestoreRenderPose( m_cGameModelCollection );
    RestoreReplayLauncherVisualForRender();
}


void Run::RenderReplayPredictionGhosts( const RenderFrameContext& frame,
                                        const CinematicRenderConfig* cinematic,
                                        const Rendering::ShadowFrameData* shadow )
{
    PROFILE_SCOPED( "Frame/Render/ReplayPredictionGhosts" );
    if ( !m_replayPrediction.enabled || !m_replayPrediction.ragdollVisualsEnabled ||
         m_replayPrediction.frames.size() < 2 )
    {
        return;
    }

    const std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.Models();
    bool hasRagdollPart = false;
    for ( const GameObjects::GameModel& model : models )
    {
        if ( ReplayModelIsRagdollPart( model ) )
        {
            hasRagdollPart = true;
            break;
        }
    }
    if ( !hasRagdollPart )
    {
        return;
    }

    SelectRenderTexture( TEXTURE_BOUNDING_SPHERE );
    const std::size_t lastIndex = m_replayPrediction.frames.size() - 1;
    const std::size_t stride =
        (std::max)( static_cast<std::size_t>( 1 ),
                    ( lastIndex + REPLAY_PREDICTION_GHOST_MAX_FRAMES - 1 ) / REPLAY_PREDICTION_GHOST_MAX_FRAMES );
    const ReplayFrameIndex lastFrame = m_replayPrediction.frames.back().frameIndex;

    RenderHelper::DrawBoxBatchBegin( frame.baseView,
                                     frame.projection,
                                     frame.lightPosition,
                                     true,
                                     cinematic,
                                     shadow,
                                     1.0f );

    auto appendGhostFrame = [&]( std::size_t index )
    {
        const RunReplayPredictionFrame& predictionFrame = m_replayPrediction.frames[index];
        if ( predictionFrame.frameIndex == 0 )
        {
            return;
        }

        const float t =
            lastFrame > 0
                ? std::clamp( static_cast<float>( predictionFrame.frameIndex ) / static_cast<float>( lastFrame ),
                              0.0f,
                              1.0f )
                : 1.0f;
        const float alpha = std::clamp( 0.055f + ( 1.0f - t ) * 0.105f, 0.045f, 0.18f );

        for ( const RunReplayPredictionBodySample& body : predictionFrame.bodies )
        {
            if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
            {
                continue;
            }

            const GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
            if ( model.GetReplayBodyId() != body.id.value || !ReplayModelIsRagdollPart( model ) )
            {
                continue;
            }

            const BoundingBox* box = std::get_if<BoundingBox>( &model.GetCollisionShape() );
            if ( !box )
            {
                continue;
            }

            Math::Orientation::Quaternion orientation = body.orientation;
            orientation.Normalise();
            Rendering::RenderMaterial material = model.GetRenderMaterial();
            material.baseColor[3] = alpha;
            const Matrix4 modelMatrix = box->GetModelMatrix( body.position, Matrix4::FromQuaternion( orientation ) );
            RenderHelper::DrawBoxBatchModel( modelMatrix, material );
        }
    };

    std::size_t farIndex = lastIndex;
    if ( farIndex % stride != 0 )
    {
        appendGhostFrame( farIndex );
        farIndex = ( farIndex / stride ) * stride;
    }
    for ( std::size_t index = farIndex; index >= stride; index -= stride )
    {
        appendGhostFrame( index );
        if ( index == stride )
        {
            break;
        }
    }

    RenderHelper::DrawBoxBatchEnd();
}


void Run::ApplyReplayLauncherVisualSampleForRender( const ReplayLauncherVisualSample& sample )
{
    if ( m_replayRuntime.HasLauncherVisualBackup() )
    {
        return;
    }

    ReplayLauncherVisualSample liveSample;
    BuildReplayLauncherVisualSample( liveSample );
    m_replayRuntime.StoreLauncherVisualBackup( liveSample );
    RestoreReplayLauncherVisualSample( sample );
}


void Run::RestoreReplayLauncherVisualForRender()
{
    if ( !m_replayRuntime.HasLauncherVisualBackup() )
    {
        return;
    }

    RestoreReplayLauncherVisualSample( m_replayRuntime.LauncherVisualBackup() );
    m_replayRuntime.ClearLauncherVisualBackup();
}


RuntimeRenderServices Run::BuildRuntimeRenderServices()
{
    return RuntimeRenderServices{ *m_systems.textures,
                                  m_cGameModelCollection,
                                  m_cWorldEnvironment,
                                  m_systems.terrain.get(),
                                  *m_systems.cameras,
                                  *m_systems.window,
                                  m_UI,
                                  m_systems.skyBox };
}


RuntimeRenderInputs Run::BuildRuntimeRenderInputs()
{
    return RuntimeRenderInputs{ BuildRuntimeRenderServices() };
}


RenderFrameContext RuntimeRenderer::BuildRenderFrameContext( const RuntimeRenderInputs& renderInputs,
                                                             bool cinematicRender,
                                                             const CinematicRenderConfig& renderConfig ) const
{
    const RuntimeRenderServices& services = renderInputs.services;
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;
    frame.scene = &services.models;

    // Ordinary and cinematic rendering both use a directional sun (w = 0).
    // Keeping one sun-vector contract makes direct BRDF lighting and shadow-map
    // visibility block the same light contribution.
    if ( frame.cinematicEnabled )
    {
        const Vector3 sunDirection = CinematicSkySunDirection( renderConfig );
        frame.lightPosition[0] = sunDirection.x;
        frame.lightPosition[1] = sunDirection.y;
        frame.lightPosition[2] = sunDirection.z;
        frame.lightPosition[3] = 0.0f;
    }

    // Invariant: build the pass context after SetCamera(). During camera
    // transitions, the selected camera and render camera can differ; all passes
    // must consume the interpolated render camera so reflection, sky, and water
    // sample the same view.
    frame.baseView = services.cameras.GetViewMatrix();
    frame.projection = services.window.GetProjectionMatrix();
    frame.viewProjection = frame.projection * frame.baseView;
    frame.eye = services.cameras.GetRenderCameraTranslation();
    frame.viewCenter = services.cameras.GetRenderCameraView();
    frame.up = services.cameras.GetRenderCameraUp();
    frame.waterY = services.world.GetFluidSurfaceHeight();
    frame.reflectionEye = Vector3( frame.eye.x, 2.0f * frame.waterY - frame.eye.y, frame.eye.z );
    frame.reflectionCenter =
        Vector3( frame.viewCenter.x, 2.0f * frame.waterY - frame.viewCenter.y, frame.viewCenter.z );
    frame.reflectionUp = Vector3( frame.up.x, -frame.up.y, frame.up.z );
    frame.reflectionView = Matrix4::LookAt( frame.reflectionEye, frame.reflectionCenter, frame.reflectionUp );
    frame.reflectionViewProjection = frame.projection * frame.reflectionView;
    return frame;
}


RuntimeRenderer::RuntimeRenderer( RuntimeRenderHost& host )
    : m_host( host ), m_fullscreenQuadPass( host ), m_skyPass( host ), m_sceneTargetPass( host ), m_shadowPass( host ),
      m_reflectionPass( host ), m_objectPass( host ), m_terrainPass( host ), m_waterPass( host ),
      m_tornadoVisualPass( host ), m_debugOverlayPass( host ), m_volumetricPass( host ), m_tonemapPass( host ),
      m_uiTextPass( host )
{
}


void RuntimeRenderer::EnsureFrameResources( const RuntimeRenderInputs& renderInputs,
                                            bool cinematicRender,
                                            const CinematicRenderConfig& renderConfig )
{
    if ( cinematicRender )
    {
        // Lifetime: cinematic resources are lazy. A window resize or backend
        // rebuild drops them; the next cinematic frame recreates the targets and
        // shader objects with the current window dimensions.
        RenderFrameContext preFrame = BuildRenderFrameContext( renderInputs, cinematicRender, renderConfig );
        m_fullscreenQuadPass.EnsureGpuResources( preFrame );
        m_skyPass.EnsureGpuResources( preFrame );
        m_sceneTargetPass.EnsureGpuResources( preFrame );
        m_volumetricPass.EnsureGpuResources( preFrame );
        m_tonemapPass.EnsureGpuResources( preFrame );
    }
}


void RuntimeRenderer::RenderFrame( const RuntimeRenderInputs& renderInputs )
{
    RuntimeRenderHost& host = m_host;
    const bool cinematicRender = host.IsCinematicRenderingEnabled();
    const CinematicRenderConfig& renderConfig = host.ActiveCinematicConfig();
    const OrdinaryRenderConfig& ordinaryRender = Cfg().ordinaryRender;
    CinematicRenderConfig ordinaryShadowConfig = renderConfig;
    ordinaryShadowConfig.shadowsEnabled = ordinaryRender.shadowsEnabled;
    ordinaryShadowConfig.shadowTerrainCasts = ordinaryRender.shadowTerrainCasts;
    ordinaryShadowConfig.shadowObjectsCast = ordinaryRender.shadowObjectsCast;
    ordinaryShadowConfig.shadowTerrainReceives = ordinaryRender.shadowTerrainReceives;
    ordinaryShadowConfig.shadowObjectsReceive = ordinaryRender.shadowObjectsReceive;
    ordinaryShadowConfig.shadowMapSize = ordinaryRender.shadowMapSize;
    ordinaryShadowConfig.shadowPcfRadius = ordinaryRender.shadowPcfRadius;
    ordinaryShadowConfig.shadowStrength = ordinaryRender.shadowStrength;
    ordinaryShadowConfig.shadowSoftness = ordinaryRender.shadowSoftness;
    ordinaryShadowConfig.shadowDepthBias = ordinaryRender.shadowDepthBias;
    ordinaryShadowConfig.shadowSlopeBias = ordinaryRender.shadowSlopeBias;
    ordinaryShadowConfig.shadowMaxDistance = ordinaryRender.shadowMaxDistance;
    const CinematicRenderConfig& activeShadowStyle = cinematicRender ? renderConfig : ordinaryShadowConfig;
    const bool shadowMapsEnabled = activeShadowStyle.shadowsEnabled && IsGfxReady() && !host.m_debug.isTextOnly;

    EnsureFrameResources( renderInputs, cinematicRender, renderConfig );

    const bool useCinematicTarget = cinematicRender && m_sceneTargetPass.IsReady();
    if ( cinematicRender && !useCinematicTarget )
    {
        // If the cinematic target could not be created, fall back to the normal
        // backbuffer clear so the frame still renders instead of showing stale data.
        Gfx().Clear( true, true );
    }

    // Build the shared pass contract once, after camera update and before any
    // pass can bind targets. All extracted passes consume this same frame view.
    RenderFrameContext frame = BuildRenderFrameContext( renderInputs, cinematicRender, renderConfig );

    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    host.m_cGameModelCollection.PrepareRenderStreams();
    PROFILE_END( "Frame/Render/PrepareModels" );

    // These passes currently borrow subsystem-owned mesh/material resources,
    // but keeping the ensure calls in the frame story gives future extraction
    // work an obvious place to move those GPU resources.
    m_objectPass.EnsureGpuResources( frame );
    m_terrainPass.EnsureGpuResources( frame );
    m_waterPass.EnsureGpuResources( frame );
    m_tornadoVisualPass.EnsureGpuResources( frame );
    m_debugOverlayPass.EnsureGpuResources( frame );

    // Defer the first DX12 command-list open until after CPU-side model prep so
    // allocator waits do not block work that can overlap the previous frame.
    if ( !cinematicRender )
    {
        Gfx().Clear( true, true );
    }

    const CinematicRenderConfig* activeCinematic = frame.cinematic;
    const CinematicRenderConfig* activeShadowConfig = shadowMapsEnabled ? &activeShadowStyle : nullptr;
    ShadowPassOutput shadowPass = m_shadowPass.Render( { frame, activeShadowConfig } );
    const Rendering::ShadowFrameData* terrainShadowFrame = shadowPass.terrainShadow;
    const Rendering::ShadowFrameData* objectShadowFrame = shadowPass.objectShadow;

    const bool collisionStateColorsVisible = host.m_debug.isCollisionVisualizer;
    const bool debugTransparentBodyPass =
        host.m_debug.isPhysicsDebugTransparent && host.m_debug.physicsDebugAlpha < 1.0f;
    const bool replayPredictionOverlayActive = host.m_replayPrediction.enabled;
    const bool replayFocusFadeActive = !replayPredictionOverlayActive && !collisionStateColorsVisible &&
                                       !debugTransparentBodyPass && host.BuildReplayFocusModelMask();
    const std::vector<uint8_t>* replayFocusModelMask = replayFocusFadeActive ? &host.m_replayFocusModelMask : nullptr;
    const bool transparentBodyPass = debugTransparentBodyPass || replayFocusFadeActive;
    const float bodyRenderAlpha = debugTransparentBodyPass ? host.m_debug.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = debugTransparentBodyPass ? bodyRenderAlpha : -1.0f;
    const bool waterModeOff = frame.cinematicEnabled && activeCinematic && activeCinematic->waterMode == 0;
    const bool waterVisibleThisFrame = !host.m_debug.isWaterHidden && !waterModeOff;
    const bool reflectionPassNeeded = waterVisibleThisFrame && !host.m_debug.isWaterNoReflect;

    // Invariant: sky and reflection both consume the interpolated render camera
    // from RenderFrameContext. Using the selected destination camera here would
    // stretch reflected geometry during camera transitions.
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/Render/Skybox" );
            m_skyPass.Render( frame, frame.baseView, SkyPassMode::CubemapOnly );
        }
        PROFILE_GPU_END( "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection;
    reflection.reflectionSampleViewProjection = frame.reflectionViewProjection;
    if ( reflectionPassNeeded )
    {
        m_reflectionPass.EnsureGpuResources( frame );
        reflection = m_reflectionPass.Render( { frame,
                                                activeCinematic,
                                                objectShadowFrame,
                                                collisionStateColorsVisible,
                                                debugTransparentBodyPass,
                                                collisionVisualizerAlphaOverride,
                                                bodyRenderAlpha },
                                              m_skyPass );
    }

    if ( useCinematicTarget )
    {
        m_sceneTargetPass.Begin( frame, m_skyPass );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    if ( !debugTransparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Opaque,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               1.0f,
                               replayFocusModelMask,
                               true } );
    }

    // Terrain receives the broad shadow frame and provides the main world depth
    // that cinematic post passes read later.
    m_terrainPass.Render( { frame, activeCinematic, terrainShadowFrame } );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    m_waterPass.Render( { frame,
                          reflection,
                          activeCinematic,
                          host.m_debug.isWaterHidden,
                          host.m_debug.isWaterFlatDebug,
                          host.m_debug.isWaterNoReflect,
                          host.m_debug.isWaterFreezeDebug,
                          host.m_debug.frozenWaterTime } );

    const bool tornadoVisualRendered = m_tornadoVisualPass.Render( { frame } );

    if ( debugTransparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Transparent,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               bodyRenderAlpha,
                               nullptr,
                               true } );
    }
    else if ( replayFocusFadeActive )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Transparent,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               0.5f,
                               replayFocusModelMask,
                               false } );
    }

    host.RenderReplayPredictionGhosts( frame, activeCinematic, objectShadowFrame );

    m_debugOverlayPass.Render( { frame } );

    bool volumetricReady = false;
    if ( useCinematicTarget )
    {
        volumetricReady = m_volumetricPass.Render( frame );
        m_tonemapPass.Render( frame, volumetricReady, volumetricReady );
    }

    Rendering::RenderSceneSnapshot frameSnapshot;
    frameSnapshot.cinematicRender = cinematicRender;
    frameSnapshot.useCinematicTarget = useCinematicTarget;
    frameSnapshot.terrainShadowValid = terrainShadowFrame && terrainShadowFrame->valid;
    frameSnapshot.objectShadowValid = objectShadowFrame && objectShadowFrame->valid;
    frameSnapshot.reflectionUsedDxr = reflection.usedDxr;
    frameSnapshot.objectOpaquePass = !debugTransparentBodyPass;
    frameSnapshot.objectTransparentPass = transparentBodyPass;
    frameSnapshot.terrainPassRendered = !host.m_debug.isTerrainHidden;
    const WaterPassDebugInfo& waterDebug = m_waterPass.LastDebugInfo();
    frameSnapshot.waterPassRendered = waterDebug.rendered;
    frameSnapshot.waterSamplesReflection =
        waterDebug.rendered && !waterDebug.noReflection && waterDebug.reflectionValid;
    frameSnapshot.tornadoVisualRendered = tornadoVisualRendered;
    frameSnapshot.volumetricReady = volumetricReady;
    Rendering::RenderPipeline::DumpExecutedFrameGraphIfChanged( frameSnapshot );
}


void RuntimeRenderer::ReleaseBackendOwnedResources()
{
    // Lifetime: release pass-owned GPU resources while the renderer backend is
    // still alive. The order keeps consumers ahead of their producers, so cached
    // handles are invalidated before targets die.
    m_tonemapPass.ReleaseGpuResources();
    m_volumetricPass.ReleaseGpuResources();
    m_tornadoVisualPass.ReleaseGpuResources();
    m_sceneTargetPass.ReleaseGpuResources();
    m_shadowPass.ReleaseGpuResources();
    m_reflectionPass.ReleaseGpuResources();
    m_skyPass.ReleaseGpuResources();
    m_fullscreenQuadPass.ReleaseGpuResources();
    m_uiTextPass.ReleaseGpuResources();
}


void RuntimeRenderer::EnsureUiTextResources()
{
    m_uiTextPass.EnsureGpuResources();
}


bool RuntimeRenderer::ShouldRenderUiText() const
{
    return m_uiTextPass.ShouldRender();
}


void RuntimeRenderer::RenderUiText( double dSecondsPerFrame )
{
    m_uiTextPass.Render( dSecondsPerFrame );
}


void Run::Render()
{
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
    m_systems.cameras->SetCamera();

    ApplyReplayRenderStateForFrame();

    m_renderer.RenderFrame( BuildRuntimeRenderInputs() );
    RestoreReplayRenderStateForFrame();
}


void Run::DrawPrimitives()
{
    m_renderer.RenderFrame( BuildRuntimeRenderInputs() );
}


void Run::SetUpCameras()
{
    SceneGeneratedSetup::SetUpCameras( BuildSceneGeneratedCameraContext() );
}


void Run::RebuildRegisteredRenderResources()
{
    enum class RebuildStep
    {
        ResetHelperCache,
        RegisterBuiltInSources,
        RebuildTextures
    };

    struct RebuildPhase
    {
        const char* name;
        RebuildStep step;
    };

    const RebuildPhase rebuildSteps[] = {
        { "reset_helper_cache", RebuildStep::ResetHelperCache },
        { "register_builtin_source_records", RebuildStep::RegisterBuiltInSources },
        { "rebuild_textures_from_source_assets", RebuildStep::RebuildTextures },
    };

    for ( const RebuildPhase& phase : rebuildSteps )
    {
        LogRenderResourceLifecycleStep( "backend_rebuild", phase.name );
        switch ( phase.step )
        {
        case RebuildStep::ResetHelperCache:
            RenderHelper::ResetRenderResources();
            break;
        case RebuildStep::RegisterBuiltInSources:
            RegisterBuiltInAssets();
            break;
        case RebuildStep::RebuildTextures:
            // Recreate backend texture handles from stable source asset records.
            m_systems.textures->RebuildTexturesFromSourceAssets();
            break;
        }
    }
}


void Run::SetViewingOrientation()
{
    if ( m_replayCamera.active )
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

    // In fly mode, freeze the cycle clock and keep the free camera
    if ( IsFlyCameraMode() )
    {
        m_camera.cameraTime = 0.0f;
        m_timers.cameraTimer.StopTimer();
        m_timers.cameraTimer.StartTimer();
        return;
    }

    /*
        if(Input::IsKeyDown('1')) m_camera.selectedCamera = 0;
        if(Input::IsKeyDown('2')) m_camera.selectedCamera = 1;
        if(Input::IsKeyDown('3')) m_camera.selectedCamera = 2;
    */

    // maintain the camera timer
    m_timers.cameraTimer.StopTimer();
    m_camera.cameraTime += static_cast<float>( m_timers.cameraTimer.GetElapsedTime() );
    m_timers.cameraTimer.StartTimer();

    // change the viewing camera automatically
    if ( m_camera.cameraTime > 5.0f )
    {
        ++m_camera.selectedCamera;
        if ( m_camera.selectedCamera == 3 )
        {
            m_camera.selectedCamera = 0;
        }
        m_camera.cameraTime = 0.0f;
    }

    // select camera based on input
    switch ( m_camera.selectedCamera )
    {
    case 0:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_1, true );
        break;
    case 1:
        m_systems.cameras->SelectCamera( CAMERA_GAME_MODEL_2, true );
        break;
    case 2:
        m_systems.cameras->SelectCamera( CAMERA_FREE, true );
        break;
    }

    // Object-follow cameras keep their eye fixed and retarget their view point
    // to the tracked model each frame.
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // New synchronization requests start a fresh relative-camera baseline.
        if(m_camera.input.Get( InputState::Aux1 )) m_systems.cameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_camera.input.Get( InputState::Aux2 ))
        {
            // perform the relative update
            RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            RelativeUpdateCamera(CAMERA_FREE);

            // The requested relative update has been consumed for this frame.
            m_systems.cameras->ResetRelativity();
        }
    */
}


void Run::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_systems.cameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation( hash );
        float minY =
            m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) +
            Cfg().minCameraHeight;
        m_systems.cameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}
