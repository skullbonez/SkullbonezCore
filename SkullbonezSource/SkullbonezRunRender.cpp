/*
File: SkullbonezSource/SkullbonezRunRender.cpp
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
  - DrawPrimitives() owns pass order. Pass classes may bind targets and
    restore local render state, but they must not present or advance the frame.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.

Related:
  - SkullbonezSource/SkullbonezRun.h declares pass contracts and resources.
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"
#include "SkullbonezRenderGraph.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{

struct ActualFrameGraphInputs
{
    bool cinematicRender = false;
    bool useCinematicTarget = false;
    bool terrainShadowValid = false;
    bool objectShadowValid = false;
    bool reflectionUsedDxr = false;
    bool transparentBodyPass = false;
    bool waterPassVisible = false;
    bool waterSamplesReflection = false;
    bool volumetricReady = false;
};


bool IsSameActualFrameGraphInput( const ActualFrameGraphInputs& lhs, const ActualFrameGraphInputs& rhs )
{
    return lhs.cinematicRender == rhs.cinematicRender &&
           lhs.useCinematicTarget == rhs.useCinematicTarget &&
           lhs.terrainShadowValid == rhs.terrainShadowValid &&
           lhs.objectShadowValid == rhs.objectShadowValid &&
           lhs.reflectionUsedDxr == rhs.reflectionUsedDxr &&
           lhs.transparentBodyPass == rhs.transparentBodyPass &&
           lhs.waterPassVisible == rhs.waterPassVisible &&
           lhs.waterSamplesReflection == rhs.waterSamplesReflection &&
           lhs.volumetricReady == rhs.volumetricReady;
}


void DumpActualFrameGraph( const ActualFrameGraphInputs& inputs )
{
    static bool hasLastInputs = false;
    static ActualFrameGraphInputs lastInputs;
    if ( hasLastInputs && IsSameActualFrameGraphInput( inputs, lastInputs ) )
    {
        return;
    }
    lastInputs = inputs;
    hasLastInputs = true;

    using namespace SkullbonezCore::Rendering;

    RenderGraph graph;
    const RenderGraphResourceHandle backbuffer = graph.AddExternalResource( "SwapchainBackbuffer", RenderGraphResourceAccess::Present );
    const RenderGraphResourceHandle mainDepth = graph.AddExternalResource( "MainDepthStencil", RenderGraphResourceAccess::DepthWrite );

    RenderGraphResourceHandle terrainShadow;
    RenderGraphResourceHandle objectShadow;
    RenderGraphResourceHandle rasterReflectionColor;
    RenderGraphResourceHandle rasterReflectionDepth;
    RenderGraphResourceHandle dxrReflection;
    RenderGraphResourceHandle sceneColor;
    RenderGraphResourceHandle sceneDepth;
    RenderGraphResourceHandle volumetricLight;

    if ( inputs.terrainShadowValid )
    {
        terrainShadow = graph.AddExternalResource( "TerrainShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( inputs.objectShadowValid )
    {
        objectShadow = graph.AddExternalResource( "ObjectShadowMapDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( inputs.reflectionUsedDxr )
    {
        dxrReflection = graph.AddExternalResource( "DxrReflectionTexture", RenderGraphResourceAccess::PixelShaderResource );
    }
    else
    {
        rasterReflectionColor = graph.AddExternalResource( "RasterReflectionColor", RenderGraphResourceAccess::PixelShaderResource );
        rasterReflectionDepth = graph.AddExternalResource( "RasterReflectionDepth", RenderGraphResourceAccess::PixelShaderResource );
    }
    if ( inputs.useCinematicTarget )
    {
        sceneColor = graph.AddExternalResource( "CinematicSceneColor", RenderGraphResourceAccess::PixelShaderResource );
        sceneDepth = graph.AddExternalResource( "CinematicSceneDepth", RenderGraphResourceAccess::PixelShaderResource );
        if ( inputs.volumetricReady )
        {
            volumetricLight = graph.AddExternalResource( "VolumetricLight", RenderGraphResourceAccess::PixelShaderResource );
        }
    }

    const auto colorTarget = [&]() -> RenderGraphResourceHandle
    {
        return inputs.useCinematicTarget ? sceneColor : backbuffer;
    };
    const auto depthTarget = [&]() -> RenderGraphResourceHandle
    {
        return inputs.useCinematicTarget ? sceneDepth : mainDepth;
    };

    const auto addTargetWrite = [&]( uint32_t pass )
    {
        graph.AddWrite( pass, colorTarget(), RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( pass, depthTarget(), RenderGraphResourceAccess::DepthWrite );
    };
    const auto addShadowReads = [&]( uint32_t pass )
    {
        if ( inputs.terrainShadowValid )
        {
            graph.AddRead( pass, terrainShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        if ( inputs.objectShadowValid )
        {
            graph.AddRead( pass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
    };

    if ( !inputs.cinematicRender || !inputs.useCinematicTarget )
    {
        const uint32_t clearPass = graph.AddPass( "BackbufferClear" );
        graph.AddWrite( clearPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( clearPass, mainDepth, RenderGraphResourceAccess::DepthWrite );
    }

    if ( inputs.terrainShadowValid || inputs.objectShadowValid )
    {
        const uint32_t shadowPass = graph.AddPass( "ShadowMapPass" );
        if ( inputs.terrainShadowValid )
        {
            graph.AddWrite( shadowPass, terrainShadow, RenderGraphResourceAccess::DepthWrite );
        }
        if ( inputs.objectShadowValid )
        {
            graph.AddWrite( shadowPass, objectShadow, RenderGraphResourceAccess::DepthWrite );
        }
    }

    if ( !inputs.cinematicRender )
    {
        const uint32_t skyPass = graph.AddPass( "SkyboxPass" );
        graph.AddWrite( skyPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    }

    if ( inputs.reflectionUsedDxr )
    {
        const uint32_t dxrPass = graph.AddPass( "DxrReflectionPass", RenderGraphQueueType::Compute );
        graph.AddWrite( dxrPass, dxrReflection, RenderGraphResourceAccess::UnorderedAccess );
    }
    else
    {
        const uint32_t reflectionPass = graph.AddPass( "RasterReflectionPass" );
        if ( inputs.objectShadowValid )
        {
            graph.AddRead( reflectionPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        graph.AddWrite( reflectionPass, rasterReflectionColor, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( reflectionPass, rasterReflectionDepth, RenderGraphResourceAccess::DepthWrite );
    }

    if ( inputs.useCinematicTarget )
    {
        const uint32_t sceneBegin = graph.AddPass( "CinematicSceneBegin" );
        graph.AddWrite( sceneBegin, sceneColor, RenderGraphResourceAccess::RenderTarget );
        graph.AddWrite( sceneBegin, sceneDepth, RenderGraphResourceAccess::DepthWrite );
    }

    if ( !inputs.transparentBodyPass )
    {
        const uint32_t objectPass = graph.AddPass( "ObjectOpaquePass" );
        if ( inputs.objectShadowValid )
        {
            graph.AddRead( objectPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( objectPass );
    }

    const uint32_t terrainPass = graph.AddPass( "TerrainPass" );
    if ( inputs.terrainShadowValid )
    {
        graph.AddRead( terrainPass, terrainShadow, RenderGraphResourceAccess::PixelShaderResource );
    }
    addTargetWrite( terrainPass );

    if ( inputs.waterPassVisible )
    {
        const uint32_t waterPass = graph.AddPass( "WaterPass" );
        if ( inputs.waterSamplesReflection )
        {
            graph.AddRead( waterPass,
                           inputs.reflectionUsedDxr ? dxrReflection : rasterReflectionColor,
                           RenderGraphResourceAccess::PixelShaderResource );
        }
        addShadowReads( waterPass );
        addTargetWrite( waterPass );
    }

    if ( inputs.transparentBodyPass )
    {
        const uint32_t objectPass = graph.AddPass( "ObjectTransparentPass" );
        if ( inputs.objectShadowValid )
        {
            graph.AddRead( objectPass, objectShadow, RenderGraphResourceAccess::PixelShaderResource );
        }
        addTargetWrite( objectPass );
    }

    const uint32_t debugPass = graph.AddPass( "DebugOverlayPass" );
    addTargetWrite( debugPass );

    if ( inputs.useCinematicTarget && inputs.volumetricReady )
    {
        const uint32_t volumetricPass = graph.AddPass( "VolumetricLightPass" );
        graph.AddRead( volumetricPass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( volumetricPass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddWrite( volumetricPass, volumetricLight, RenderGraphResourceAccess::RenderTarget );
    }

    if ( inputs.useCinematicTarget )
    {
        const uint32_t tonemapPass = graph.AddPass( "ToneMapPass" );
        graph.AddRead( tonemapPass, sceneColor, RenderGraphResourceAccess::PixelShaderResource );
        graph.AddRead( tonemapPass, sceneDepth, RenderGraphResourceAccess::PixelShaderResource );
        if ( inputs.volumetricReady )
        {
            graph.AddRead( tonemapPass, volumetricLight, RenderGraphResourceAccess::PixelShaderResource );
        }
        graph.AddWrite( tonemapPass, backbuffer, RenderGraphResourceAccess::RenderTarget );
    }

    const uint32_t presentPass = graph.AddPass( "Present" );
    graph.AddWrite( presentPass, backbuffer, RenderGraphResourceAccess::Present );

    std::ostringstream out;
    out << "ActualExecutedFrameGraph\n";
    out << "cinematic_render=" << ( inputs.cinematicRender ? "true" : "false" ) << "\n";
    out << "use_cinematic_target=" << ( inputs.useCinematicTarget ? "true" : "false" ) << "\n";
    out << "terrain_shadow_valid=" << ( inputs.terrainShadowValid ? "true" : "false" ) << "\n";
    out << "object_shadow_valid=" << ( inputs.objectShadowValid ? "true" : "false" ) << "\n";
    out << "reflection_path=" << ( inputs.reflectionUsedDxr ? "DXR" : "Raster" ) << "\n";
    out << "transparent_body_pass=" << ( inputs.transparentBodyPass ? "true" : "false" ) << "\n";
    out << "water_pass_visible=" << ( inputs.waterPassVisible ? "true" : "false" ) << "\n";
    out << "water_samples_reflection=" << ( inputs.waterSamplesReflection ? "true" : "false" ) << "\n";
    out << "volumetric_ready=" << ( inputs.volumetricReady ? "true" : "false" ) << "\n\n";
    out << graph.DumpText();

    const std::string dumpText = out.str();
    static std::string lastDumpText;
    if ( dumpText == lastDumpText )
    {
        return;
    }
    lastDumpText = dumpText;

    std::ofstream file( "Debug/dx12_frame_graph_actual.txt", std::ios::binary );
    if ( file.is_open() )
    {
        file << dumpText << "\n";
    }
}

} // namespace


// The cinematic settings can come from two places:
//  1. a .scene.json file, when a test/preview scene is loaded, or
//  2. the normal engine config, when the app is running without a scene override.
// This helper hides that choice so the render code below can just ask for "the
// current cinematic look" without caring where it was authored.
CinematicRenderConfig& SkullbonezRun::ActiveCinematicConfig()
{
    return SceneState().isSceneMode ? SceneState().cinematicRender : Cfg().cinematicRender;
}


const CinematicRenderConfig& SkullbonezRun::ActiveCinematicConfig() const
{
    return SceneState().isSceneMode ? SceneState().cinematicRender : Cfg().cinematicRender;
}


bool SkullbonezRun::IsCinematicRenderingEnabled() const
{
    // Command line switches win over config/scene values. That lets us launch
    // the same scene in plain mode or cinematic mode while debugging.
    const bool enabled = m_cmdHasCinematicRenderingOverride ? m_cmdCinematicRendering : ActiveCinematicConfig().enabled;

    // Text-only mode deliberately skips all 3D rendering, so cinematic mode must
    // also stay off there. The UI text renderer handles that path by itself.
    return enabled && IsGfxReady() && !m_debug.isTextOnly;
}


bool SkullbonezRun::ApplyReplayPresentationSampleForRender( const ReplayPresentationSample& sample )
{
    std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.PhysicsModels();
    m_replayPoseBackups.clear();
    m_replayPoseBackups.reserve( (std::min)( sample.bodies.size(), models.size() ) );

    for ( const ReplayBodyPresentationSample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            continue;
        }

        RunReplayPoseBackup backup;
        backup.modelIndex = body.modelIndex;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        m_replayPoseBackups.push_back( backup );

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
    }

    if ( !m_replayPoseBackups.empty() )
    {
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }
    return !m_replayPoseBackups.empty();
}


bool SkullbonezRun::ApplyReplaySolverSampleForRender( const ReplaySolverFrameSample& sample )
{
    std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.PhysicsModels();
    m_replayPoseBackups.clear();
    m_replayPoseBackups.reserve( (std::min)( sample.bodies.size(), models.size() ) );

    for ( const ReplaySolverBodySample& body : sample.bodies )
    {
        if ( body.modelIndex < 0 || body.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( body.modelIndex )];
        if ( model.GetReplayBodyId() != body.id.value )
        {
            continue;
        }

        RunReplayPoseBackup backup;
        backup.modelIndex = body.modelIndex;
        backup.position = model.GetPosition();
        backup.orientation = model.GetOrientation();
        m_replayPoseBackups.push_back( backup );

        Math::Orientation::Quaternion orientation( body.orientation[0],
                                                   body.orientation[1],
                                                   body.orientation[2],
                                                   body.orientation[3] );
        orientation.Normalise();
        model.SetPosition( body.position );
        model.SetOrientation( orientation );
    }

    if ( !m_replayPoseBackups.empty() )
    {
        m_cGameModelCollection.InvalidatePhysicsStreams();
    }
    return !m_replayPoseBackups.empty();
}


void SkullbonezRun::RestoreReplayPresentationRenderPose()
{
    if ( m_replayPoseBackups.empty() )
    {
        return;
    }

    std::vector<GameObjects::GameModel>& models = m_cGameModelCollection.PhysicsModels();
    for ( const RunReplayPoseBackup& backup : m_replayPoseBackups )
    {
        if ( backup.modelIndex < 0 || backup.modelIndex >= static_cast<int>( models.size() ) )
        {
            continue;
        }

        GameObjects::GameModel& model = models[static_cast<std::size_t>( backup.modelIndex )];
        model.SetPosition( backup.position );
        model.SetOrientation( backup.orientation );
    }
    m_replayPoseBackups.clear();
    m_cGameModelCollection.InvalidatePhysicsStreams();
}


SkullbonezRun::RenderFrameContext SkullbonezRun::BuildRenderFrameContext( bool cinematicRender, const CinematicRenderConfig& renderConfig )
{
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;

    // Ordinary and cinematic rendering both use a directional sun (w = 0).
    // Keeping one sun-vector contract makes direct BRDF lighting and shadow-map
    // visibility block the same light contribution.
    if ( frame.cinematicEnabled )
    {
        frame.lightPosition[0] = -0.68f;
        frame.lightPosition[1] = 0.22f;
        frame.lightPosition[2] = -0.70f;
        frame.lightPosition[3] = 0.0f;
    }

    // Invariant: build the pass context after SetCamera(). During camera
    // transitions, the selected camera and render camera can differ; all passes
    // must consume the interpolated render camera so reflection, sky, and water
    // sample the same view.
    frame.baseView = m_systems.cameras->GetViewMatrix();
    frame.projection = m_systems.window->GetProjectionMatrix();
    frame.viewProjection = frame.projection * frame.baseView;
    frame.eye = m_systems.cameras->GetRenderCameraTranslation();
    frame.viewCenter = m_systems.cameras->GetRenderCameraView();
    frame.up = m_systems.cameras->GetRenderCameraUp();
    frame.waterY = m_cWorldEnvironment.GetFluidSurfaceHeight();
    frame.reflectionEye = Vector3( frame.eye.x, 2.0f * frame.waterY - frame.eye.y, frame.eye.z );
    frame.reflectionCenter = Vector3( frame.viewCenter.x, 2.0f * frame.waterY - frame.viewCenter.y, frame.viewCenter.z );
    frame.reflectionUp = Vector3( frame.up.x, -frame.up.y, frame.up.z );
    frame.reflectionView = Matrix4::LookAt( frame.reflectionEye, frame.reflectionCenter, frame.reflectionUp );
    frame.reflectionViewProjection = frame.projection * frame.reflectionView;
    return frame;
}


void SkullbonezRun::Render()
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

    if ( const ReplayPresentationSample* replaySample = CurrentReplayScrubSample() )
    {
        m_systems.cameras->OverrideRenderCameraForFrame( replaySample->camera.eye,
                                                         replaySample->camera.view,
                                                         replaySample->camera.up );
        ApplyReplayPresentationSampleForRender( *replaySample );
    }
    else if ( const ReplaySolverFrameSample* solverSample = CurrentReplaySolverScrubSample() )
    {
        m_systems.cameras->OverrideRenderCameraForFrame( solverSample->camera.eye,
                                                         solverSample->camera.view,
                                                         solverSample->camera.up );
        ApplyReplaySolverSampleForRender( *solverSample );
    }

    // DrawPrimitives is now the frame story: it chooses the optional cinematic
    // target, then runs named passes in the same order the image is produced.
    DrawPrimitives();
    RestoreReplayPresentationRenderPose();
}


void SkullbonezRun::DrawPrimitives()
{
    const bool cinematicRender = IsCinematicRenderingEnabled();
    const CinematicRenderConfig& renderConfig = ActiveCinematicConfig();
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
    const bool shadowMapsEnabled = activeShadowStyle.shadowsEnabled && IsGfxReady() && !m_debug.isTextOnly;

    if ( cinematicRender )
    {
        // Lifetime: cinematic resources are lazy. A window resize or backend
        // rebuild drops them; the next cinematic frame recreates the targets and
        // shader objects with the current window dimensions.
        RenderFrameContext preFrame = BuildRenderFrameContext( cinematicRender, renderConfig );
        m_fullscreenQuadPass.EnsureGpuResources( preFrame );
        m_skyPass.EnsureGpuResources( preFrame );
        m_sceneTargetPass.EnsureGpuResources( preFrame );
        m_volumetricPass.EnsureGpuResources( preFrame );
        m_tonemapPass.EnsureGpuResources( preFrame );
    }
    const bool useCinematicTarget = cinematicRender && m_sceneTargetPass.IsReady();
    if ( cinematicRender && !useCinematicTarget )
    {
        // If the cinematic target could not be created, fall back to the normal
        // backbuffer clear so the frame still renders instead of showing stale data.
        Gfx().Clear( true, true );
    }

    // Build the shared pass contract once, after camera update and before any
    // pass can bind targets. All extracted passes consume this same frame view.
    RenderFrameContext frame = BuildRenderFrameContext( cinematicRender, renderConfig );

    PROFILE_BEGIN( "Frame/Render/PrepareModels" );
    m_cGameModelCollection.PrepareRenderStreams();
    PROFILE_END( "Frame/Render/PrepareModels" );

    // These passes currently borrow subsystem-owned mesh/material resources,
    // but keeping the ensure calls in the frame story gives future extraction
    // work an obvious place to move those GPU resources.
    m_objectPass.EnsureGpuResources( frame );
    m_terrainPass.EnsureGpuResources( frame );
    m_waterPass.EnsureGpuResources( frame );
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

    const bool collisionStateColorsVisible = m_debug.isCollisionVisualizer;
    const bool transparentBodyPass = m_debug.isPhysicsDebugTransparent && m_debug.physicsDebugAlpha < 1.0f;
    const float bodyRenderAlpha = transparentBodyPass ? m_debug.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = transparentBodyPass ? bodyRenderAlpha : -1.0f;
    // Lifetime: reflection is produced before water decides whether to draw.
    // Keeping the target alive here avoids coupling debug water visibility to
    // GPU resource lifetime or resize behavior.
    m_reflectionPass.EnsureGpuResources( frame );

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

    ReflectionPassOutput reflection = m_reflectionPass.Render( { frame,
                                                                 activeCinematic,
                                                                 objectShadowFrame,
                                                                 collisionStateColorsVisible,
                                                                 transparentBodyPass,
                                                                 collisionVisualizerAlphaOverride,
                                                                 bodyRenderAlpha },
                                                               m_skyPass );

    if ( useCinematicTarget )
    {
        m_sceneTargetPass.Begin( frame, m_skyPass );
    }

    // Opaque bodies render before terrain/water unless debug transparency asks
    // for a late transparent body pass.
    if ( !transparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Opaque,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               1.0f } );
    }

    // Terrain receives the broad shadow frame and provides the main world depth
    // that cinematic post passes read later.
    m_terrainPass.Render( { frame, activeCinematic, terrainShadowFrame } );

    // Water is deliberately downstream of ReflectionPass; it samples the
    // reflection texture but never rebuilds it.
    m_waterPass.Render( { frame,
                          reflection,
                          activeCinematic,
                          m_debug.isWaterHidden,
                          m_debug.isWaterFlatDebug,
                          m_debug.isWaterNoReflect,
                          m_debug.isWaterFreezeDebug,
                          m_debug.frozenWaterTime } );

    if ( transparentBodyPass )
    {
        m_objectPass.Render( { frame,
                               ObjectPassMode::Transparent,
                               activeCinematic,
                               objectShadowFrame,
                               collisionStateColorsVisible,
                               collisionVisualizerAlphaOverride,
                               bodyRenderAlpha } );
    }

    m_debugOverlayPass.Render( { frame } );

    bool volumetricReady = false;
    if ( useCinematicTarget )
    {
        volumetricReady = m_volumetricPass.Render( frame );
        m_tonemapPass.Render( frame, volumetricReady, volumetricReady );
    }

    ActualFrameGraphInputs frameGraphInputs;
    frameGraphInputs.cinematicRender = cinematicRender;
    frameGraphInputs.useCinematicTarget = useCinematicTarget;
    frameGraphInputs.terrainShadowValid = terrainShadowFrame && terrainShadowFrame->valid;
    frameGraphInputs.objectShadowValid = objectShadowFrame && objectShadowFrame->valid;
    frameGraphInputs.reflectionUsedDxr = reflection.usedDxr;
    frameGraphInputs.transparentBodyPass = transparentBodyPass;
    frameGraphInputs.waterPassVisible = !m_debug.isWaterHidden;
    frameGraphInputs.waterSamplesReflection =
        frameGraphInputs.waterPassVisible &&
        !m_debug.isWaterNoReflect &&
        reflection.reflectionTextureHandle != 0;
    frameGraphInputs.volumetricReady = volumetricReady;
    DumpActualFrameGraph( frameGraphInputs );
}


void SkullbonezRun::SetUpCameras()
{
    m_systems.cameras = CameraCollection::Instance();

    m_systems.cameras->AddCamera( Vector3( 321.0f, 110.0f, 557.0f ), // Position
                                  Vector3( 581.0f, 40.0f, 633.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_1 );

    m_systems.cameras->AddCamera( Vector3( 730.0f, 100.0f, 380.0f ), // Position
                                  Vector3( 709.0f, 92.0f, 482.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_GAME_MODEL_2 );

    m_systems.cameras->AddCamera( Vector3( 900.0f, 110.0f, 900.0f ), // Position
                                  Vector3( 313.0f, 31.0f, 282.0f ),  // View
                                  Vector3( 0.0f, 1.0f, 0.0f ),       // Up
                                  CAMERA_FREE );

    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    m_systems.cameras->SetTerrain( m_systems.terrain.get() );

    // lock the m_cameras
    m_systems.cameras->SetLockedMode( true );
}


void SkullbonezRun::RebuildRegisteredRenderResources()
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
            SkullbonezHelper::ResetRenderResources();
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


void SkullbonezRun::SetViewingOrientation()
{
    // In scene mode, use the first camera without cycling.
    // If ball-tracking is active, keep the camera locked onto the selected ball.
    if ( SceneState().isSceneMode )
    {
        if ( m_camera.trackBallIndex >= 0 && m_camera.trackBallIndex < m_cGameModelCollection.GetModelCount() )
        {
            Vector3 ballPos = m_cGameModelCollection.GetModelPosition( m_camera.trackBallIndex );
            m_systems.cameras->SetPrimaryPosition( Vector3( ballPos.x, ballPos.y + m_camera.trackHeight, ballPos.z ) );
            m_systems.cameras->SetViewCoordinates( ballPos );
        }
        return;
    }

    // In fly mode, freeze the cycle clock and keep the free camera
    if ( m_camera.isFlyMode )
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


void SkullbonezRun::RelativeUpdateCamera( uint32_t hash )
{
    if ( !m_systems.cameras->IsCameraSelected( hash ) )
    {
        Vector3 translatedCameraPosition = m_systems.cameras->GetCameraTranslation( hash );
        float minY = m_systems.terrain->GetTerrainHeightAt( translatedCameraPosition.x, translatedCameraPosition.z, true ) + Cfg().minCameraHeight;
        m_systems.cameras->RelativeUpdate( hash, minY, Cfg().maxCameraHeight );
    }
}
