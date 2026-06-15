/*
File: SkullbonezSource/SkullbonezRunRender.cpp
Purpose:
  Coordinates render passes for the active scene.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Render pass: Named slice of DrawPrimitives() with explicit inputs, outputs,
  and resource ownership.
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  DXR (DirectX Raytracing): DX12 API used here for optional raytraced water
  reflection dispatch.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
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
  - DrawPrimitives() owns pass order. Pass helpers may bind targets and restore
    local render state, but they must not present or advance the frame.
  - Pass resource reset hooks run while the renderer backend is alive, because
    framebuffers, shaders, and dynamic vertex buffers can own backend objects.
  - Pass input/output structs borrow data for one frame only. Do not cache
    pointers returned from ShadowPassOutput or ReflectionPassOutput consumers.

Related:
  - SkullbonezSource/SkullbonezRun.h declares pass contracts and resources.
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
Vector3 NormalizeShadowLightDirection( Vector3 lightDirectionWorld )
{
    // Why: scene/config data can omit or zero the sun vector. Shadows still need
    // a normalized direction so matrix construction cannot divide by a zero
    // length vector.
    if ( VectorMag( lightDirectionWorld ) < 1.0e-5f )
    {
        lightDirectionWorld = Vector3( -0.68f, 0.22f, -0.70f );
    }
    lightDirectionWorld.Normalise();
    return lightDirectionWorld;
}

} // namespace

// The cinematic settings can come from two places:
//  1. a .scene file, when a test/preview scene is loaded, or
//  2. the normal engine config, when the app is running without a scene override.
// This helper hides that choice so the render code below can just ask for "the
// current cinematic look" without caring where it was authored.
CinematicRenderConfig& SkullbonezRun::ActiveCinematicConfig()
{
    return m_scene.isSceneMode ? m_scene.cinematicRender : Cfg().cinematicRender;
}


const CinematicRenderConfig& SkullbonezRun::ActiveCinematicConfig() const
{
    return m_scene.isSceneMode ? m_scene.cinematicRender : Cfg().cinematicRender;
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


SkullbonezRun::RenderFrameContext SkullbonezRun::BuildRenderFrameContext( bool cinematicRender, const CinematicRenderConfig& renderConfig )
{
    RenderFrameContext frame;
    frame.cinematicEnabled = cinematicRender;
    frame.cinematic = cinematicRender ? &renderConfig : nullptr;

    // Normal gameplay uses a point light (w = 1). Cinematic mode uses a
    // directional light (w = 0), which behaves like the sun: the same warm light
    // direction hits every object no matter where it is in the world.
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


void SkullbonezRun::EnsureReflectionRenderResources()
{
    if ( !IsGfxReady() )
    {
        return;
    }

    ReflectionPassResources& reflection = m_systems.renderPasses.reflection;
    // Why: the reflection texture is intentionally supersampled relative to the
    // window. Water can then sample it at grazing angles without making the
    // mirrored scene look blocky.
    const int fboW = (std::max)( 1, Gfx().GetWidth() * 2 );
    const int fboH = (std::max)( 1, Gfx().GetHeight() * 2 );
    const bool needsReflectionTarget = !reflection.target ||
                                       reflection.target->GetWidth() != fboW ||
                                       reflection.target->GetHeight() != fboH ||
                                       reflection.target->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA8;

    if ( needsReflectionTarget )
    {
        LogRenderResourceLifecycleStep( "window_resize", "reflection_target_recreate_if_needed" );
        if ( reflection.target )
        {
            reflection.target->ResetResources();
        }
        reflection.target.reset();
        reflection.target = Gfx().CreateFramebuffer( fboW, fboH );
    }
}


void SkullbonezRun::ResetReflectionRenderResources()
{
    ReflectionPassResources& reflection = m_systems.renderPasses.reflection;
    LogRenderResourceLifecycleStep( "reflection_reset", "reflection_target" );
    // Lifetime: ResetResources gives the backend a chance to release device
    // objects before the unique_ptr destructor drops the renderer-neutral shell.
    if ( reflection.target )
    {
        reflection.target->ResetResources();
    }
    reflection.target.reset();
}


void SkullbonezRun::EnsureCinematicRenderResources()
{
    if ( !IsCinematicRenderingEnabled() )
    {
        return;
    }

    const int w = (std::max)( 1, Gfx().GetWidth() );
    const int h = (std::max)( 1, Gfx().GetHeight() );

    // Why: the main scene target is full size because it holds the real image.
    // The volumetric target is half size because light shafts are naturally
    // soft, so the cheaper texture still looks right after compositing.
    const int volW = (std::max)( 1, w / 2 );
    const int volH = (std::max)( 1, h / 2 );

    // Concept: RGBA16F is a floating-point color format. It can store values
    // brighter than "monitor white", which bloom, god rays, and sunset
    // tonemapping need before the final pass squeezes the image back onto the
    // screen.
    CinematicScenePassResources& scene = m_systems.renderPasses.cinematicScene;
    VolumetricLightPassResources& volumetric = m_systems.renderPasses.volumetricLight;
    TonemapPassResources& tonemap = m_systems.renderPasses.tonemap;
    SkyPassResources& sky = m_systems.renderPasses.sky;
    FullscreenPassResources& fullscreen = m_systems.renderPasses.fullscreen;
    const bool needsSceneTarget = !scene.hdrTarget ||
                                  scene.hdrTarget->GetWidth() != w ||
                                  scene.hdrTarget->GetHeight() != h ||
                                  scene.hdrTarget->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
    const bool needsVolumetricTarget = !volumetric.target ||
                                       volumetric.target->GetWidth() != volW ||
                                       volumetric.target->GetHeight() != volH ||
                                       volumetric.target->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;

    if ( needsSceneTarget )
    {
        // Lifetime: these targets are recreated on resize. The reset path owns
        // backend teardown, while this ensure path owns dimensions and format.
        scene.hdrTarget.reset();
        scene.hdrTarget = Gfx().CreateFramebuffer( w, h, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }
    if ( needsVolumetricTarget )
    {
        volumetric.target.reset();
        volumetric.target = Gfx().CreateFramebuffer( volW, volH, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }

    if ( !tonemap.shader )
    {
        // Final full-screen pass: combines fog, bloom, god rays, volumetric light,
        // exposure, gamma, and vignette into the backbuffer image.
        tonemap.shader = m_systems.assets.CreateShader( "shader.post_tonemap" );
    }
    if ( !volumetric.shader )
    {
        // Half-resolution pass: creates the warm "shafts of light through air"
        // texture that the tonemap pass later adds over the scene.
        volumetric.shader = m_systems.assets.CreateShader( "shader.post_volumetric_light" );
    }
    if ( !sky.atmosphereShader )
    {
        // Procedural sky pass: draws a generated sunset/cloud backdrop instead
        // of the normal cube-map skybox when cinematic mode is enabled.
        sky.atmosphereShader = m_systems.assets.CreateShader( "shader.sky_atmosphere" );
    }

    if ( fullscreen.quadVB == 0 )
    {
        // Post-processing shaders do not draw a 3D model. They draw one rectangle
        // that covers the whole screen, then each pixel samples textures from the
        // previous passes. Each vertex stores: screen position xy + texture uv.
        const int attribs[] = { 2, 2 };
        fullscreen.quadVB = Gfx().CreateDynamicVB( attribs, 2, 6 );
    }
}


void SkullbonezRun::ResetCinematicRenderResources()
{
    // Lifetime: cinematic resources are device-owned objects grouped by pass.
    // The ordered table makes teardown auditable and keeps future pass
    // extraction from hiding backend reset work inside destructors.
    enum class CinematicResetStep
    {
        ShadowResources,
        FullscreenQuad,
        SkyAtmosphereShader,
        VolumetricLightShader,
        TonemapShader,
        VolumetricTarget,
        SceneTarget
    };

    struct CinematicResetPhase
    {
        const char* name;
        CinematicResetStep step;
    };

    const CinematicResetPhase resetSteps[] = {
        { "shadow_resources", CinematicResetStep::ShadowResources },
        { "fullscreen_quad_vb", CinematicResetStep::FullscreenQuad },
        { "sky_atmosphere_shader", CinematicResetStep::SkyAtmosphereShader },
        { "volumetric_light_shader", CinematicResetStep::VolumetricLightShader },
        { "tonemap_shader", CinematicResetStep::TonemapShader },
        { "volumetric_target", CinematicResetStep::VolumetricTarget },
        { "cinematic_scene_target", CinematicResetStep::SceneTarget },
    };

    RunRenderPassResources& passes = m_systems.renderPasses;
    for ( const CinematicResetPhase& phase : resetSteps )
    {
        LogRenderResourceLifecycleStep( "cinematic_reset", phase.name );
        switch ( phase.step )
        {
        case CinematicResetStep::ShadowResources:
            ResetShadowRenderResources();
            break;
        case CinematicResetStep::FullscreenQuad:
            if ( IsGfxReady() && passes.fullscreen.quadVB != 0 )
            {
                Gfx().DestroyDynamicVB( passes.fullscreen.quadVB );
            }
            passes.fullscreen.quadVB = 0;
            break;
        case CinematicResetStep::SkyAtmosphereShader:
            passes.sky.atmosphereShader.reset();
            break;
        case CinematicResetStep::VolumetricLightShader:
            passes.volumetricLight.shader.reset();
            break;
        case CinematicResetStep::TonemapShader:
            passes.tonemap.shader.reset();
            break;
        case CinematicResetStep::VolumetricTarget:
            passes.volumetricLight.target.reset();
            break;
        case CinematicResetStep::SceneTarget:
            passes.cinematicScene.hdrTarget.reset();
            break;
        }
    }
}


void SkullbonezRun::EnsureShadowRenderResources( const CinematicRenderConfig& cinematic )
{
    if ( !cinematic.shadowsEnabled || !IsGfxReady() )
    {
        return;
    }
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/EnsureResources" );

    // Concept: the shadow map is a renderer-neutral depth framebuffer. It is
    // intentionally owned outside the cinematic HDR target because the same
    // light-space depth texture is useful in normal backbuffer rendering,
    // cinematic rendering, and screenshot/perf scenes. The cinematic config
    // still supplies map size and bias/softness values, but the feature itself
    // is no longer gated by the cinematic post-processing path.
    const int mapSize = std::clamp( cinematic.shadowMapSize, 256, 8192 );
    auto ensureTarget = [&]( std::unique_ptr<Rendering::IFramebuffer>& target )
    {
        const bool needsTarget = !target ||
                                 target->GetWidth() != mapSize ||
                                 target->GetHeight() != mapSize;
        if ( needsTarget )
        {
            target.reset();
            target = Gfx().CreateFramebuffer( mapSize, mapSize );
        }
    };
    ShadowPassResources& shadows = m_systems.renderPasses.shadows;
    ensureTarget( shadows.terrainTarget );
    ensureTarget( shadows.objectTarget );
}


void SkullbonezRun::ResetShadowRenderResources()
{
    // Lifetime: drop both the backing framebuffer and the per-frame payload.
    // Framebuffer handles are owned by the current device/backend, so any
    // device reset, resize rebuild, or future backend bring-up must force a
    // clean recreate before the next shadow pass. The payload is reset too so
    // receivers cannot accidentally sample an old depth texture after the
    // resource dies.
    enum class ShadowResetStep
    {
        TerrainShadowFBO,
        ObjectShadowFBO,
        FramePayloads
    };

    struct ShadowResetPhase
    {
        const char* name;
        ShadowResetStep step;
    };

    const ShadowResetPhase resetSteps[] = {
        { "terrain_shadow_target", ShadowResetStep::TerrainShadowFBO },
        { "object_shadow_target", ShadowResetStep::ObjectShadowFBO },
        { "shadow_frame_payloads", ShadowResetStep::FramePayloads },
    };

    ShadowPassResources& shadows = m_systems.renderPasses.shadows;
    for ( const ShadowResetPhase& phase : resetSteps )
    {
        LogRenderResourceLifecycleStep( "shadow_reset", phase.name );
        switch ( phase.step )
        {
        case ShadowResetStep::TerrainShadowFBO:
            if ( shadows.terrainTarget )
            {
                shadows.terrainTarget->ResetResources();
            }
            shadows.terrainTarget.reset();
            break;
        case ShadowResetStep::ObjectShadowFBO:
            if ( shadows.objectTarget )
            {
                shadows.objectTarget->ResetResources();
            }
            shadows.objectTarget.reset();
            break;
        case ShadowResetStep::FramePayloads:
            shadows.terrainFrame = Rendering::ShadowFrameData();
            shadows.objectFrame = Rendering::ShadowFrameData();
            break;
        }
    }
}


SkullbonezCore::Rendering::ShadowFrameData SkullbonezRun::BuildShadowFrameData( const CinematicRenderConfig& cinematic, const Vector3& lightDirectionWorld ) const
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildTerrainFrame" );

    Rendering::ShadowFrameData shadowFrame;
    const ShadowPassResources& shadows = m_systems.renderPasses.shadows;
    if ( !m_systems.terrain || !shadows.terrainTarget )
    {
        return shadowFrame;
    }

    // Shadow maps need a stable light-space camera. `lightDirectionWorld` is
    // treated as the vector from the scene toward the light source. Cinematic
    // rendering passes the authored sun direction; normal rendering passes the
    // existing point-light position vector as a directional approximation. That
    // keeps the feature available everywhere without rewriting the old lighting
    // model into a true directional-light renderer.
    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );

    const XZBounds terrainBounds = m_systems.terrain->GetXZBounds();
    const float extentX = (std::max)( terrainBounds.m_xMax - terrainBounds.m_xMin, 1.0f );
    const float extentZ = (std::max)( terrainBounds.m_zMax - terrainBounds.m_zMin, 1.0f );
    const float terrainHeightRange = (std::max)( m_systems.terrain->GetMaxHeight() - m_systems.terrain->GetMinHeight(), 64.0f );
    const float terrainRadius = (std::max)( extentX, extentZ ) * 0.5f;
    const float shadowRadius = std::clamp( terrainRadius + 180.0f, 128.0f, (std::max)( cinematic.shadowMaxDistance, 128.0f ) );

    // Center the orthographic projection over the whole terrain instead of the
    // camera. This is a simple single-map v1: it avoids camera-dependent popping
    // and makes screenshots deterministic, at the cost of spreading resolution
    // across the authored terrain bounds instead of using cascades.
    const Vector3 focus( ( terrainBounds.m_xMin + terrainBounds.m_xMax ) * 0.5f,
                         ( m_systems.terrain->GetMinHeight() + m_systems.terrain->GetMaxHeight() ) * 0.5f,
                         ( terrainBounds.m_zMin + terrainBounds.m_zMax ) * 0.5f );
    const float lightBackDistance = shadowRadius + terrainHeightRange + 650.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + terrainHeightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection = Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = shadows.terrainTarget->GetDepthTextureHandle();

    // Everything below is copied into shader uniforms by ApplyShadowReceiverUniforms.
    // Keeping the values in one payload makes balls, boxes, terrain, and any
    // future backend consume the same shadow decision for the frame.
    shadowFrame.mapSize = shadows.terrainTarget->GetWidth();
    shadowFrame.pcfRadius = std::clamp( cinematic.shadowPcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadowStrength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadowDepthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadowSlopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadowSoftness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = cinematic.shadowTerrainReceives;
    shadowFrame.objectsReceive = cinematic.shadowObjectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


SkullbonezCore::Rendering::ShadowFrameData SkullbonezRun::BuildObjectShadowFrameData( const CinematicRenderConfig& cinematic, const Vector3& lightDirectionWorld, const Vector3& focusHint )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame" );

    Rendering::ShadowFrameData shadowFrame;
    ShadowPassResources& shadows = m_systems.renderPasses.shadows;
    if ( !shadows.objectTarget || !cinematic.shadowObjectsCast || !cinematic.shadowObjectsReceive )
    {
        return shadowFrame;
    }

    Vector3 focus;
    float shadowRadius = 0.0f;
    float heightRange = 0.0f;
    const float objectSearchDistance = std::clamp( cinematic.shadowMaxDistance * 0.15f, 180.0f, 320.0f );
    if ( !m_cGameModelCollection.GetObjectShadowBounds( focusHint, objectSearchDistance, focus, shadowRadius, heightRange ) )
    {
        return shadowFrame;
    }

    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );
    const float lightBackDistance = shadowRadius + heightRange + 220.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + heightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection = Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = shadows.objectTarget->GetDepthTextureHandle();
    shadowFrame.mapSize = shadows.objectTarget->GetWidth();
    shadowFrame.pcfRadius = std::clamp( cinematic.shadowPcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadowStrength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadowDepthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadowSlopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadowSoftness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = false;
    shadowFrame.objectsReceive = cinematic.shadowObjectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


void SkullbonezRun::RenderShadowMap( Rendering::IFramebuffer& target, const Rendering::ShadowFrameData& shadowFrame, const CinematicRenderConfig& cinematic, bool renderTerrain, bool renderObjects )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap" );

    if ( !shadowFrame.valid )
    {
        return;
    }
    if ( ( !renderTerrain || !cinematic.shadowTerrainCasts ) && ( !renderObjects || !cinematic.shadowObjectsCast ) )
    {
        return;
    }

    // Render from the light's point of view into the depth attachment. The pass
    // does not need color output; the framebuffer abstraction still gives us a
    // color target on some backends, but receivers sample only the depth texture
    // handle stored in ShadowFrameData.
    target.Bind();
    Gfx().SetViewport( 0, 0, target.GetWidth(), target.GetHeight() );
    Gfx().Clear( true, true );

    // Shadow depth writes must be opaque and depth-only. Save the caller's
    // blend/depth state because this pass runs in the middle of DrawPrimitives()
    // before reflection, world rendering, water, UI, and debug overlays.
    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    Gfx().SetDepthTest( true );
    Gfx().SetDepthWrite( true );
    Gfx().SetBlend( false );
    Gfx().SetCullFace( true );

    // Polygon offset reduces self-shadow acne on terrain and object faces. The
    // shader-side receiver bias handles comparison precision; this rasterizer
    // bias handles the depth values written into the map.
    Gfx().SetPolygonOffset( true, 2.0f, 4.0f );

    if ( renderTerrain && cinematic.shadowTerrainCasts && !m_debug.isTerrainHidden && m_systems.terrain )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );

        // Terrain must cast with the same optional render-only relief that the
        // visible terrain uses. Otherwise cinematic basin relief would receive
        // shadows from the flat CPU height map and the contact would visibly
        // detach. With normal rendering the relief amount is zero by default.
        m_systems.terrain->RenderShadowDepth( shadowFrame.lightView, shadowFrame.lightProjection, &cinematic );
    }

    if ( renderObjects && cinematic.shadowObjectsCast && !m_debug.isCollisionVisualizer )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );

        // Balls, boxes, and pine-style box visuals all write depth here. The
        // collection keeps separate instanced batches so each caster shape uses
        // the same mesh silhouette as the visible forward pass.
        m_cGameModelCollection.RenderShadowCasters( shadowFrame.lightView, shadowFrame.lightProjection, &cinematic );
    }

    Gfx().SetPolygonOffset( false );
    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetDepthWrite( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
    target.Unbind();
    Gfx().SetViewport( 0, 0, Gfx().GetWidth(), Gfx().GetHeight() );
}


SkullbonezRun::ShadowPassOutput SkullbonezRun::RenderShadowPass( const ShadowPassInputs& inputs )
{
    ShadowPassResources& shadows = m_systems.renderPasses.shadows;
    // Invariant: always clear the receiver payloads at the start of the pass.
    // If shadows are disabled, downstream terrain/object passes must see null
    // outputs instead of last frame's depth texture handles.
    shadows.terrainFrame = Rendering::ShadowFrameData();
    shadows.objectFrame = Rendering::ShadowFrameData();
    if ( inputs.cinematic )
    {
        // Build shadow maps before any receiver pass. Terrain receives the broad
        // map, while objects receive a second tight map centered on nearby bodies
        // so ball-on-ball shadows have enough texel density.
        PROFILE_SCOPED( "Frame/Shadows" );
        PROFILE_GPU_BEGIN( "Frame/Shadows/ShadowMap" );
        Vector3 lightDirection( inputs.frame.lightPosition[0], inputs.frame.lightPosition[1], inputs.frame.lightPosition[2] );
        EnsureShadowRenderResources( *inputs.cinematic );
        shadows.terrainFrame = BuildShadowFrameData( *inputs.cinematic, lightDirection );
        if ( shadows.terrainTarget )
        {
            RenderShadowMap( *shadows.terrainTarget, shadows.terrainFrame, *inputs.cinematic, true, true );
        }
        shadows.objectFrame = BuildObjectShadowFrameData( *inputs.cinematic, lightDirection, inputs.frame.eye );
        if ( shadows.objectTarget )
        {
            RenderShadowMap( *shadows.objectTarget, shadows.objectFrame, *inputs.cinematic, false, true );
        }
        PROFILE_GPU_END( "Frame/Shadows/ShadowMap" );
    }

    ShadowPassOutput output;
    output.terrainShadow = shadows.terrainFrame.valid ? &shadows.terrainFrame : nullptr;
    output.objectShadow = shadows.objectFrame.valid ? &shadows.objectFrame : output.terrainShadow;
    return output;
}


void SkullbonezRun::RenderCinematicSky( const Matrix4& view, const Matrix4& projection )
{
    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    SkyPassResources& sky = m_systems.renderPasses.sky;
    FullscreenPassResources& fullscreen = m_systems.renderPasses.fullscreen;
    if ( !cinematic.skyAtmosphereEnabled || !sky.atmosphereShader || fullscreen.quadVB == 0 )
    {
        return;
    }

    // The sky is painted as a full-screen background. It should not test against
    // terrain depth and it should not blend with whatever was previously there.
    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    Gfx().SetDepthTest( false );
    Gfx().SetDepthWrite( false );
    Gfx().SetBlend( false );

    sky.atmosphereShader->Use();
    sky.atmosphereShader->SetVec4( "uSunParams",
                                   cinematic.sunScreenX,
                                   cinematic.sunScreenY,
                                   cinematic.sunIntensity,
                                   cinematic.skyGlowStrength );
    sky.atmosphereShader->SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    sky.atmosphereShader->SetVec3( "uHorizonColor", cinematic.skyHorizonR, cinematic.skyHorizonG, cinematic.skyHorizonB );
    sky.atmosphereShader->SetVec3( "uZenithColor", cinematic.skyZenithR, cinematic.skyZenithG, cinematic.skyZenithB );
    sky.atmosphereShader->SetMat4( "uInvView", view.Inverse() );
    sky.atmosphereShader->SetMat4( "uInvProjection", projection.Inverse() );
    sky.atmosphereShader->SetInt( "uSkyMode", cinematic.skyMode );
    sky.atmosphereShader->SetVec4( "uCloudParams",
                                   cinematic.cloudCoverage,
                                   cinematic.cloudSoftness,
                                   cinematic.cloudScale,
                                   cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );

    // Two triangles make a screen-sized rectangle. The first two numbers are
    // clip-space position (-1..1), and the second two are the UV used by the
    // fragment shader to know where this pixel is on the screen.
    const float verts[] = {
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
    };
    Gfx().UploadAndDrawDynamicVB( fullscreen.quadVB, verts, 6 );

    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetDepthWrite( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
}


void SkullbonezRun::RenderSkyPass( const RenderFrameContext& frame, const Matrix4& view, SkyPassMode mode )
{
    const bool useCinematicAtmosphere = mode == SkyPassMode::CinematicIfEnabled &&
                                        frame.cinematic &&
                                        frame.cinematic->skyAtmosphereEnabled;
    if ( useCinematicAtmosphere )
    {
        RenderCinematicSky( view, frame.projection );
        return;
    }

    // The cube-map sky follows camera X/Z so the box feels infinitely far away,
    // while its Y stays authored by config to preserve the long-standing horizon.
    Matrix4 skyView = view * Matrix4::Translate( frame.eye.x, Cfg().skyboxRenderHeight, frame.eye.z ) * Matrix4::Scale( Cfg().skyboxScale );
    m_systems.skyBox->Render( skyView, frame.projection );
}


void SkullbonezRun::BeginCinematicScenePass( const RenderFrameContext& frame )
{
    // Invariant: from this point onward, draw the world into the HDR scene
    // target instead of directly into the window. The post pass later moves it
    // to the backbuffer with the cinematic effects applied.
    CinematicScenePassResources& scene = m_systems.renderPasses.cinematicScene;
    scene.hdrTarget->Bind();
    Gfx().SetViewport( 0, 0, scene.hdrTarget->GetWidth(), scene.hdrTarget->GetHeight() );
    Gfx().Clear( true, true );

    PROFILE_GPU_BEGIN( "Frame/Render/CinematicSky" );
    RenderSkyPass( frame, frame.baseView, SkyPassMode::CinematicIfEnabled );
    PROFILE_GPU_END( "Frame/Render/CinematicSky" );
}


SkullbonezRun::ReflectionPassOutput SkullbonezRun::RenderReflectionPass( const ReflectionPassInputs& inputs )
{
    ReflectionPassOutput output;

    // Concept: two implementations, one water-pass contract. The planar path
    // renders the above-water scene from a mirrored camera into an FBO. The DXR
    // path rebuilds the raytracing TLAS and writes a screen-space reflection
    // texture directly. Both feed the same water shader later.
    PROFILE_GPU_BEGIN( "Frame/Render/Reflection" );
    const auto renderCapabilities = Gfx().GetCapabilities();
    const bool useDxrReflection = renderCapabilities.supportsDxrReflection &&
                                  m_debug.isWaterRTReflect &&
                                  !m_debug.isWaterNoReflect &&
                                  !inputs.collisionStateColorsVisible &&
                                  !inputs.transparentBodyPass;
    output.usedDxr = useDxrReflection;

    if ( useDxrReflection )
    {
        // Lifetime: the DX12 backend owns the raytracing acceleration
        // structures. The runtime only streams current per-model transforms into
        // the TLAS before dispatching one reflection ray per texture pixel.
        int ballCount = m_cGameModelCollection.GetModelCount();
        for ( int i = 0; i < ballCount; ++i )
        {
            Matrix4 mdlMat = m_cGameModelCollection.GetModelAtIndex( i ).GetModelMatrix();
            memcpy( m_dxrReflectionTransforms.data() + i * 16, mdlMat.Data(), 16 * sizeof( float ) );
        }

        // Terrain/sphere BLAS objects are owned by the DX12 backend, so the
        // runtime supplies only per-instance sphere transforms here.
        Gfx().BuildTLAS( m_dxrReflectionTransforms.data(), ballCount, 0, 0 );

        // Ray generation reconstructs world-space rays from screen pixels, so
        // it needs the inverse of the main camera view-projection matrix.
        Matrix4 invVP = inputs.frame.viewProjection.Inverse();
        float cameraPos[3] = { inputs.frame.eye.x, inputs.frame.eye.y, inputs.frame.eye.z };
        float simTime = static_cast<float>( m_timers.simulationTimer.GetTotalTime() );

        uint32_t sphereHandle = m_systems.textures->GetTextureHandle( TEXTURE_BOUNDING_SPHERE );
        uint32_t terrainHandle = m_systems.textures->GetTextureHandle( TEXTURE_GROUND );
        uint32_t skyUpHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_UP );
        uint32_t skyDownHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_DOWN );
        uint32_t skyRightHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_RIGHT );
        uint32_t skyLeftHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_LEFT );
        uint32_t skyFrontHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_FRONT );
        uint32_t skyBackHandle = m_systems.textures->GetTextureHandle( TEXTURE_SKY_BACK );
        Gfx().DispatchReflectionRays( invVP.Data(),
                                      cameraPos,
                                      inputs.frame.waterY,
                                      simTime,
                                      inputs.frame.lightPosition,
                                      m_systems.window->m_sWindowDimensions.x * 2,
                                      m_systems.window->m_sWindowDimensions.y * 2,
                                      sphereHandle,
                                      terrainHandle,
                                      skyUpHandle,
                                      skyDownHandle,
                                      skyRightHandle,
                                      skyLeftHandle,
                                      skyFrontHandle,
                                      skyBackHandle );
        output.reflectionTextureHandle = Gfx().GetReflectionUAVTexture();
        output.reflectionSampleViewProjection = inputs.frame.viewProjection;
    }
    else
    {
        // Invariant: the planar path binds only its own reflection target and
        // restores the viewport to the window size before water renders.
        ReflectionPassResources& reflectionResources = m_systems.renderPasses.reflection;
        reflectionResources.target->Bind();
        Gfx().SetViewport( 0, 0, reflectionResources.target->GetWidth(), reflectionResources.target->GetHeight() );
        Gfx().Clear( true, true );

        // Skybox reflected (XZ follows eye; Y anchored at Cfg().skyboxRenderHeight).
        // Cinematic mode can reflect the generated sunset sky into the water
        // instead of the usual cube-map sky.
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Skybox" );
        RenderSkyPass( inputs.frame, inputs.frame.reflectionView, SkyPassMode::CinematicIfEnabled );
        PROFILE_GPU_END( "Frame/Render/Reflection/Skybox" );

        // Why: clip at the water surface so the reflection texture contains only
        // the above-water portion of models. The water shader supplies the
        // below-surface visual from the main scene.
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Balls" );
        Gfx().SetClipPlane( 0, true );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        if ( inputs.collisionStateColorsVisible )
        {
            m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
            m_collisionVisualizer.Render( m_cGameModelCollection, inputs.frame.reflectionView, inputs.frame.projection, inputs.frame.lightPosition );
            m_collisionVisualizer.SetAlphaOverride( -1.0f );
        }
        else
        {
            m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
            m_cGameModelCollection.RenderModels( inputs.frame.reflectionView,
                                                 inputs.frame.projection,
                                                 inputs.frame.lightPosition,
                                                 inputs.cinematic,
                                                 inputs.objectShadow,
                                                 inputs.bodyAlpha );
        }
        Gfx().SetClipPlane( 0, false );
        SkullbonezHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( "Frame/Render/Reflection/Balls" );

        reflectionResources.target->Unbind();
        Gfx().SetViewport( 0, 0, m_systems.window->m_sWindowDimensions.x, m_systems.window->m_sWindowDimensions.y );
        output.reflectionTextureHandle = reflectionResources.target->GetColorTextureHandle();
        output.reflectionSampleViewProjection = inputs.frame.reflectionViewProjection;
    }
    PROFILE_GPU_END( "Frame/Render/Reflection" );
    return output;
}


void SkullbonezRun::RenderObjectPass( const ObjectPassInputs& inputs )
{
    PROFILE_GPU_BEGIN( "Frame/Render/Balls" );

    if ( inputs.collisionStateColorsVisible )
    {
        m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
        m_collisionVisualizer.Render( m_cGameModelCollection, inputs.frame.baseView, inputs.frame.projection, inputs.frame.lightPosition );
        m_collisionVisualizer.SetAlphaOverride( -1.0f );
    }
    else
    {
        m_systems.textures->SelectTexture( TEXTURE_BOUNDING_SPHERE );
        m_cGameModelCollection.RenderModels( inputs.frame.baseView,
                                             inputs.frame.projection,
                                             inputs.frame.lightPosition,
                                             inputs.cinematic,
                                             inputs.shadow,
                                             inputs.bodyAlpha );
    }

    PROFILE_GPU_END( "Frame/Render/Balls" );
}


void SkullbonezRun::RenderTerrainPass( const TerrainPassInputs& inputs )
{
    if ( m_debug.isTerrainHidden )
    {
        return;
    }

    PROFILE_GPU_BEGIN( "Frame/Render/Terrain" );
    m_systems.textures->SelectTexture( TEXTURE_GROUND );
    m_systems.terrain->Render( inputs.frame.baseView, inputs.frame.projection, inputs.frame.lightPosition, inputs.cinematic, inputs.shadow );
    PROFILE_GPU_END( "Frame/Render/Terrain" );
}


void SkullbonezRun::RenderWaterPass( const WaterPassInputs& inputs )
{
    if ( inputs.waterHidden || ( inputs.frame.cinematicEnabled && inputs.cinematic && inputs.cinematic->waterMode == 0 ) )
    {
        return;
    }

    PROFILE_GPU_BEGIN( "Frame/Render/Water" );
    float waterTime = inputs.freezeTime
                          ? inputs.frozenTime
                          : static_cast<float>( m_timers.simulationTimer.GetTimeSinceLastStart() );
    m_cWorldEnvironment.RenderFluid( inputs.frame.baseView,
                                     inputs.frame.projection,
                                     inputs.reflection.reflectionSampleViewProjection,
                                     waterTime,
                                     inputs.reflection.reflectionTextureHandle,
                                     inputs.flatWater,
                                     inputs.noReflection,
                                     inputs.frame.cinematicEnabled,
                                     inputs.cinematic );
    PROFILE_GPU_END( "Frame/Render/Water" );
}


void SkullbonezRun::RenderDebugOverlayPass( const DebugOverlayPassInputs& inputs )
{
    // Debug overlays intentionally stay out of the object/material pass. They
    // draw diagnostic geometry over the final world view and should not inherit
    // production material binding assumptions.
    if ( m_debug.isBroadphaseOverlay )
    {
        m_broadphaseVisualizer.Render( inputs.frame.viewProjection );
    }

    if ( m_runtimeSettings.tornadoField.visualizeVelocityField )
    {
        m_cGameModelCollection.RenderTornadoFieldVectors( inputs.frame.viewProjection );
    }

    if ( m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        m_physicsDebugVisualizer.SetFlags( m_debug.physicsDebugFlags );
        m_physicsDebugVisualizer.SetPipelineStageCursor( m_debug.physicsDebugPipelineStageCursor );
        m_physicsDebugVisualizer.Render( m_cGameModelCollection, inputs.frame.viewProjection, m_systems.terrain.get() );
    }
}


bool SkullbonezRun::RenderCinematicVolumetricLight()
{
    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    CinematicScenePassResources& scene = m_systems.renderPasses.cinematicScene;
    VolumetricLightPassResources& volumetric = m_systems.renderPasses.volumetricLight;
    FullscreenPassResources& fullscreen = m_systems.renderPasses.fullscreen;
    if ( !cinematic.volumetricLightingEnabled ||
         !scene.hdrTarget ||
         !volumetric.target ||
         !volumetric.shader ||
         fullscreen.quadVB == 0 )
    {
        return false;
    }

    // Invariant: unbind the full-size scene target before sampling it. The
    // volumetric pass reads scene color/depth and writes a separate soft light
    // texture, so read and write targets must be different resources.
    scene.hdrTarget->Unbind();
    volumetric.target->Bind();
    Gfx().SetViewport( 0, 0, volumetric.target->GetWidth(), volumetric.target->GetHeight() );

    // This is another screen-space effect, so depth testing and blending are
    // disabled while the full-screen quad is generated.
    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    Gfx().SetDepthTest( false );
    Gfx().SetDepthWrite( false );
    Gfx().SetBlend( false );

    volumetric.shader->Use();
    volumetric.shader->SetInt( "uSceneTex", 0 );
    volumetric.shader->SetInt( "uDepthTex", 1 );
    volumetric.shader->SetVec4( "uDepthParams", Cfg().frustumNear, Cfg().frustumFar, 0.0f, 0.0f );
    volumetric.shader->SetVec4( "uSunShaftParams",
                                cinematic.sunScreenX,
                                cinematic.sunScreenY,
                                cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f,
                                cinematic.sunShaftFalloff );
    volumetric.shader->SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    volumetric.shader->SetVec4( "uVolumetricParams",
                                cinematic.volumetricStrength,
                                cinematic.volumetricDensity,
                                cinematic.volumetricDecay,
                                cinematic.fogDensity );
    volumetric.shader->SetVec4( "uCloudParams",
                                cinematic.cloudCoverage,
                                cinematic.cloudSoftness,
                                cinematic.cloudScale,
                                cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
    // Pass contract: texture slot 0 is rendered color, slot 1 is rendered
    // depth. The shader uses depth to tell sky pixels from solid geometry so
    // rays pass through sky and fade when they cross hills/balls.
    Gfx().BindTexture( scene.hdrTarget->GetColorTextureHandle(), 0 );
    Gfx().BindTexture( scene.hdrTarget->GetDepthTextureHandle(), 1 );

    // Same full-screen rectangle pattern as the sky and tonemap passes.
    const float verts[] = {
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
    };
    Gfx().UploadAndDrawDynamicVB( fullscreen.quadVB, verts, 6 );

    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetDepthWrite( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
    volumetric.target->Unbind();
    Gfx().SetViewport( 0, 0, Gfx().GetWidth(), Gfx().GetHeight() );
    return true;
}


void SkullbonezRun::ResolveCinematicSceneToBackbuffer( bool sceneAlreadyUnbound, bool volumetricReady )
{
    CinematicScenePassResources& scene = m_systems.renderPasses.cinematicScene;
    VolumetricLightPassResources& volumetric = m_systems.renderPasses.volumetricLight;
    TonemapPassResources& tonemap = m_systems.renderPasses.tonemap;
    FullscreenPassResources& fullscreen = m_systems.renderPasses.fullscreen;
    if ( !scene.hdrTarget || !tonemap.shader || fullscreen.quadVB == 0 )
    {
        return;
    }

    if ( !sceneAlreadyUnbound )
    {
        scene.hdrTarget->Unbind();
    }
    Gfx().SetViewport( 0, 0, Gfx().GetWidth(), Gfx().GetHeight() );

    const bool depthWasEnabled = Gfx().IsDepthTestEnabled();
    const bool blendWasEnabled = Gfx().IsBlendEnabled();
    Gfx().SetDepthTest( false );
    Gfx().SetDepthWrite( false );
    Gfx().SetBlend( false );

    // Concept: "resolve" means "turn our off-screen cinematic render target
    // into the final image on the window." This is where the HDR scene becomes
    // normal display color and where bloom/fog/rays are layered in.
    tonemap.shader->Use();
    tonemap.shader->SetInt( "uSceneTex", 0 );
    tonemap.shader->SetInt( "uDepthTex", 1 );
    tonemap.shader->SetInt( "uVolumetricTex", 2 );
    const CinematicRenderConfig& cinematic = ActiveCinematicConfig();
    tonemap.shader->SetFloat( "uExposure", cinematic.exposure );
    tonemap.shader->SetFloat( "uGamma", cinematic.gamma );
    tonemap.shader->SetVec4( "uDepthParams", Cfg().frustumNear, Cfg().frustumFar, 0.0f, 0.0f );
    tonemap.shader->SetVec4( "uFogParams",
                             cinematic.fogStart,
                             cinematic.fogEnd,
                             cinematic.fogEnabled ? cinematic.fogDensity : 0.0f,
                             cinematic.fogEnabled ? cinematic.fogMaxOpacity : 0.0f );
    tonemap.shader->SetVec3( "uFogColor", cinematic.fogColorR, cinematic.fogColorG, cinematic.fogColorB );
    tonemap.shader->SetVec4( "uSunShaftParams",
                             cinematic.sunScreenX,
                             cinematic.sunScreenY,
                             cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f,
                             cinematic.sunShaftFalloff );
    tonemap.shader->SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    tonemap.shader->SetVec4( "uBloomParams",
                             cinematic.bloomThreshold,
                             cinematic.bloomKnee,
                             cinematic.bloomEnabled ? cinematic.bloomStrength : 0.0f,
                             cinematic.bloomRadius );
    tonemap.shader->SetVec4( "uCloudParams",
                             cinematic.cloudCoverage,
                             cinematic.cloudSoftness,
                             cinematic.cloudScale,
                             cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
    tonemap.shader->SetVec4( "uStyleGrade",
                             cinematic.styleSaturation,
                             cinematic.styleContrast,
                             cinematic.styleVignette,
                             static_cast<float>( cinematic.skyMode ) );
    tonemap.shader->SetFloat( "uVolumetricCompositeStrength", volumetricReady && cinematic.volumetricLightingEnabled ? 1.0f : 0.0f );
    // Pass contract: slot 0 is the bright HDR scene, slot 1 is its depth buffer,
    // and slot 2 is either the volumetric-light texture or a harmless fallback
    // when that pass is disabled.
    Gfx().BindTexture( scene.hdrTarget->GetColorTextureHandle(), 0 );
    Gfx().BindTexture( scene.hdrTarget->GetDepthTextureHandle(), 1 );
    Gfx().BindTexture( volumetricReady && volumetric.target ? volumetric.target->GetColorTextureHandle() : scene.hdrTarget->GetColorTextureHandle(), 2 );

    // Draw one rectangle over the backbuffer. The fragment shader runs once per
    // window pixel and decides the final visible color.
    const float verts[] = {
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
    };
    Gfx().UploadAndDrawDynamicVB( fullscreen.quadVB, verts, 6 );

    Gfx().SetDepthTest( depthWasEnabled );
    Gfx().SetDepthWrite( depthWasEnabled );
    Gfx().SetBlend( blendWasEnabled );
}


void SkullbonezRun::RenderCinematicPostPasses()
{
    // Invariant: post passes run only after all world geometry has been rendered
    // into the HDR target. Volumetric light depends on completed scene depth;
    // tonemap depends on both scene color and the optional volumetric texture.
    const bool volumetricReady = RenderCinematicVolumetricLight();
    ResolveCinematicSceneToBackbuffer( volumetricReady, volumetricReady );
}


void SkullbonezRun::Render()
{
    // In text_only mode all 3D rendering is skipped. DrawWindowText handles the display.
    if ( m_debug.isTextOnly )
    {
        return;
    }

    // Update the active camera selection and any transition/tween state before
    // rendering asks for view matrices.
    SetViewingOrientation();

    // Apply the selected camera to the camera collection so render code below
    // reads one coherent eye/view/up triple for this frame.
    m_systems.cameras->SetCamera();

    // Despite the old name, DrawPrimitives is the renderer-neutral frame body:
    // shadows, reflection, terrain, objects, water, overlays, and post passes.
    DrawPrimitives();
}


void SkullbonezRun::DrawPrimitives()
{
    const bool cinematicRender = IsCinematicRenderingEnabled();
    const CinematicRenderConfig& renderConfig = ActiveCinematicConfig();
    const bool shadowMapsEnabled = renderConfig.shadowsEnabled && IsGfxReady() && !m_debug.isTextOnly;

    if ( cinematicRender )
    {
        // Lifetime: cinematic resources are lazy. A window resize or backend
        // rebuild drops them; the next cinematic frame recreates the targets and
        // shader objects with the current window dimensions.
        EnsureCinematicRenderResources();
    }
    const bool useCinematicTarget = cinematicRender && m_systems.renderPasses.cinematicScene.hdrTarget != nullptr;
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

    // Defer the first DX12 command-list open until after CPU-side model prep so
    // allocator waits do not block work that can overlap the previous frame.
    if ( !cinematicRender )
    {
        Gfx().Clear( true, true );
    }

    const CinematicRenderConfig* activeCinematic = frame.cinematic;
    const CinematicRenderConfig* activeShadowConfig = shadowMapsEnabled ? &renderConfig : nullptr;
    ShadowPassOutput shadowPass = RenderShadowPass( { frame, activeShadowConfig } );
    const Rendering::ShadowFrameData* terrainShadowFrame = shadowPass.terrainShadow;
    const Rendering::ShadowFrameData* objectShadowFrame = shadowPass.objectShadow;

    const bool collisionStateColorsVisible = m_debug.isCollisionVisualizer;
    const bool transparentBodyPass = m_debug.isPhysicsDebugTransparent && m_debug.physicsDebugAlpha < 1.0f;
    const float bodyRenderAlpha = transparentBodyPass ? m_debug.physicsDebugAlpha : 1.0f;
    const float collisionVisualizerAlphaOverride = transparentBodyPass ? bodyRenderAlpha : -1.0f;
    // Reflection is needed even when water later decides not to draw; keeping
    // resource creation here preserves the historical frame sequence and avoids
    // coupling water visibility to target lifetime.
    EnsureReflectionRenderResources();

    // Camera m_position for skybox placement.  During camera transitions the
    // selected camera is already the destination, but SetCamera() renders from
    // the interpolated tween camera.  Reflection math must use the same render
    // camera as baseView; otherwise the mirror pass is generated from the
    // destination camera while the water surface samples it from the in-between
    // camera, which stretches reflected balls during transitions.
    // render skybox ------------------------------
    if ( !cinematicRender )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Skybox" );
        RenderSkyPass( frame, frame.baseView, SkyPassMode::CubemapOnly );
        PROFILE_GPU_END( "Frame/Render/Skybox" );
    }

    ReflectionPassOutput reflection = RenderReflectionPass( { frame,
                                                              activeCinematic,
                                                              objectShadowFrame,
                                                              collisionStateColorsVisible,
                                                              transparentBodyPass,
                                                              collisionVisualizerAlphaOverride,
                                                              bodyRenderAlpha } );

    if ( useCinematicTarget )
    {
        BeginCinematicScenePass( frame );
    }

    // render game models -----------------------------
    if ( !transparentBodyPass )
    {
        RenderObjectPass( { frame,
                            ObjectPassMode::Opaque,
                            activeCinematic,
                            objectShadowFrame,
                            collisionStateColorsVisible,
                            collisionVisualizerAlphaOverride,
                            1.0f } );
    }

    // render m_terrain ------------------------------
    RenderTerrainPass( { frame, activeCinematic, terrainShadowFrame } );

    // render the fluid ---------------------------
    RenderWaterPass( { frame,
                       reflection,
                       activeCinematic,
                       m_debug.isWaterHidden,
                       m_debug.isWaterFlatDebug,
                       m_debug.isWaterNoReflect,
                       m_debug.isWaterFreezeDebug,
                       m_debug.frozenWaterTime } );

    if ( transparentBodyPass )
    {
        RenderObjectPass( { frame,
                            ObjectPassMode::Transparent,
                            activeCinematic,
                            objectShadowFrame,
                            collisionStateColorsVisible,
                            collisionVisualizerAlphaOverride,
                            bodyRenderAlpha } );
    }

    RenderDebugOverlayPass( { frame } );

    if ( useCinematicTarget )
    {
        RenderCinematicPostPasses();
    }
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

    // set the camera m_boundaries
    m_systems.cameras->SetCameraXZBounds( m_systems.terrain->GetXZBounds() );

    // set the m_terrain
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


void SkullbonezRun::DrawWindowText( const double dSecondsPerFrame )
{
    // Update rolling timers — runs every frame regardless of overlay state
    m_timers.updateTimer.StopTimer();
    m_timers.timeSinceLastRender += static_cast<float>( m_timers.updateTimer.GetElapsedTime() );
    m_timers.updateTimer.StartTimer();

    const double currentSceneEnergy = m_cGameModelCollection.GetSceneKineticEnergy();
    m_timers.sceneEnergyAccumulator += currentSceneEnergy;
    ++m_timers.sceneEnergySampleCount;

    if ( m_timers.timeSinceLastRender > 0.5f )
    {
        if ( dSecondsPerFrame )
        {
            m_timers.rollingFpsTime = 1.0f / static_cast<float>( dSecondsPerFrame );
            m_timers.rollingPhysicsTime = m_timers.physicsTime;
            m_timers.rollingRenderTime = m_timers.renderTime;
        }
        if ( m_timers.sceneEnergySampleCount > 0 )
        {
            m_timers.rollingSceneEnergy = static_cast<float>( m_timers.sceneEnergyAccumulator / static_cast<double>( m_timers.sceneEnergySampleCount ) );
            m_timers.sceneEnergyAccumulator = 0.0;
            m_timers.sceneEnergySampleCount = 0;
        }
        m_timers.timeSinceLastRender = 0.0f;
    }

    float sceneEnergyForDisplay = m_timers.rollingSceneEnergy;
    if ( m_timers.sceneEnergySampleCount > 0 && sceneEnergyForDisplay == 0.0f )
    {
        sceneEnergyForDisplay = static_cast<float>( m_timers.sceneEnergyAccumulator / static_cast<double>( m_timers.sceneEnergySampleCount ) );
    }

    const char* rendererName = Gfx().GetRendererName();

    // text_only mode: solid background + full-screen pangram, no HUD/profiler
    if ( m_debug.isTextOnly )
    {
        // Dark background covering the full viewport
        Text2d::Render2dQuad( -0.55f, -0.45f, 0.55f, 0.45f, 0.08f, 0.08f, 0.12f, 1.0f );

        // Three rows of the pangram — each line uses a slightly different colour
        // so hue/brightness fringing artefacts are visible on all channel combinations
        const float sz = 0.09f;
        Text2d::Render2dTextColor( -0.46f, 0.22f, sz, 1.00f, 1.00f, 1.00f, "The quick brown fox" );
        Text2d::Render2dTextColor( -0.46f, 0.07f, sz, 1.00f, 0.90f, 0.20f, "jumps over the" );
        Text2d::Render2dTextColor( -0.46f, -0.08f, sz, 0.40f, 0.90f, 1.00f, "lazy dog" );

        // Renderer name in small text at bottom so we know which backend we're looking at
        Text2d::Render2dTextColor( -0.46f, -0.38f, 0.015f, 0.60f, 0.60f, 0.60f, "renderer: %s", rendererName );

        Text2d::FlushText();
        return;
    }

    const float hw = Text2d::HalfW();
    const float hh = Text2d::HalfH();
    const float mX = 0.022f; // horizontal inset from left/right edge
    const float mY = 0.015f; // vertical inset from top/bottom edge

    // Crosshair — always visible when nudge mode is active, regardless of overlay state.
    // A tiny center gap keeps the target visible instead of covering it.
    if ( m_camera.isNudgeMode )
    {
        const float cArm = 0.020f;
        const float cGap = 0.004f;
        const float cHalf = 0.00045f;
        const float cShadowHalf = 0.00080f;
        Text2d::Render2dQuad( -cArm, -cShadowHalf, -cGap, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( cGap, -cShadowHalf, cArm, cShadowHalf, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cShadowHalf, -cArm, cShadowHalf, -cGap, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cShadowHalf, cGap, cShadowHalf, cArm, 0.0f, 0.0f, 0.0f, 0.40f );
        Text2d::Render2dQuad( -cArm, -cHalf, -cGap, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( cGap, -cHalf, cArm, cHalf, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( -cHalf, -cArm, cHalf, -cGap, 0.80f, 0.96f, 1.0f, 0.88f );
        Text2d::Render2dQuad( -cHalf, cGap, cHalf, cArm, 0.80f, 0.96f, 1.0f, 0.88f );
#ifdef _DEBUG
        if ( m_debug.reproSnapshotMessage[0] != '\0' &&
             m_timers.simulationTimer.GetTimeSinceLastStart() <= m_debug.reproSnapshotMessageUntil )
        {
            const float msgSz = 0.014f;
            float msgW = Text2d::MeasureText( msgSz, m_debug.reproSnapshotMessage );
            Text2d::Render2dTextColor( -msgW * 0.5f,
                                       -0.065f,
                                       msgSz,
                                       0.65f,
                                       0.92f,
                                       1.0f,
                                       "%s",
                                       m_debug.reproSnapshotMessage );
        }
#endif
    }

    const char* sceneName = "";
    if ( m_scene.isSceneMode && m_scene.currentSceneIndex >= 0 && m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) )
    {
        sceneName = FileNameFromPath( m_sceneQueue[m_scene.currentSceneIndex].c_str() );
    }

    if ( m_UI.IsVisible() )
    {
        PROFILE_BEGIN( "Frame/UI/BuildData" );
        InGameUIFrameData UIData;
        UIData.screenW = m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.x ) : Cfg().window.screenX;
        UIData.screenH = m_systems.window ? static_cast<int>( m_systems.window->m_sWindowDimensions.y ) : Cfg().window.screenY;
        if ( m_debug.isUITestPattern )
        {
            DrawUITestPattern( UIData.screenW, UIData.screenH );
        }
        UIData.rendererName = rendererName;
        UIData.sceneName = sceneName;
        UIData.sceneOptions = m_sceneBrowserNamePtrs.empty() ? nullptr : m_sceneBrowserNamePtrs.data();
        UIData.sceneOptionCount = static_cast<int>( m_sceneBrowserNamePtrs.size() );
        UIData.selectedSceneOption = CurrentSceneBrowserIndex();
        UIData.selectedCineModeSceneOption = m_selectedCineModeSceneIndex;
        UIData.UIDrawCalls = m_timers.lastUIDrawCalls;
        UIData.fps = m_timers.rollingFpsTime > 0.0f ? m_timers.rollingFpsTime : ( dSecondsPerFrame > 0.0 ? 1.0f / static_cast<float>( dSecondsPerFrame ) : 0.0f );
        UIData.renderMs = ( m_timers.rollingRenderTime > 0.0f ? m_timers.rollingRenderTime : m_timers.renderTime ) * 1000.0f;
        UIData.physicsMs = ( m_timers.rollingPhysicsTime > 0.0f ? m_timers.rollingPhysicsTime : m_timers.physicsTime ) * 1000.0f;
        UIData.cpuFrameMs = m_timers.cpuFrameWorkMs;
        UIData.gpuFrameMs = m_timers.gpuFrameWorkMs;
        UIData.modelCount = m_scene.modelCount;
        UIData.currentFrame = m_scene.currentFrame;
        UIData.targetFrameCount = m_scene.targetFrameCount;
        UIData.rngSeed = m_scene.rngSeed;
        UIData.solverBallCount = m_scene.solverBallCount;
        UIData.solverBoxCount = m_scene.solverBoxCount;
        UIData.currentSceneIndex = m_scene.currentSceneIndex;
        UIData.sceneCount = static_cast<int>( m_sceneQueue.size() );
        UIData.now = m_timers.simulationTimer.GetTotalTime();
        UIData.sceneMode = m_scene.isSceneMode;
        UIData.scenePhysicsEnabled = m_scene.isScenePhysics;
        UIData.sceneTextEnabled = m_scene.isSceneText;
        UIData.textOnly = m_debug.isTextOnly;
        UIData.fixedStep = m_scene.isFixedStep;
        UIData.exitOnComplete = m_scene.isExitOnComplete;
        UIData.testComplete = m_scene.isTestComplete;
        UIData.vsyncEnabled = m_runtimeSettings.isVsyncEnabled;
        UIData.pipelineSyncEnabled = m_runtimeSettings.isPipelineSyncEnabled;
        UIData.sceneEnergy = sceneEnergyForDisplay;
        UIData.timeScale = m_scene.timeScale;
        UIData.trackHeight = m_camera.trackBallIndex >= 0 ? m_camera.trackHeight : 0.0f;
        UIData.autoCycleInterval = m_camera.autoCycleInterval > 0.0f ? m_camera.autoCycleInterval : 0.0f;
        UIData.worldGravity = m_cWorldEnvironment.GetGravity();
        UIData.worldFluidHeight = m_cWorldEnvironment.GetFluidSurfaceHeight();
        UIData.worldFluidDensity = m_cWorldEnvironment.GetFluidDensity();
        UIData.physicsDebugFlags = m_debug.physicsDebugFlags;
        {
            const int stageCount = static_cast<int>( PhysicsPipelineStage::Count );
            int stageIndex = stageCount > 0 ? m_debug.physicsDebugPipelineStageCursor % stageCount : 0;
            if ( stageIndex < 0 )
            {
                stageIndex += stageCount;
            }
            UIData.physicsPipelineStageName = PhysicsPipelineStageName( static_cast<PhysicsPipelineStage>( stageIndex ) );
            UIData.physicsPipelineStageIndex = stageIndex;
            UIData.physicsPipelineStageCount = stageCount;
        }
        UIData.physicsDebugAlpha = m_debug.physicsDebugAlpha;
        UIData.physicsDebugContactLinger = m_debug.physicsDebugContactLinger;
        UIData.physicsSleepEnabled = m_runtimeSettings.isPhysicsSleepEnabled;
        UIData.collisionVisualizer = m_debug.isCollisionVisualizer;
        UIData.physicsDebugTransparent = m_debug.isPhysicsDebugTransparent;
        UIData.broadphaseOverlay = m_debug.isBroadphaseOverlay;
        UIData.tornadoEnabled = m_runtimeSettings.tornadoField.enabled;
        UIData.tornadoFieldVectors = m_runtimeSettings.tornadoField.visualizeVelocityField;
        UIData.tornadoRadius = m_runtimeSettings.tornadoField.radius;
        UIData.tornadoHeight = m_runtimeSettings.tornadoField.height;
        UIData.tornadoInwardAcceleration = m_runtimeSettings.tornadoField.inwardAcceleration;
        UIData.tornadoSwirlAcceleration = m_runtimeSettings.tornadoField.swirlAcceleration;
        UIData.tornadoLiftAcceleration = m_runtimeSettings.tornadoField.liftAcceleration;
        UIData.waterFreezeDebug = m_debug.isWaterFreezeDebug;
        UIData.waterFlatDebug = m_debug.isWaterFlatDebug;
        UIData.terrainHidden = m_debug.isTerrainHidden;
        UIData.waterHidden = m_debug.isWaterHidden;
        UIData.waterNoReflect = m_debug.isWaterNoReflect;
        UIData.waterRTReflect = m_debug.isWaterRTReflect;
        UIData.cameraMouseActive = m_camera.isFlyMode && !m_UI.WantsNativeMouseCursor() && !m_UI.BlocksCameraMouse();
        UIData.nativeCursorVisible = !UIData.cameraMouseActive;
        UIData.canSaveSceneDefaults = m_scene.isSceneMode &&
                                      m_scene.currentSceneIndex >= 0 &&
                                      m_scene.currentSceneIndex < static_cast<int>( m_sceneQueue.size() ) &&
                                      !m_sceneQueue[m_scene.currentSceneIndex].empty();
        UIData.cinematicRendering = IsCinematicRenderingEnabled();
        UIData.cinematic = ActiveCinematicConfig();
        PROFILE_END( "Frame/UI/BuildData" );

        PROFILE_BEGIN( "Frame/UI/PreFlushText" );
        Text2d::FlushText();
        PROFILE_END( "Frame/UI/PreFlushText" );
        UIData.drawCallsBeforeUI = Gfx().GetFrameDrawCallCount();
        const int UIDrawCallStart = UIData.drawCallsBeforeUI;
        m_UI.Draw( UIData );
        PROFILE_BEGIN( "Frame/UI/PostFlushText" );
        Text2d::FlushText();
        PROFILE_END( "Frame/UI/PostFlushText" );
        const int UIDrawCallEnd = Gfx().GetFrameDrawCallCount();
        m_timers.lastUIDrawCalls = (std::max)( 0, UIDrawCallEnd - UIDrawCallStart );
        return;
    }

    // --- Overlay: None ---
    if ( m_debug.overlayMode == OverlayMode::None )
    {
        Text2d::FlushText();
        return;
    }

    // --- Overlay: Scene telemetry ---
    if ( m_debug.overlayMode == OverlayMode::SceneStats )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.012f;
        const float lineH = 0.025f;
        const float panPad = 0.014f;
        const float panW = 0.36f;
        const float panH = panPad * 2.0f + titleSz + lineH * 2.0f;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz, titleSz, 1.0f, 0.85f, 0.35f, "SCENE TELEMETRY" );
        Text2d::Render2dTextColor( panX0 + panPad, panY1 - panPad - titleSz - lineH, entrySz, 0.85f, 0.85f, 0.85f, "Model Count: %d", m_scene.modelCount );
        Text2d::Render2dTextColor( panX0 + panPad,
                                   panY1 - panPad - titleSz - lineH * 2.0f,
                                   entrySz,
                                   0.85f,
                                   0.85f,
                                   0.85f,
                                   "Scene Energy: %.6f",
                                   sceneEnergyForDisplay );
        Text2d::FlushText();
        return;
    }

    // --- Overlay: Visual profiler bars (normalized or absolute) ---
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    if ( m_debug.overlayMode == OverlayMode::BarsNormalized || m_debug.overlayMode == OverlayMode::BarsAbsolute )
    {
        // Panel anchored bottom-left, filling most of the width. Height kept modest — leave vertical
        // space above for future multi-core stacked rows.
        const float panW = ( hw - mX ) * 2.0f * 0.85f; // 85% of screen width
        const float panH = ( hh - mY ) * 2.0f * 0.22f; // 22% of screen height
        const float panX = -( hw - mX ) + mX * 0.5f;   // slight left margin
        const float panY = -( hh - mY ) + mY * 0.5f;   // slight bottom margin
        const bool absolute = ( m_debug.overlayMode == OverlayMode::BarsAbsolute );
        Profiler::Instance().RenderBarOverlay( panX, panY, panW, panH, absolute );
        Text2d::FlushText();
        return;
    }
#endif

    // --- Overlay: Keys reference screen (compact, bottom-left) ---
    if ( m_debug.overlayMode == OverlayMode::Keys )
    {
        const float titleSz = 0.013f;
        const float entrySz = 0.011f;
        const float lineH = 0.020f;
        const int nRows = 13;
        const float panPad = 0.012f;
        const float titleGap = 0.016f; // space between title baseline and first entry
        const float keyW = 0.058f;     // key-name column width
        const float descW = 0.120f;    // description column width
        const float colGap = 0.012f;   // gap between the two content columns

        // Panel dimensions — anchored to bottom-left corner
        const float panH = panPad + titleSz + titleGap + static_cast<float>( nRows ) * lineH + panPad;
        const float panW = panPad + keyW + descW + colGap + keyW + descW + panPad;
        const float panX0 = -( hw - mX );
        const float panY0 = -( hh - mY );
        const float panX1 = panX0 + panW;
        const float panY1 = panY0 + panH;

        Text2d::Render2dQuad( panX0, panY0, panX1, panY1, 0.04f, 0.04f, 0.07f, 0.93f );

        // Title left-aligned inside panel
        const float titleY = panY1 - panPad - titleSz;
        Text2d::Render2dTextColor( panX0 + panPad, titleY, titleSz, 1.0f, 0.85f, 0.35f, "CONTROL REFERENCE" );

        // Column X positions
        const float col1Key = panX0 + panPad;
        const float col1Desc = col1Key + keyW;
        const float col2Key = col1Desc + descW + colGap;
        const float col2Desc = col2Key + keyW;
        const float firstY = titleY - titleGap;

        struct KeyEntry
        {
            const char* key;
            const char* desc;
        };
        static const KeyEntry kLeft[nRows] = {
            { "N", "Nudge mode" },
            { "Enter", "Dump repro" },
            { "F", "Fly mode" },
            { "WASD", "Move camera" },
            { "Mouse", "Look" },
            { "Shift", "Sprint (3x speed)" },
            { "LMB", "Fire silver bullet" },
            { "Shift+LMB", "Fast bullet" },
            { "Q", "Cycle renderer" },
            { "V", "Collision visual" },
            { "Space", "Step physics" },
            { "R/Bksp", "Reset scene" },
            { "F3", "Screenshot" },
        };
        static const KeyEntry kRight[nRows] = {
            { "Esc", "Min/expand UI" },
            { "Esc Esc", "Quit" },
            { "1", "Freeze water" },
            { "2", "Reflection mode" },
            { "3", "Toggle water flat" },
            { "4", "Toggle terrain" },
            { "5", "Toggle water" },
            { "6", "Debug body alpha" },
            { "G", "Broadphase overlay" },
            { "C", "Physics debug" },
            { "O", "Terrain probe" },
            { "PgUp/Dn", "Water height" },
            { "F7/F8", "Pipeline stage" },
        };

        for ( int i = 0; i < nRows; ++i )
        {
            float y = firstY - static_cast<float>( i ) * lineH;
            Text2d::Render2dTextColor( col1Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kLeft[i].key );
            Text2d::Render2dTextColor( col1Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kLeft[i].desc );
            Text2d::Render2dTextColor( col2Key, y, entrySz, 0.70f, 0.88f, 1.0f, "%s", kRight[i].key );
            Text2d::Render2dTextColor( col2Desc, y, entrySz, 0.85f, 0.85f, 0.85f, "%s", kRight[i].desc );
        }

        Text2d::FlushText();
        return;
    }

    // --- Overlay: Timers / HUD (OverlayMode::Timers) ---

    // Profiler overlay — bottom-left anchored.
    // Compiled out in Release; always shown when overlay is Timers in Debug/Profile.
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    {
        const float lineH = 0.018f;
        const float profFSz = 0.012f;
        const float padY = lineH * 1.2f;
        Profiler::Instance().RenderOverlay( -( hw - mX ), -( hh - mY ) - padY, lineH, profFSz, m_timers.rollingFpsTime );
    }
#endif

    Text2d::FlushText();
}


void SkullbonezRun::SetViewingOrientation()
{
    // In scene mode, use the first camera without cycling.
    // If ball-tracking is active, keep the camera locked onto the selected ball.
    if ( m_scene.isSceneMode )
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

    // set viewing m_orientation
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

    // set the view m_position of the selected camera based on the game model m_position
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_1 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 0 ) );
    }
    if ( m_systems.cameras->IsCameraSelected( CAMERA_GAME_MODEL_2 ) )
    {
        m_systems.cameras->SetViewCoordinates( m_cGameModelCollection.GetModelPosition( 1 ) );
    }

    /*
        // reset relativity when a new request for synchronisation comes in
        if(m_camera.input.Get( InputState::Aux1 )) m_systems.cameras->ResetRelativity();

        // sync m_cameras if in sync mode
        if(m_camera.input.Get( InputState::Aux2 ))
        {
            // perform the relative update
            RelativeUpdateCamera(CAMERA_GAME_MODEL_1);
            RelativeUpdateCamera(CAMERA_GAME_MODEL_2);
            RelativeUpdateCamera(CAMERA_FREE);

            // reset the relative variable as we have already performed the action on desired m_cameras
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
