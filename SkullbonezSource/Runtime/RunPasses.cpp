/*
File: SkullbonezSource/Runtime/RunPasses.cpp
Purpose:
  Implements the named world-render pass classes owned by Run.

Mental model:
  RunRender.cpp should read like a frame story: build one frame
  context, run named passes, then hand each pass output to its downstream
  consumer. This file owns the rendering guts and GPU lifetime hooks for those
  passes so pass contracts are visible where the work happens.

Glossary:
  DXR (DirectX Raytracing): DX12 API used for hardware ray traversal and
  reflection dispatch.
  BLAS (Bottom-Level Acceleration Structure): Raytracing spatial index for one
  mesh's triangles.
  TLAS (Top-Level Acceleration Structure): Raytracing spatial index for scene
  instances that point at BLAS geometry.
  Render pass: A named slice of frame rendering with explicit inputs, outputs,
  and GPU resource ownership.
  GPU resource: Backend-owned texture, framebuffer, shader, descriptor, or
  dynamic vertex buffer that must be released before backend teardown.
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  FBO (Framebuffer Object): Engine shorthand for an off-screen render target
  exposed through the renderer abstraction.
  SRV (Shader Resource View): Descriptor row used when shaders read textures.

Invariants:
  - EnsureGpuResources may lazily create or resize backend resources, but must
    not change pass order or draw output.
  - ReleaseGpuResources must be safe during backend teardown and must leave
    resource structs in a null/zero state for the next ensure.
  - Render methods consume only the current frame's input structs; they must not
    cache borrowed pointers from those structs.

Related:
  - SkullbonezSource/Runtime/Run.h declares pass contracts and resources.
  - SkullbonezSource/Runtime/RunRender.cpp owns frame orchestration.
  - Agentic/Reference/comment-style-guide.md
*/
#include "RunInternal.h"
#include "RuntimeTuning.h"
#include "../Core/PlatformProfiler.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
constexpr int RENDER_TEXTURE_SLOT_COUNT = 5;
constexpr unsigned int RENDER_TEXTURE_SLOT_0 = 1u << 0;
constexpr unsigned int RENDER_TEXTURE_SLOT_1 = 1u << 1;
constexpr unsigned int RENDER_TEXTURE_SLOT_2 = 1u << 2;
constexpr unsigned int RENDER_TEXTURE_SLOT_3 = 1u << 3;
constexpr std::size_t TORNADO_VISUAL_FLOATS_PER_VERTEX = 11u;
constexpr float TORNADO_FX_KIND_RIBBON = 0.0f;
constexpr float TORNADO_FX_KIND_DUST = 1.0f;

void ClearRenderTextureSlotsExcept( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                                    unsigned int keptSlots )
{
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        if ( ( keptSlots & ( 1u << slot ) ) == 0u )
        {
            renderCommands.BindTexture( 0, slot );
        }
    }
}

void ClearAllRenderTextureSlots( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands )
{
    ClearRenderTextureSlotsExcept( renderCommands, 0u );
}

void BindRenderTextureSlots( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands,
                             uint32_t slot0,
                             uint32_t slot1,
                             uint32_t slot2,
                             uint32_t slot3,
                             uint32_t slot4 = 0 )
{
    // Contract: ordinary raster shaders expose t0..t4. Slot t4 is reserved for
    // the object material table, but pass hygiene still clears it to the typed
    // null SRV; object batches bind the material table again immediately before
    // drawing.
    const uint32_t handles[RENDER_TEXTURE_SLOT_COUNT] = { slot0, slot1, slot2, slot3, slot4 };
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        renderCommands.BindTexture( handles[slot], slot );
    }
}

SkullbonezCore::Rendering::IRenderCommandContext& RenderCommands( const RenderFrameContext& frame )
{
    assert( frame.renderCommands && "RenderFrameContext requires a render command context" );
    return *frame.renderCommands;
}

float Clamp01( float value )
{
    return std::clamp( value, 0.0f, 1.0f );
}

float HashUnitFloat( uint32_t value )
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>( value & 0x00ffffffu ) / static_cast<float>( 0x01000000u );
}

Vector3 NormalizeOr( Vector3 value, const Vector3& fallback )
{
    if ( VectorMagSquared( value ) <= 1.0e-8f )
    {
        return fallback;
    }
    value.Normalise();
    return value;
}

Vector3 CylindricalOffset( float radius, float angle )
{
    return Vector3( cosf( angle ) * radius, 0.0f, sinf( angle ) * radius );
}

void EmitFxVertex( std::vector<float>& vertices,
                   const Vector3& position,
                   float r,
                   float g,
                   float b,
                   float a,
                   float u,
                   float v,
                   float fxKind,
                   float terrainY )
{
    vertices.push_back( position.x );
    vertices.push_back( position.y );
    vertices.push_back( position.z );
    vertices.push_back( r );
    vertices.push_back( g );
    vertices.push_back( b );
    vertices.push_back( a );
    vertices.push_back( u );
    vertices.push_back( v );
    vertices.push_back( fxKind );
    vertices.push_back( terrainY );
}

void EmitFxQuad( std::vector<float>& vertices,
                 const Vector3& a,
                 const Vector3& b,
                 const Vector3& c,
                 const Vector3& d,
                 float r,
                 float g,
                 float blue,
                 float alpha,
                 float fxKind,
                 float terrainA,
                 float terrainB,
                 float terrainC,
                 float terrainD )
{
    EmitFxVertex( vertices, a, r, g, blue, alpha, 0.0f, 0.0f, fxKind, terrainA );
    EmitFxVertex( vertices, b, r, g, blue, alpha, 1.0f, 0.0f, fxKind, terrainB );
    EmitFxVertex( vertices, c, r, g, blue, alpha, 1.0f, 1.0f, fxKind, terrainC );
    EmitFxVertex( vertices, a, r, g, blue, alpha, 0.0f, 0.0f, fxKind, terrainA );
    EmitFxVertex( vertices, c, r, g, blue, alpha, 1.0f, 1.0f, fxKind, terrainC );
    EmitFxVertex( vertices, d, r, g, blue, alpha, 0.0f, 1.0f, fxKind, terrainD );
}

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


struct ScreenSunPosition
{
    float x = -10.0f;
    float y = -10.0f;
};


ScreenSunPosition
ProjectCinematicSunToScreen( const Vector3& eye, const Matrix4& viewProjection, const CinematicRenderConfig& cinematic )
{
    const Vector3 sunPoint = eye + CinematicSkySunDirection( cinematic ) * 1000.0f;
    const Matrix4& vp = viewProjection;
    const float clipX = vp.m[0] * sunPoint.x + vp.m[4] * sunPoint.y + vp.m[8] * sunPoint.z + vp.m[12];
    const float clipY = vp.m[1] * sunPoint.x + vp.m[5] * sunPoint.y + vp.m[9] * sunPoint.z + vp.m[13];
    const float clipW = vp.m[3] * sunPoint.x + vp.m[7] * sunPoint.y + vp.m[11] * sunPoint.z + vp.m[15];
    if ( clipW <= 0.0001f )
    {
        return {};
    }

    const float invW = 1.0f / clipW;
    return { clipX * invW * 0.5f + 0.5f, clipY * invW * 0.5f + 0.5f };
}


constexpr float FULLSCREEN_QUAD_VERTS[] = {
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f,
};

void DrawFullscreenQuad( SkullbonezCore::Rendering::IRenderCommandContext& renderCommands, uint32_t quadVB )
{
    // Shared post vertex contract: clip-space xy followed by UV. Keeping one
    // copy prevents sky, volumetric, and tonemap from quietly drifting apart.
    renderCommands.UploadAndDrawDynamicVB( quadVB, FULLSCREEN_QUAD_VERTS, 6 );
}

void BindSkyPassParams( SkullbonezCore::Rendering::IShader& shader,
                        const Matrix4& view,
                        const Matrix4& projection,
                        const CinematicRenderConfig& cinematic )
{
    shader.SetVec4( "uSunParams",
                    cinematic.sunScreenX,
                    cinematic.sunScreenY,
                    cinematic.sunIntensity,
                    cinematic.skyGlowStrength );
    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec3( "uHorizonColor", cinematic.skyHorizonR, cinematic.skyHorizonG, cinematic.skyHorizonB );
    shader.SetVec3( "uZenithColor", cinematic.skyZenithR, cinematic.skyZenithG, cinematic.skyZenithB );
    shader.SetMat4( "uInvView", view.Inverse() );
    shader.SetMat4( "uInvProjection", projection.Inverse() );
    shader.SetInt( "uSkyMode", cinematic.skyMode );
    shader.SetVec4( "uCloudParams",
                    cinematic.cloudCoverage,
                    cinematic.cloudSoftness,
                    cinematic.cloudScale,
                    cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
}

void BindVolumetricPassParams( SkullbonezCore::Rendering::IShader& shader,
                               const Vector3& eye,
                               const Matrix4& viewProjection,
                               const CinematicRenderConfig& cinematic )
{
    const ScreenSunPosition sunScreen = ProjectCinematicSunToScreen( eye, viewProjection, cinematic );
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetVec4( "uDepthParams", Cfg().frustumNear, Cfg().frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uSunShaftParams",
                    sunScreen.x,
                    sunScreen.y,
                    cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f,
                    cinematic.sunShaftFalloff );
    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec4( "uVolumetricParams",
                    cinematic.volumetricStrength,
                    cinematic.volumetricDensity,
                    cinematic.volumetricDecay,
                    cinematic.fogDensity );
    shader.SetVec4( "uCloudParams",
                    cinematic.cloudCoverage,
                    cinematic.cloudSoftness,
                    cinematic.cloudScale,
                    cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
}

void BindTonemapPassParams( SkullbonezCore::Rendering::IShader& shader,
                            const Vector3& eye,
                            const Matrix4& viewProjection,
                            const CinematicRenderConfig& cinematic,
                            bool volumetricReady )
{
    const ScreenSunPosition sunScreen = ProjectCinematicSunToScreen( eye, viewProjection, cinematic );
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetInt( "uVolumetricTex", 2 );
    shader.SetFloat( "uExposure", cinematic.exposure );
    shader.SetFloat( "uGamma", cinematic.gamma );
    shader.SetVec4( "uDepthParams", Cfg().frustumNear, Cfg().frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uFogParams",
                    cinematic.fogStart,
                    cinematic.fogEnd,
                    cinematic.fogEnabled ? cinematic.fogDensity : 0.0f,
                    cinematic.fogEnabled ? cinematic.fogMaxOpacity : 0.0f );
    shader.SetVec3( "uFogColor", cinematic.fogColorR, cinematic.fogColorG, cinematic.fogColorB );
    shader.SetVec4( "uSunShaftParams",
                    sunScreen.x,
                    sunScreen.y,
                    cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f,
                    cinematic.sunShaftFalloff );
    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec4( "uBloomParams",
                    cinematic.bloomThreshold,
                    cinematic.bloomKnee,
                    cinematic.bloomEnabled ? cinematic.bloomStrength : 0.0f,
                    cinematic.bloomRadius );
    shader.SetVec4( "uCloudParams",
                    cinematic.cloudCoverage,
                    cinematic.cloudSoftness,
                    cinematic.cloudScale,
                    cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
    shader.SetVec4( "uStyleGrade",
                    cinematic.styleSaturation,
                    cinematic.styleContrast,
                    cinematic.styleVignette,
                    static_cast<float>( cinematic.skyMode ) );
    shader.SetFloat( "uVolumetricCompositeStrength",
                     volumetricReady && cinematic.volumetricLightingEnabled ? 1.0f : 0.0f );
}

} // namespace

void FullscreenQuadPass::EnsureGpuResources( const RenderFrameContext& frame )
{
    if ( !frame.cinematicEnabled || !IsGfxReady() )
    {
        return;
    }

    FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;
    if ( fullscreen.quadVB == 0 )
    {
        // Full-screen shaders draw one rectangle; each vertex stores screen xy
        // plus uv, and every pass gives that same geometry its own shader meaning.
        const int attribs[] = { 2, 2 };
        fullscreen.quadVB = Gfx().CreateDynamicVB( attribs, 2, 6 );
    }
}


void FullscreenQuadPass::ReleaseGpuResources()
{
    FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;
    if ( IsGfxReady() && fullscreen.quadVB != 0 )
    {
        Gfx().DestroyDynamicVB( fullscreen.quadVB );
    }
    fullscreen.quadVB = 0;
}


uint32_t FullscreenQuadPass::QuadVB() const
{
    return m_host.m_systems.renderPasses.fullscreen.quadVB;
}


void SkyPass::EnsureGpuResources( const RenderFrameContext& frame )
{
    if ( !frame.cinematicEnabled || !IsGfxReady() )
    {
        return;
    }

    SkyPassResources& sky = m_host.m_systems.renderPasses.sky;
    if ( !sky.atmosphereShader )
    {
        // Procedural sky shader: draws generated sunset/cloud color when the
        // cinematic config opts out of the authored cube-map skybox.
        sky.atmosphereShader = m_host.m_systems.assets.CreateShader( "shader.sky_atmosphere" );
    }
}


void SkyPass::ReleaseGpuResources()
{
    m_host.m_systems.renderPasses.sky.atmosphereShader.reset();
}


void SceneTargetPass::EnsureGpuResources( const RenderFrameContext& frame )
{
    if ( !frame.cinematicEnabled || !IsGfxReady() )
    {
        return;
    }

    const int w = (std::max)( 1, Gfx().GetWidth() );
    const int h = (std::max)( 1, Gfx().GetHeight() );
    CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    const bool needsSceneTarget =
        !scene.hdrTarget || scene.hdrTarget->GetWidth() != w || scene.hdrTarget->GetHeight() != h ||
        scene.hdrTarget->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
    if ( needsSceneTarget )
    {
        // RGBA16F preserves bright sky/fog values until TonemapPass compresses
        // them back to display color on the window backbuffer.
        if ( scene.hdrTarget )
        {
            scene.hdrTarget->ResetResources();
        }
        scene.hdrTarget.reset();
        scene.hdrTarget = Gfx().CreateFramebuffer( w, h, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }
}


void SceneTargetPass::ReleaseGpuResources()
{
    CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    if ( scene.hdrTarget )
    {
        scene.hdrTarget->ResetResources();
    }
    scene.hdrTarget.reset();
}


bool SceneTargetPass::IsReady() const
{
    return m_host.m_systems.renderPasses.cinematicScene.hdrTarget != nullptr;
}


void ReflectionPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    if ( !IsGfxReady() )
    {
        return;
    }

    ReflectionPassResources& reflection = m_host.m_systems.renderPasses.reflection;
    // Why: the reflection texture is intentionally supersampled relative to the
    // window. Water can then sample it at grazing angles without making the
    // mirrored scene look blocky.
    const int fboW = (std::max)( 1, Gfx().GetWidth() * 2 );
    const int fboH = (std::max)( 1, Gfx().GetHeight() * 2 );
    const bool needsReflectionTarget =
        !reflection.target || reflection.target->GetWidth() != fboW || reflection.target->GetHeight() != fboH ||
        reflection.target->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA8;

    if ( needsReflectionTarget )
    {
        m_host.LogRenderResourceLifecycleStep( "window_resize", "reflection_target_recreate_if_needed" );
        if ( reflection.target )
        {
            reflection.target->ResetResources();
        }
        reflection.target.reset();
        reflection.target = Gfx().CreateFramebuffer( fboW, fboH );
    }
}


void ReflectionPass::ReleaseGpuResources()
{
    ReflectionPassResources& reflection = m_host.m_systems.renderPasses.reflection;
    m_host.LogRenderResourceLifecycleStep( "reflection_reset", "reflection_target" );
    // Lifetime: ResetResources gives the backend a chance to release device
    // objects before the unique_ptr destructor drops the renderer-neutral shell.
    if ( reflection.target )
    {
        reflection.target->ResetResources();
    }
    reflection.target.reset();
}


void ShadowPass::EnsureGpuResources( const RenderFrameContext& /*frame*/, const CinematicRenderConfig& cinematic )
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
        const bool needsTarget = !target || target->GetWidth() != mapSize || target->GetHeight() != mapSize;
        if ( needsTarget )
        {
            target.reset();
            target = Gfx().CreateFramebuffer( mapSize, mapSize );
        }
    };
    ShadowPassResources& shadows = m_host.m_systems.renderPasses.shadows;
    ensureTarget( shadows.terrainTarget );
    ensureTarget( shadows.objectTarget );
}


void ShadowPass::ReleaseGpuResources()
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

    ShadowPassResources& shadows = m_host.m_systems.renderPasses.shadows;
    for ( const ShadowResetPhase& phase : resetSteps )
    {
        m_host.LogRenderResourceLifecycleStep( "shadow_reset", phase.name );
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
            shadows.objectCasterBatches.Clear();
            break;
        }
    }
}


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildTerrainFrameData( const CinematicRenderConfig& cinematic,
                                   const Math::Vector::Vector3& lightDirectionWorld ) const
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildTerrainFrame" );

    Rendering::ShadowFrameData shadowFrame;
    const ShadowPassResources& shadows = m_host.m_systems.renderPasses.shadows;
    if ( !m_host.m_systems.terrain || !shadows.terrainTarget )
    {
        return shadowFrame;
    }

    // Shadow maps need a stable light-space camera. `lightDirectionWorld` is
    // treated as the vector from the scene toward the light source. The visible
    // ordinary and cinematic shaders use the same directional-sun contract, so
    // shadow visibility blocks the direct light the BRDF is actually shading.
    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );

    const XZBounds terrainBounds = m_host.m_systems.terrain->GetXZBounds();
    const float extentX = (std::max)( terrainBounds.m_xMax - terrainBounds.m_xMin, 1.0f );
    const float extentZ = (std::max)( terrainBounds.m_zMax - terrainBounds.m_zMin, 1.0f );
    const float terrainHeightRange =
        (std::max)( m_host.m_systems.terrain->GetMaxHeight() - m_host.m_systems.terrain->GetMinHeight(), 64.0f );
    const float terrainRadius = (std::max)( extentX, extentZ ) * 0.5f;
    const float shadowRadius =
        std::clamp( terrainRadius + 180.0f, 128.0f, (std::max)( cinematic.shadowMaxDistance, 128.0f ) );

    // Center the orthographic projection over the whole terrain instead of the
    // camera. This is a simple single-map v1: it avoids camera-dependent popping
    // and makes screenshots deterministic, at the cost of spreading resolution
    // across the authored terrain bounds instead of using cascades.
    const Vector3 focus( ( terrainBounds.m_xMin + terrainBounds.m_xMax ) * 0.5f,
                         ( m_host.m_systems.terrain->GetMinHeight() + m_host.m_systems.terrain->GetMaxHeight() ) * 0.5f,
                         ( terrainBounds.m_zMin + terrainBounds.m_zMax ) * 0.5f );
    const float lightBackDistance = shadowRadius + terrainHeightRange + 650.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + terrainHeightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection =
        Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
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


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildObjectFrameData( const CinematicRenderConfig& cinematic,
                                  const Math::Vector::Vector3& lightDirectionWorld,
                                  const Math::Vector::Vector3& focusHint,
                                  Rendering::IRenderSceneView& scene )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame" );

    Rendering::ShadowFrameData shadowFrame;
    ShadowPassResources& shadows = m_host.m_systems.renderPasses.shadows;
    if ( !shadows.objectTarget || !cinematic.shadowObjectsCast || !cinematic.shadowObjectsReceive )
    {
        return shadowFrame;
    }

    Vector3 focus;
    float shadowRadius = 0.0f;
    float heightRange = 0.0f;
    const float objectSearchDistance = std::clamp( cinematic.shadowMaxDistance * 0.15f, 180.0f, 320.0f );
    if ( !scene.GetObjectShadowBounds( focusHint, objectSearchDistance, focus, shadowRadius, heightRange ) )
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
    shadowFrame.lightProjection =
        Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius, nearPlane, farPlane );
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


void ShadowPass::RenderShadowMap( Rendering::IFramebuffer& target,
                                  const Rendering::ShadowFrameData& shadowFrame,
                                  const CinematicRenderConfig& cinematic,
                                  Rendering::IRenderCommandContext& renderCommands,
                                  bool renderTerrain,
                                  bool renderObjects,
                                  Rendering::IRenderSceneView& scene,
                                  const Rendering::ShadowCasterBatches* objectCasters )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap" );
    DRAW_CALL_TRACE_SCOPE( "Frame/Shadows/ShadowMap/RenderMap" );

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
    renderCommands.SetViewport( 0, 0, target.GetWidth(), target.GetHeight() );
    renderCommands.Clear( true, true );

    // Shadow depth writes must be opaque and depth-only. Save the caller's
    // blend/depth state because this pass runs in the middle of RenderFrame()
    // before reflection, world rendering, water, UI, and debug overlays.
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( true );
    renderCommands.SetBlend( false );
    renderCommands.SetCullFace( true );

    // Polygon offset reduces self-shadow acne on terrain and object faces. The
    // shader-side receiver bias handles comparison precision; this rasterizer
    // bias handles the depth values written into the map.
    renderCommands.SetPolygonOffset( true, 2.0f, 4.0f );
    // Pass contract: shadow depth shaders write depth only and sample no
    // textures. Clear inherited slots so descriptor state from the visible
    // scene cannot leak into this off-screen pass.
    ClearAllRenderTextureSlots( renderCommands );

    if ( renderTerrain && cinematic.shadowTerrainCasts && !m_host.m_debug.isTerrainHidden && m_host.m_systems.terrain )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );
        DRAW_CALL_TRACE_SCOPE( "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );

        // Terrain must cast with the same optional render-only relief that the
        // visible terrain uses. Otherwise cinematic basin relief would receive
        // shadows from the flat CPU height map and the contact would visibly
        // detach. With normal rendering the relief amount is zero by default.
        m_host.m_systems.terrain->RenderShadowDepth( shadowFrame.lightView, shadowFrame.lightProjection, &cinematic );
    }

    if ( renderObjects && cinematic.shadowObjectsCast && !m_host.m_debug.isCollisionVisualizer )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );
        DRAW_CALL_TRACE_SCOPE( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );

        // Balls, boxes, and pine-style box visuals all write depth here. The
        // scene view keeps separate instanced batches so each caster shape uses
        // the same mesh silhouette as the visible forward pass.
        if ( objectCasters )
        {
            scene.RenderShadowCasterBatches( *objectCasters,
                                             shadowFrame.lightView,
                                             shadowFrame.lightProjection,
                                             &cinematic );
        }
        else
        {
            scene.RenderShadowCasters( shadowFrame.lightView, shadowFrame.lightProjection, &cinematic );
        }
    }

    renderCommands.SetPolygonOffset( false );
    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    target.Unbind();
    renderCommands.SetViewport( 0, 0, m_host.WindowScreenWidth(), m_host.WindowScreenHeight() );
}


ShadowPassOutput ShadowPass::Render( const ShadowPassInputs& inputs )
{
    ShadowPassResources& shadows = m_host.m_systems.renderPasses.shadows;
    // Invariant: always clear the receiver payloads at the start of the pass.
    // If shadows are disabled, downstream terrain/object passes must see null
    // outputs instead of last frame's depth texture handles.
    shadows.terrainFrame = Rendering::ShadowFrameData();
    shadows.objectFrame = Rendering::ShadowFrameData();
    shadows.objectCasterBatches.Clear();
    if ( inputs.cinematic )
    {
        if ( !inputs.frame.scene )
        {
            return ShadowPassOutput();
        }

        // Build shadow maps before any receiver pass. Terrain receives the broad
        // map, while objects receive a second tight map centered on nearby bodies
        // so ball-on-ball shadows have enough texel density.
        PROFILE_SCOPED( "Frame/Shadows" );
        DRAW_CALL_TRACE_SCOPE( "Frame/Shadows" );
        PROFILE_GPU_BEGIN( "Frame/Shadows/ShadowMap" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/Shadows/ShadowMap" );
            Vector3 lightDirection( inputs.frame.lightPosition[0],
                                    inputs.frame.lightPosition[1],
                                    inputs.frame.lightPosition[2] );
            EnsureGpuResources( inputs.frame, *inputs.cinematic );
            Rendering::ShadowCasterBatches& objectCasters = shadows.objectCasterBatches;
            const bool shouldBuildObjectCasters =
                inputs.cinematic->shadowObjectsCast && !m_host.m_debug.isCollisionVisualizer;
            if ( shouldBuildObjectCasters )
            {
                inputs.frame.scene->BuildShadowCasterBatches( objectCasters );
            }
            shadows.terrainFrame = BuildTerrainFrameData( *inputs.cinematic, lightDirection );
            if ( shadows.terrainTarget )
            {
                RenderShadowMap( *shadows.terrainTarget,
                                 shadows.terrainFrame,
                                 *inputs.cinematic,
                                 RenderCommands( inputs.frame ),
                                 true,
                                 true,
                                 *inputs.frame.scene,
                                 &objectCasters );
            }
            // Anchor the tight object-shadow map to the render look target, not
            // the eye. Locked/inspect zoom moves the eye around a stable target;
            // using the eye makes nearby-object bounds pop as the user zooms.
            shadows.objectFrame =
                BuildObjectFrameData( *inputs.cinematic, lightDirection, inputs.frame.viewCenter, *inputs.frame.scene );
            if ( shadows.objectTarget )
            {
                RenderShadowMap( *shadows.objectTarget,
                                 shadows.objectFrame,
                                 *inputs.cinematic,
                                 RenderCommands( inputs.frame ),
                                 false,
                                 true,
                                 *inputs.frame.scene,
                                 &objectCasters );
            }
        }
        PROFILE_GPU_END( "Frame/Shadows/ShadowMap" );
    }

    ShadowPassOutput output;
    output.terrainShadow = shadows.terrainFrame.valid ? &shadows.terrainFrame : nullptr;
    output.objectShadow = shadows.objectFrame.valid ? &shadows.objectFrame : output.terrainShadow;
    return output;
}


void SkyPass::RenderCinematicSky( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view )
{
    const CinematicRenderConfig& cinematic = m_host.ActiveCinematicConfig();
    SkyPassResources& sky = m_host.m_systems.renderPasses.sky;
    FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;
    if ( !cinematic.skyAtmosphereEnabled || !sky.atmosphereShader || fullscreen.quadVB == 0 )
    {
        return;
    }

    // The sky is painted as a full-screen background. It should not test against
    // terrain depth and it should not blend with whatever was previously there.
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    // Pass contract: this generated sky samples no textures. Clear inherited
    // SRV slots before the fullscreen draw so stale pass inputs cannot be
    // recopied by the backend while the sky shader is active.
    ClearAllRenderTextureSlots( renderCommands );
    sky.atmosphereShader->Use();
    BindSkyPassParams( *sky.atmosphereShader, view, frame.projection, cinematic );
    DrawFullscreenQuad( renderCommands, fullscreen.quadVB );

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
}


void SkyPass::Render( const RenderFrameContext& frame, const Math::Transformation::Matrix4& view, SkyPassMode mode )
{
    const bool useCinematicAtmosphere =
        mode == SkyPassMode::CinematicIfEnabled && frame.cinematic && frame.cinematic->skyAtmosphereEnabled;
    if ( useCinematicAtmosphere )
    {
        RenderCinematicSky( frame, view );
        return;
    }

    // The cube-map sky follows camera X/Z so the box feels infinitely far away,
    // while its Y stays authored by config to preserve the long-standing horizon.
    Matrix4 skyView = view * Matrix4::Translate( frame.eye.x, Cfg().skyboxRenderHeight, frame.eye.z ) *
                      Matrix4::Scale( Cfg().skyboxScale );
    // Pass contract: cube-map skybox faces sample only slot 0. Slots owned by
    // water, post, or shadows must not leak into these six mesh draws.
    ClearRenderTextureSlotsExcept( RenderCommands( frame ), RENDER_TEXTURE_SLOT_0 );
    m_host.m_systems.skyBox->Render( skyView, frame.projection );
}


void SceneTargetPass::Begin( const RenderFrameContext& frame, SkyPass& skyPass )
{
    // Invariant: from this point onward, draw the world into the HDR scene
    // target instead of directly into the window. The post pass later moves it
    // to the backbuffer with the cinematic effects applied.
    CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    scene.hdrTarget->Bind();
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    renderCommands.SetViewport( 0, 0, scene.hdrTarget->GetWidth(), scene.hdrTarget->GetHeight() );
    renderCommands.Clear( true, true );

    PROFILE_GPU_BEGIN( "Frame/Render/CinematicSky" );
    {
        DRAW_CALL_TRACE_SCOPE( "Frame/Render/CinematicSky" );
        skyPass.Render( frame, frame.baseView, SkyPassMode::CinematicIfEnabled );
    }
    PROFILE_GPU_END( "Frame/Render/CinematicSky" );
}


ReflectionPassOutput ReflectionPass::Render( const ReflectionPassInputs& inputs, SkyPass& skyPass )
{
    ReflectionPassOutput output;

    // Concept: two implementations, one water-pass contract. The planar path
    // renders the above-water scene from a mirrored camera into an FBO. The DXR
    // path rebuilds the raytracing TLAS and writes a screen-space reflection
    // texture directly. Both feed the same water shader later.
    PROFILE_GPU_BEGIN( "Frame/Render/Reflection" );
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/Reflection" );
    const auto renderCapabilities = Gfx().GetCapabilities();
    const bool useDxrReflection = renderCapabilities.supportsDxrReflection && IsGfxRayTracingReady() &&
                                  m_host.m_debug.isWaterRTReflect && !m_host.m_debug.isWaterNoReflect &&
                                  !inputs.collisionStateColorsVisible && !inputs.transparentBodyPass;
    output.usedDxr = useDxrReflection;

    if ( useDxrReflection )
    {
        auto& rayTracing = GfxRayTracing();
        // Lifetime: the DX12 backend owns the raytracing acceleration
        // structures. The scene view streams current per-model transforms into
        // the TLAS before dispatching one reflection ray per texture pixel.
        const int ballCount = inputs.frame.scene
                                  ? inputs.frame.scene->CopyDxrModelMatrices(
                                        m_host.m_dxrReflectionTransforms.data(),
                                        static_cast<int>( m_host.m_dxrReflectionTransforms.size() / 16 ) )
                                  : 0;

        // Terrain/sphere BLAS objects are owned by the DX12 backend, so the
        // runtime supplies only per-instance sphere transforms here.
        rayTracing.BuildTLAS( m_host.m_dxrReflectionTransforms.data(), ballCount, 0, 0 );

        // Ray generation reconstructs world-space rays from screen pixels, so
        // it needs the inverse of the main camera view-projection matrix.
        Matrix4 invVP = inputs.frame.viewProjection.Inverse();
        float cameraPos[3] = { inputs.frame.eye.x, inputs.frame.eye.y, inputs.frame.eye.z };
        float simTime = static_cast<float>( m_host.m_timers.simulationTimer.GetTotalTime() );

        uint32_t sphereHandle = m_host.TextureHandle( TEXTURE_BOUNDING_SPHERE );
        uint32_t terrainHandle = m_host.TextureHandle( TEXTURE_GROUND );
        uint32_t skyUpHandle = m_host.TextureHandle( TEXTURE_SKY_UP );
        uint32_t skyDownHandle = m_host.TextureHandle( TEXTURE_SKY_DOWN );
        uint32_t skyRightHandle = m_host.TextureHandle( TEXTURE_SKY_RIGHT );
        uint32_t skyLeftHandle = m_host.TextureHandle( TEXTURE_SKY_LEFT );
        uint32_t skyFrontHandle = m_host.TextureHandle( TEXTURE_SKY_FRONT );
        uint32_t skyBackHandle = m_host.TextureHandle( TEXTURE_SKY_BACK );
        rayTracing.DispatchReflectionRays( invVP.Data(),
                                           cameraPos,
                                           inputs.frame.waterY,
                                           simTime,
                                           inputs.frame.lightPosition,
                                           m_host.WindowScreenWidth() * 2,
                                           m_host.WindowScreenHeight() * 2,
                                           sphereHandle,
                                           terrainHandle,
                                           skyUpHandle,
                                           skyDownHandle,
                                           skyRightHandle,
                                           skyLeftHandle,
                                           skyFrontHandle,
                                           skyBackHandle );
        output.reflectionTextureHandle = rayTracing.GetReflectionUAVTexture();
        output.reflectionSampleViewProjection = inputs.frame.viewProjection;
    }
    else
    {
        // Invariant: the planar path binds only its own reflection target and
        // restores the viewport to the window size before water renders.
        Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
        ReflectionPassResources& reflectionResources = m_host.m_systems.renderPasses.reflection;
        reflectionResources.target->Bind();
        renderCommands.SetViewport( 0,
                                    0,
                                    reflectionResources.target->GetWidth(),
                                    reflectionResources.target->GetHeight() );
        renderCommands.Clear( true, true );

        // Skybox reflected (XZ follows eye; Y anchored at Cfg().skyboxRenderHeight).
        // Cinematic mode can reflect the generated sunset sky into the water
        // instead of the usual cube-map sky.
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( "Frame/Render/Reflection/Skybox" );
            skyPass.Render( inputs.frame, inputs.frame.reflectionView, SkyPassMode::CinematicIfEnabled );
        }
        PROFILE_GPU_END( "Frame/Render/Reflection/Skybox" );

        // Why: clip at the water surface so the reflection texture contains only
        // the above-water portion of models. The water shader supplies the
        // below-surface visual from the main scene.
        PROFILE_GPU_BEGIN( "Frame/Render/Reflection/Balls" );
        DRAW_CALL_TRACE_SCOPE( "Frame/Render/Reflection/Balls" );
        renderCommands.SetClipPlane( 0, true );
        RenderHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        m_host.m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.frame.waterY );
        if ( inputs.collisionStateColorsVisible )
        {
            // Pass contract: collision-state solids are vertex-colored and do
            // not sample textures.
            ClearAllRenderTextureSlots( renderCommands );
            if ( inputs.frame.scene )
            {
                inputs.frame.scene->RenderCollisionStateSolids( m_host.m_collisionVisualizer,
                                                                inputs.frame.reflectionView,
                                                                inputs.frame.projection,
                                                                inputs.frame.lightPosition,
                                                                inputs.collisionVisualizerAlphaOverride );
            }
        }
        else
        {
            // Pass contract: reflected lit models read material color from slot
            // 0 and optional shadow depth from slot 3.
            ClearRenderTextureSlotsExcept(
                renderCommands,
                RENDER_TEXTURE_SLOT_0 |
                    ( inputs.objectShadow && inputs.objectShadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );
            m_host.SelectRenderTexture( TEXTURE_BOUNDING_SPHERE );
            if ( inputs.frame.scene )
            {
                inputs.frame.scene->RenderModels( inputs.frame.reflectionView,
                                                  inputs.frame.projection,
                                                  inputs.frame.lightPosition,
                                                  inputs.cinematic,
                                                  inputs.objectShadow,
                                                  inputs.bodyAlpha );
            }
        }
        renderCommands.SetClipPlane( 0, false );
        RenderHelper::SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_host.m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( "Frame/Render/Reflection/Balls" );

        reflectionResources.target->Unbind();
        renderCommands.SetViewport( 0, 0, m_host.WindowScreenWidth(), m_host.WindowScreenHeight() );
        output.reflectionTextureHandle = reflectionResources.target->GetColorTextureHandle();
        output.reflectionSampleViewProjection = inputs.frame.reflectionViewProjection;
    }
    PROFILE_GPU_END( "Frame/Render/Reflection" );
    return output;
}


void ObjectPass::Render( const ObjectPassInputs& inputs )
{
    const bool transparentPass = inputs.mode == ObjectPassMode::Transparent;
    const char* passName = transparentPass ? "Frame/Render/TransparentBalls" : "Frame/Render/Balls";
    const uint32_t passHash =
        transparentPass ? HashStr( "Frame/Render/TransparentBalls" ) : HashStr( "Frame/Render/Balls" );
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    GpuProfilerScope profileScope( passName, passHash );
#endif
    Rendering::DrawCallTraceScope drawTraceScope( passName, passHash );

    if ( inputs.collisionStateColorsVisible )
    {
        // Pass contract: collision-state solids are vertex-colored and do not
        // sample textures.
        ClearAllRenderTextureSlots( RenderCommands( inputs.frame ) );
        if ( inputs.frame.scene )
        {
            inputs.frame.scene->RenderCollisionStateSolids( m_host.m_collisionVisualizer,
                                                            inputs.frame.baseView,
                                                            inputs.frame.projection,
                                                            inputs.frame.lightPosition,
                                                            inputs.collisionVisualizerAlphaOverride );
        }
    }
    else
    {
        // Pass contract: lit model shaders read the material texture in slot 0
        // and optionally the shadow depth texture in slot 3.
        ClearRenderTextureSlotsExcept(
            RenderCommands( inputs.frame ),
            RENDER_TEXTURE_SLOT_0 | ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );
        m_host.SelectRenderTexture( TEXTURE_BOUNDING_SPHERE );
        if ( inputs.frame.scene )
        {
            inputs.frame.scene->RenderModels( inputs.frame.baseView,
                                              inputs.frame.projection,
                                              inputs.frame.lightPosition,
                                              inputs.cinematic,
                                              inputs.shadow,
                                              inputs.bodyAlpha,
                                              inputs.modelMask,
                                              inputs.drawMaskedModels );
        }
    }
}


void ObjectPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    // Object mesh/shader resources live behind the scene view; this pass owns
    // the draw contract and texture-slot hygiene, not the model cache.
}


void ObjectPass::ReleaseGpuResources()
{
    // Nothing to release until body shaders/materials move behind this pass.
}


void TerrainPass::Render( const TerrainPassInputs& inputs )
{
    if ( m_host.m_debug.isTerrainHidden )
    {
        return;
    }

    PROFILE_GPU_BEGIN( "Frame/Render/Terrain" );
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/Terrain" );
    // Pass contract: terrain reads ground albedo from slot 0 and optional
    // shadow depth from slot 3.
    ClearRenderTextureSlotsExcept(
        RenderCommands( inputs.frame ),
        RENDER_TEXTURE_SLOT_0 | ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );
    m_host.SelectRenderTexture( TEXTURE_GROUND );
    m_host.m_systems.terrain->Render( inputs.frame.baseView,
                                      inputs.frame.projection,
                                      inputs.frame.lightPosition,
                                      inputs.cinematic,
                                      inputs.shadow );
    PROFILE_GPU_END( "Frame/Render/Terrain" );
}


void TerrainPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    // Terrain mesh/material resources live on Terrain; this pass owns ordering
    // and the receiver texture-slot contract.
}


void TerrainPass::ReleaseGpuResources()
{
    // Terrain releases its backend resources through terrain lifecycle hooks.
}


void WaterPass::Render( const WaterPassInputs& inputs )
{
    m_debugInfo = WaterPassDebugInfo();
    m_debugInfo.skippedHidden = inputs.waterHidden;
    m_debugInfo.skippedModeOff = inputs.frame.cinematicEnabled && inputs.cinematic && inputs.cinematic->waterMode == 0;
    m_debugInfo.reflectionTextureHandle = inputs.reflection.reflectionTextureHandle;
    m_debugInfo.reflectionValid = inputs.reflection.reflectionTextureHandle != 0;
    m_debugInfo.reflectionRaytraced = inputs.reflection.usedDxr;
    m_debugInfo.noReflection = inputs.noReflection;
    m_debugInfo.flatWater = inputs.flatWater;
    m_debugInfo.freezeTime = inputs.freezeTime;
    m_debugInfo.styleWaterMode = inputs.cinematic ? inputs.cinematic->waterMode : -1;

    if ( m_debugInfo.skippedHidden || m_debugInfo.skippedModeOff )
    {
        return;
    }

    PROFILE_GPU_BEGIN( "Frame/Render/Water" );
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/Water" );
    // Pass contract: water samples only the reflection texture in slot 1.
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    ClearRenderTextureSlotsExcept( renderCommands, RENDER_TEXTURE_SLOT_1 );
    float waterTime = inputs.freezeTime ? inputs.frozenTime
                                        : static_cast<float>( m_host.m_timers.simulationTimer.GetTimeSinceLastStart() );
    m_debugInfo.rendered = true;
    m_debugInfo.waterTime = waterTime;
    SkullbonezCore::Environment::WaterReflectionInput reflectionInput;
    reflectionInput.sampleViewProjection = inputs.reflection.reflectionSampleViewProjection;
    reflectionInput.textureHandle = inputs.reflection.reflectionTextureHandle;
    reflectionInput.noReflection = inputs.noReflection;
    reflectionInput.raytraced = inputs.reflection.usedDxr;

    const bool depthTestWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool depthWriteWasEnabled = renderCommands.IsDepthWriteEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    Rendering::BlendFactor blendSrc = Rendering::BlendFactor::One;
    Rendering::BlendFactor blendDst = Rendering::BlendFactor::Zero;
    renderCommands.GetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::OneMinusSrcAlpha );
    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( false );
    m_host.m_cWorldEnvironment.RenderFluid( inputs.frame.baseView,
                                            inputs.frame.projection,
                                            inputs.frame.eye,
                                            reflectionInput,
                                            waterTime,
                                            inputs.flatWater,
                                            inputs.frame.cinematicEnabled,
                                            inputs.cinematic );
    renderCommands.SetDepthWrite( depthWriteWasEnabled );
    renderCommands.SetDepthTest( depthTestWasEnabled );
    renderCommands.SetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( blendWasEnabled );
    PROFILE_GPU_END( "Frame/Render/Water" );
}


void WaterPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    // Water shader/mesh resources are owned by WorldEnvironment; this pass
    // makes reflection input explicit and keeps water downstream of reflection.
}


void WaterPass::ReleaseGpuResources()
{
    // WorldEnvironment owns fluid render resources.
}


void TornadoVisualPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    const TornadoVisualSettings& visual = m_host.m_runtimeSettings.tornadoVisual;
    const int ribbonCount = std::clamp( visual.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( visual.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( visual.particleCount, 0, 256 );
    constexpr int dustBands = 3;
    constexpr int dustSegments = 56;
    const int authoredVortexCount =
        m_host.m_runtimeSettings.tornadoSystem.enabled
            ? (std::max)( 1, static_cast<int>( m_host.m_runtimeSettings.tornadoSystem.vortices.size() ) )
            : 1;
    const int vertexCount =
        authoredVortexCount * ( ribbonCount * ribbonSegments * 6 + dustBands * dustSegments * 6 + particleCount * 6 );
    const std::size_t floatCapacity =
        static_cast<std::size_t>( (std::max)( vertexCount, 0 ) ) * TORNADO_VISUAL_FLOATS_PER_VERTEX;
    if ( m_vertices.capacity() < floatCapacity )
    {
        m_vertices.reserve( floatCapacity );
    }
}


void TornadoVisualPass::ReleaseGpuResources()
{
    m_vertices.clear();
    m_vertices.shrink_to_fit();
    m_activeVisualVortices.clear();
    m_activeVisualVortices.shrink_to_fit();
    m_liveVisualTimeSeconds = 0.0f;
    m_lastLiveVisualSourceSeconds = 0.0;
    m_hasLiveVisualTime = false;
}


bool TornadoVisualPass::Render( const TornadoVisualPassInputs& inputs )
{
    const TornadoVisualSettings& visual = m_host.m_runtimeSettings.tornadoVisual;
    if ( !visual.enabled || !IsGfxReady() )
    {
        return false;
    }

    const int ribbonCount = std::clamp( visual.ribbonCount, 0, 16 );
    const int ribbonSegments = std::clamp( visual.ribbonSegments, 2, 96 );
    const int particleCount = std::clamp( visual.particleCount, 0, 256 );
    const float shellAlpha = std::clamp( visual.shellAlpha, 0.0f, 0.30f );
    const float dustAlpha = std::clamp( visual.dustAlpha, 0.0f, 0.30f );
    if ( ( ribbonCount <= 0 || shellAlpha <= 0.0f ) && dustAlpha <= 0.0f )
    {
        return false;
    }

    const float twoPi = 6.28318530718f;
    const auto* replaySample = m_host.CurrentReplayScrubSample();
    const auto* solverSample = replaySample ? nullptr : m_host.CurrentReplaySolverScrubSample();
    const auto* predictionFrame =
        ( replaySample || solverSample ) ? nullptr : m_host.CurrentReplayPredictionScrubFrame();
    const bool useReplayTime = replaySample != nullptr || solverSample != nullptr || predictionFrame != nullptr;
    const bool useTornadoSystem =
        m_host.m_runtimeSettings.tornadoSystem.enabled && !m_host.m_runtimeSettings.tornadoSystem.vortices.empty();
    const double sourceSeconds = m_host.m_timers.simulationTimer.GetTimeSinceLastStart();
    if ( !m_hasLiveVisualTime || sourceSeconds < m_lastLiveVisualSourceSeconds )
    {
        m_liveVisualTimeSeconds = static_cast<float>( sourceSeconds );
        m_hasLiveVisualTime = true;
    }
    else if ( !useReplayTime && !m_host.ReplayLiveAdvanceHeld() )
    {
        m_liveVisualTimeSeconds += static_cast<float>( sourceSeconds - m_lastLiveVisualSourceSeconds );
    }
    m_lastLiveVisualSourceSeconds = sourceSeconds;

    float time = m_liveVisualTimeSeconds;
    if ( replaySample )
    {
        time = static_cast<float>( replaySample->simulationSeconds );
    }
    else if ( solverSample )
    {
        time = useTornadoSystem ? solverSample->worldSnapshot.tornadoSystemElapsedSeconds
                                : static_cast<float>( solverSample->simulationSeconds );
    }
    else if ( predictionFrame )
    {
        time = useTornadoSystem ? predictionFrame->tornadoSystemElapsedSeconds
                                : static_cast<float>( predictionFrame->simulationSeconds );
    }
    else if ( useTornadoSystem )
    {
        time = m_host.m_cGameModelCollection.GetTornadoSystemElapsedSeconds();
    }

    m_activeVisualVortices.clear();
    if ( useTornadoSystem )
    {
        Physics::TornadoSystem::BuildActiveVortices( m_host.m_runtimeSettings.tornadoSystem,
                                                     time,
                                                     m_activeVisualVortices );
    }
    else
    {
        const Physics::TornadoFieldConfig& field = m_host.m_runtimeSettings.tornadoField;
        if ( field.enabled && field.radius > 1.0f && field.height > 1.0f )
        {
            Physics::TornadoActiveVortex active;
            active.field = field;
            active.strength = 1.0f;
            active.ageSeconds = time;
            active.sourceIndex = 0;
            m_activeVisualVortices.push_back( active );
        }
    }
    if ( m_activeVisualVortices.empty() )
    {
        return false;
    }

    m_vertices.clear();
    const Vector3 cameraForward =
        NormalizeOr( inputs.frame.viewCenter - inputs.frame.eye, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 cameraUp = NormalizeOr( inputs.frame.up, Vector3( 0.0f, 1.0f, 0.0f ) );
    const Vector3 cameraRight = NormalizeOr( CrossProduct( cameraForward, cameraUp ), Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 billboardUp = NormalizeOr( CrossProduct( cameraRight, cameraForward ), cameraUp );
    const auto terrainHeightFor = [&]( const Vector3& position )
    {
        if ( m_host.m_systems.terrain && m_host.m_systems.terrain->IsInBounds( position.x, position.z ) )
        {
            return m_host.m_systems.terrain->GetTerrainHeightAt( position.x, position.z );
        }
        return position.y - 64.0f;
    };

    for ( const Physics::TornadoActiveVortex& activeVortex : m_activeVisualVortices )
    {
        const Physics::TornadoFieldConfig& field = activeVortex.field;
        const float rotation = time * visual.rotationSpeed + static_cast<float>( activeVortex.sourceIndex ) * 1.73f;
        const float radius = field.radius;
        const float height = field.height;

        if ( shellAlpha > 0.0f )
        {
            constexpr float shellTurns = 2.85f;
            for ( int ribbon = 0; ribbon < ribbonCount; ++ribbon )
            {
                const float ribbonSeed = HashUnitFloat( 41u + static_cast<uint32_t>( ribbon ) * 97u );
                const float phase = static_cast<float>( ribbon ) * twoPi / static_cast<float>( ribbonCount ) +
                                    rotation + ribbonSeed * 0.45f;
                for ( int segment = 0; segment < ribbonSegments; ++segment )
                {
                    const float t0 = static_cast<float>( segment ) / static_cast<float>( ribbonSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( ribbonSegments );
                    const float angle0 = phase + t0 * shellTurns * twoPi;
                    const float angle1 = phase + t1 * shellTurns * twoPi;
                    const float radius0 =
                        radius * ( 0.32f + 0.46f * t0 + 0.035f * sinf( angle0 * 1.7f + ribbonSeed * twoPi ) );
                    const float radius1 =
                        radius * ( 0.32f + 0.46f * t1 + 0.035f * sinf( angle1 * 1.7f + ribbonSeed * twoPi ) );
                    const Vector3 p0 =
                        field.center + CylindricalOffset( radius0, angle0 ) + Vector3( 0.0f, t0 * height, 0.0f );
                    const Vector3 p1 =
                        field.center + CylindricalOffset( radius1, angle1 ) + Vector3( 0.0f, t1 * height, 0.0f );
                    const Vector3 segmentCenter = ( p0 + p1 ) * 0.5f;
                    const Vector3 viewDir = NormalizeOr( inputs.frame.eye - segmentCenter, -cameraForward );
                    const Vector3 tangent = NormalizeOr( p1 - p0, Vector3( 0.0f, 1.0f, 0.0f ) );
                    const Vector3 side = NormalizeOr( CrossProduct( viewDir, tangent ), cameraRight );
                    const float width = (std::max)( 1.0f, visual.ribbonWidth * ( 0.78f + 0.34f * t0 ) );
                    const float baseFade = Clamp01( t0 / 0.16f );
                    const float topFade = Clamp01( ( 1.0f - t0 ) / 0.18f );
                    const float gapFade = 0.72f + 0.28f * sinf( phase + t0 * twoPi * 4.0f );
                    const float alpha = shellAlpha * baseFade * topFade * gapFade;
                    const float cool = 0.72f + 0.08f * t0;
                    EmitFxQuad( m_vertices,
                                p0 - side * width,
                                p1 - side * width,
                                p1 + side * width,
                                p0 + side * width,
                                cool,
                                0.78f,
                                0.84f,
                                alpha,
                                TORNADO_FX_KIND_RIBBON,
                                0.0f,
                                0.0f,
                                0.0f,
                                0.0f );
                }
            }
        }

        if ( dustAlpha > 0.0f )
        {
            constexpr int dustBands = 3;
            constexpr int dustSegments = 56;
            for ( int band = 0; band < dustBands; ++band )
            {
                const float bandT = static_cast<float>( band ) / static_cast<float>( dustBands - 1 );
                const float phase = rotation * ( 1.15f + bandT * 0.35f ) + bandT * twoPi * 0.37f;
                for ( int segment = 0; segment < dustSegments; ++segment )
                {
                    if ( ( segment + band * 3 ) % 5 == 0 || ( segment + band ) % 11 == 0 )
                    {
                        continue;
                    }
                    const float t0 = static_cast<float>( segment ) / static_cast<float>( dustSegments );
                    const float t1 = static_cast<float>( segment + 1 ) / static_cast<float>( dustSegments );
                    const float angle0 = phase + t0 * twoPi * 1.18f;
                    const float angle1 = phase + t1 * twoPi * 1.18f;
                    const float bandRadius = radius * ( 0.58f + 0.16f * static_cast<float>( band ) );
                    const float innerRadius = bandRadius - radius * 0.015f;
                    const float outerRadius = bandRadius + radius * ( 0.024f + 0.008f * bandT );
                    const float y0 =
                        field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle0 * 2.0f ) * 1.6f;
                    const float y1 =
                        field.center.y + height * ( 0.018f + 0.030f * bandT ) + sinf( angle1 * 2.0f ) * 1.6f;
                    const Vector3 a = field.center + CylindricalOffset( innerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );
                    const Vector3 b = field.center + CylindricalOffset( innerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );
                    const Vector3 c = field.center + CylindricalOffset( outerRadius, angle1 ) +
                                      Vector3( 0.0f, y1 - field.center.y, 0.0f );
                    const Vector3 d = field.center + CylindricalOffset( outerRadius, angle0 ) +
                                      Vector3( 0.0f, y0 - field.center.y, 0.0f );
                    const float alpha = dustAlpha * ( 0.42f - 0.08f * bandT );
                    EmitFxQuad( m_vertices,
                                a,
                                b,
                                c,
                                d,
                                0.58f,
                                0.47f,
                                0.31f,
                                alpha,
                                TORNADO_FX_KIND_DUST,
                                terrainHeightFor( a ),
                                terrainHeightFor( b ),
                                terrainHeightFor( c ),
                                terrainHeightFor( d ) );
                }
            }

            for ( int particle = 0; particle < particleCount; ++particle )
            {
                const uint32_t seed = 0x9e3779b9u + static_cast<uint32_t>( particle ) * 0x85ebca6bu;
                const float h0 = HashUnitFloat( seed );
                const float h1 = HashUnitFloat( seed ^ 0x68bc21ebu );
                const float h2 = HashUnitFloat( seed ^ 0x02e5be93u );
                const float heightT = powf( h0, 1.45f );
                const float angularSpeed = 0.65f + heightT * 1.25f;
                const float angle = h1 * twoPi + rotation * angularSpeed + heightT * twoPi * 2.2f;
                const float particleRadius = radius * ( 0.55f + 0.43f * h2 );
                const Vector3 center =
                    field.center + CylindricalOffset( particleRadius, angle ) + Vector3( 0.0f, height * heightT, 0.0f );
                const float size = std::clamp( radius * ( 0.010f + 0.020f * ( 1.0f - heightT ) ), 2.0f, 9.0f );
                const float alpha = dustAlpha * ( 0.38f + 0.42f * ( 1.0f - heightT ) ) * ( 0.55f + 0.45f * h1 );
                const Vector3 right = cameraRight * size;
                const Vector3 up = billboardUp * ( size * ( 0.70f + 0.50f * h2 ) );
                const Vector3 a = center - right - up;
                const Vector3 b = center + right - up;
                const Vector3 c = center + right + up;
                const Vector3 d = center - right + up;
                EmitFxQuad( m_vertices,
                            a,
                            b,
                            c,
                            d,
                            0.68f,
                            0.52f,
                            0.34f,
                            alpha,
                            TORNADO_FX_KIND_DUST,
                            terrainHeightFor( a ),
                            terrainHeightFor( b ),
                            terrainHeightFor( c ),
                            terrainHeightFor( d ) );
            }
        }
    }

    if ( m_vertices.empty() )
    {
        return false;
    }

    PROFILE_GPU_BEGIN( "Frame/Render/TornadoVisual" );
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/TornadoVisual" );
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( inputs.frame );
    ClearAllRenderTextureSlots( renderCommands );
    const bool depthTestWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool depthWriteWasEnabled = renderCommands.IsDepthWriteEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    const bool cullWasEnabled = renderCommands.IsCullFaceEnabled();
    Rendering::BlendFactor blendSrc = Rendering::BlendFactor::One;
    Rendering::BlendFactor blendDst = Rendering::BlendFactor::Zero;
    renderCommands.GetBlendFunc( blendSrc, blendDst );

    renderCommands.SetDepthTest( true );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( true );
    renderCommands.SetBlendFunc( Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::OneMinusSrcAlpha );
    renderCommands.SetCullFace( false );
    renderCommands.DrawTransientColoredTriangles(
        m_vertices.data(),
        static_cast<int>( m_vertices.size() / TORNADO_VISUAL_FLOATS_PER_VERTEX ),
        inputs.frame.viewProjection.Data() );
    renderCommands.SetCullFace( cullWasEnabled );
    renderCommands.SetBlendFunc( blendSrc, blendDst );
    renderCommands.SetBlend( blendWasEnabled );
    renderCommands.SetDepthWrite( depthWriteWasEnabled );
    renderCommands.SetDepthTest( depthTestWasEnabled );
    PROFILE_GPU_END( "Frame/Render/TornadoVisual" );
    return true;
}


void DebugOverlayPass::Render( const DebugOverlayPassInputs& inputs )
{
    // Debug overlays intentionally stay out of the object/material pass. They
    // draw diagnostic geometry over the final world view and should not inherit
    // production material binding assumptions.
    if ( !HasOverlayWork( inputs ) )
    {
        return;
    }

    const bool detailMarkers = PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/DebugOverlay" );
    }
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/DebugOverlay" );
    if ( m_host.m_debug.isBroadphaseOverlay )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( "Frame/Render/DebugOverlay/Broadphase" );
        }
        DRAW_CALL_TRACE_SCOPE( "Broadphase" );
        m_host.m_broadphaseVisualizer.Render( inputs.frame.viewProjection );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( "Frame/Render/DebugOverlay/Broadphase" );
        }
    }

    const auto tornadoSystemVectorsVisible = []( const Physics::TornadoSystemConfig& config )
    {
        if ( config.visualizeVelocityField )
        {
            return true;
        }
        for ( const Physics::TornadoVortexConfig& vortex : config.vortices )
        {
            if ( vortex.field.visualizeVelocityField )
            {
                return true;
            }
        }
        return false;
    };
    const bool tornadoVectorsVisible = m_host.m_runtimeSettings.tornadoField.visualizeVelocityField ||
                                       tornadoSystemVectorsVisible( m_host.m_runtimeSettings.tornadoSystem );
    if ( tornadoVectorsVisible )
    {
        if ( inputs.frame.scene )
        {
            if ( detailMarkers )
            {
                PROFILE_GPU_BEGIN( "Frame/Render/DebugOverlay/TornadoField" );
            }
            DRAW_CALL_TRACE_SCOPE( "TornadoField" );
            inputs.frame.scene->RenderTornadoFieldVectors( inputs.frame.viewProjection );
            if ( detailMarkers )
            {
                PROFILE_GPU_END( "Frame/Render/DebugOverlay/TornadoField" );
            }
        }
    }

    m_host.RenderEditorOverlay( inputs.frame.viewProjection, inputs.frame.eye, inputs.frame.up );

    if ( m_host.m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
        DRAW_CALL_TRACE_SCOPE( "PhysicsDebug" );
        m_host.m_physicsDebugVisualizer.SetFlags( m_host.m_debug.physicsDebugFlags );
        m_host.m_physicsDebugVisualizer.SetPipelineStageCursor( m_host.m_debug.physicsDebugPipelineStageCursor );
        if ( inputs.frame.scene )
        {
            inputs.frame.scene->RenderPhysicsDebug( m_host.m_physicsDebugVisualizer,
                                                    inputs.frame.viewProjection,
                                                    m_host.m_systems.terrain.get() );
        }
        if ( detailMarkers )
        {
            PROFILE_GPU_END( "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
    }
    if ( detailMarkers )
    {
        PROFILE_GPU_END( "Frame/Render/DebugOverlay" );
    }
}


bool DebugOverlayPass::HasOverlayWork( const DebugOverlayPassInputs& inputs ) const
{
    if ( m_host.m_debug.isBroadphaseOverlay )
    {
        return true;
    }
    if ( ( m_host.m_runtimeSettings.tornadoField.visualizeVelocityField ||
           m_host.m_runtimeSettings.tornadoSystem.visualizeVelocityField ) &&
         inputs.frame.scene )
    {
        return true;
    }
    if ( m_host.m_debug.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        return true;
    }

    const float rayLinger = (std::max)( 0.0f, m_host.m_debug.physicsDebugContactLinger );
    if ( m_host.ToolHasLingeredRayCastLine( rayLinger ) )
    {
        return true;
    }

    if ( m_host.ToolHasSelectionOverlayWork() )
    {
        return true;
    }
    if ( m_host.ToolHasMousePickupOverlayWork() )
    {
        return true;
    }

    if ( m_host.ReplayPathVisualizerHasTarget() || m_host.ReplayHasCameraFocus() )
    {
        return true;
    }
    if ( m_host.ReplayVelocityEditActive() && !m_host.m_editor.editorModeEnabled )
    {
        return true;
    }
    return m_host.ToolHasLauncherShots();
}


void DebugOverlayPass::EnsureGpuResources( const RenderFrameContext& /*frame*/ )
{
    // Debug visualizers own their transient geometry; this pass owns late-frame
    // ordering so diagnostics draw over production geometry.
}


void DebugOverlayPass::ReleaseGpuResources()
{
    // Current debug visualizers release with their owning systems.
}


void VolumetricPass::EnsureGpuResources( const RenderFrameContext& frame )
{
    if ( !frame.cinematicEnabled || !IsGfxReady() )
    {
        return;
    }

    const int w = (std::max)( 1, Gfx().GetWidth() );
    const int h = (std::max)( 1, Gfx().GetHeight() );
    const int volW = (std::max)( 1, w / 2 );
    const int volH = (std::max)( 1, h / 2 );
    VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
    const bool needsVolumetricTarget =
        !volumetric.target || volumetric.target->GetWidth() != volW || volumetric.target->GetHeight() != volH ||
        volumetric.target->GetColorFormat() != SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;
    if ( needsVolumetricTarget )
    {
        // Light shafts are soft, so this pass intentionally renders at half
        // resolution before TonemapPass composites the result over the scene.
        if ( volumetric.target )
        {
            volumetric.target->ResetResources();
        }
        volumetric.target.reset();
        volumetric.target =
            Gfx().CreateFramebuffer( volW, volH, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }
    if ( !volumetric.shader )
    {
        // Half-resolution pass: creates warm light shafts that tonemap can add
        // without making every world shader understand volumetric lighting.
        volumetric.shader = m_host.m_systems.assets.CreateShader( "shader.post_volumetric_light" );
    }
}


void VolumetricPass::ReleaseGpuResources()
{
    VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
    if ( volumetric.target )
    {
        volumetric.target->ResetResources();
    }
    volumetric.target.reset();
    volumetric.shader.reset();
}


bool VolumetricPass::CanRender( const RenderFrameContext& frame ) const
{
    const CinematicRenderConfig& cinematic = m_host.ActiveCinematicConfig();
    const CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    const VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
    const FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;
    return frame.cinematicEnabled && cinematic.volumetricLightingEnabled && scene.hdrTarget && volumetric.target &&
           volumetric.shader && fullscreen.quadVB != 0;
}


bool VolumetricPass::Render( const RenderFrameContext& frame )
{
    if ( !CanRender( frame ) )
    {
        return false;
    }

    const CinematicRenderConfig& cinematic = m_host.ActiveCinematicConfig();
    CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
    FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;

    const bool detailMarkers = PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/VolumetricLight" );
    }
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/VolumetricLight" );
    // Invariant: unbind the full-size scene target before sampling it. The
    // volumetric pass reads scene color/depth and writes a separate soft light
    // texture, so read and write targets must be different resources.
    scene.hdrTarget->Unbind();
    volumetric.target->Bind();
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    renderCommands.SetViewport( 0, 0, volumetric.target->GetWidth(), volumetric.target->GetHeight() );

    // This is another screen-space effect, so depth testing and blending are
    // disabled while the full-screen quad is generated.
    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( "Frame/Render/VolumetricLight/Draw" );
        }
        DRAW_CALL_TRACE_SCOPE( "Draw" );
        volumetric.shader->Use();
        BindVolumetricPassParams( *volumetric.shader, frame.eye, frame.viewProjection, cinematic );
        // Pass contract: texture slot 0 is rendered color, slot 1 is rendered
        // depth. The shader uses depth to tell sky pixels from solid geometry so
        // rays pass through sky and fade when they cross hills/balls.
        BindRenderTextureSlots( renderCommands,
                                scene.hdrTarget->GetColorTextureHandle(),
                                scene.hdrTarget->GetDepthTextureHandle(),
                                0,
                                0 );
        DrawFullscreenQuad( renderCommands, fullscreen.quadVB );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( "Frame/Render/VolumetricLight/Draw" );
        }
    }

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    volumetric.target->Unbind();
    renderCommands.SetViewport( 0, 0, m_host.WindowScreenWidth(), m_host.WindowScreenHeight() );
    if ( detailMarkers )
    {
        PROFILE_GPU_END( "Frame/Render/VolumetricLight" );
    }
    return true;
}


void TonemapPass::EnsureGpuResources( const RenderFrameContext& frame )
{
    if ( !frame.cinematicEnabled || !IsGfxReady() )
    {
        return;
    }

    TonemapPassResources& tonemap = m_host.m_systems.renderPasses.tonemap;
    if ( !tonemap.shader )
    {
        // Final full-screen shader: combines HDR scene color, depth fog, bloom,
        // grade, vignette, and optional volumetric light into the backbuffer.
        tonemap.shader = m_host.m_systems.assets.CreateShader( "shader.post_tonemap" );
    }
}


void TonemapPass::ReleaseGpuResources()
{
    m_host.m_systems.renderPasses.tonemap.shader.reset();
}


void TonemapPass::Render( const RenderFrameContext& frame, bool sceneAlreadyUnbound, bool volumetricReady )
{
    CinematicScenePassResources& scene = m_host.m_systems.renderPasses.cinematicScene;
    VolumetricLightPassResources& volumetric = m_host.m_systems.renderPasses.volumetricLight;
    TonemapPassResources& tonemap = m_host.m_systems.renderPasses.tonemap;
    FullscreenPassResources& fullscreen = m_host.m_systems.renderPasses.fullscreen;
    if ( !scene.hdrTarget || !tonemap.shader || fullscreen.quadVB == 0 )
    {
        return;
    }

    const bool detailMarkers = PlatformProfiler::AreDetailedRangesEnabled();
    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( "Frame/Render/Tonemap" );
    }
    DRAW_CALL_TRACE_SCOPE( "Frame/Render/Tonemap" );
    if ( !sceneAlreadyUnbound )
    {
        scene.hdrTarget->Unbind();
    }
    Rendering::IRenderCommandContext& renderCommands = RenderCommands( frame );
    renderCommands.SetViewport( 0, 0, m_host.WindowScreenWidth(), m_host.WindowScreenHeight() );

    const bool depthWasEnabled = renderCommands.IsDepthTestEnabled();
    const bool blendWasEnabled = renderCommands.IsBlendEnabled();
    renderCommands.SetDepthTest( false );
    renderCommands.SetDepthWrite( false );
    renderCommands.SetBlend( false );

    // Concept: "resolve" means "turn our off-screen cinematic render target
    // into the final image on the window." This is where the HDR scene becomes
    // normal display color and where bloom/fog/rays are layered in.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( "Frame/Render/Tonemap/Draw" );
        }
        DRAW_CALL_TRACE_SCOPE( "Draw" );
        tonemap.shader->Use();
        const CinematicRenderConfig& cinematic = m_host.ActiveCinematicConfig();
        BindTonemapPassParams( *tonemap.shader, frame.eye, frame.viewProjection, cinematic, volumetricReady );
        // Pass contract: slot 0 is the bright HDR scene, slot 1 is its depth buffer,
        // and slot 2 is either the volumetric-light texture or a harmless fallback
        // when that pass is disabled.
        BindRenderTextureSlots( renderCommands,
                                scene.hdrTarget->GetColorTextureHandle(),
                                scene.hdrTarget->GetDepthTextureHandle(),
                                volumetricReady && volumetric.target ? volumetric.target->GetColorTextureHandle()
                                                                     : scene.hdrTarget->GetColorTextureHandle(),
                                0 );
        DrawFullscreenQuad( renderCommands, fullscreen.quadVB );
        if ( detailMarkers )
        {
            PROFILE_GPU_END( "Frame/Render/Tonemap/Draw" );
        }
    }

    renderCommands.SetDepthTest( depthWasEnabled );
    renderCommands.SetDepthWrite( depthWasEnabled );
    renderCommands.SetBlend( blendWasEnabled );
    if ( detailMarkers )
    {
        PROFILE_GPU_END( "Frame/Render/Tonemap" );
    }
}
