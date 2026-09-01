/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
Purpose:
  Implements the named world-render pass classes owned by RuntimeRenderer.

Summary:
  RuntimeRenderer.cpp should read like a frame story: sample shared camera
  values, run focused named passes, then hand each output to its downstream
  consumer. This file owns rendering guts and GPU lifetime hooks so pass
  contracts remain visible where the work happens. The debug overlay routes
  detached contact patches through the existing physics glyph visualizer.

Glossary:
  GPU resource: Backend-owned texture, framebuffer, shader, descriptor, or
  dynamic vertex buffer that must be released before backend teardown.

Invariants:
  - EnsureGpuResources may lazily create or resize backend resources, but must
    not change pass order or draw output.
  - ReleaseGpuResources must be safe during backend teardown and must leave
    resource structs in a null/zero state for the next ensure.
  - Render methods consume only the current frame's input structs; they must not
    cache borrowed pointers from those structs.
  - Every PSO-affecting draw selects a complete pass-local raster bucket; no
    pass saves, mutates, or restores another pass's fixed-function state.
  - Generic contact patches render independently of the operator's live physics
    debug flags and never enter the live-contact linger cache.
  - SkyPass verifies its live world-view sky owner before matrix construction,
    texture commands, or draw submission.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h declares pass contracts.
  - SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp owns frame orchestration.
  - Agentic/Reference/engine-glossary.md
*/
#include "RuntimeRenderPasses.h"
#include "RuntimeRenderFrameValues.h"
#include "../../Assets/AssetKeys.h"
#include "RuntimeRenderResources.h"
#include "CollisionVisualizer.h"
#include "PhysicsDebugVisualizer.h"
#include "BroadphaseVisualizer.h"
#include "../Replay/ReplayVisualPacket.h"
#include "../../Assets/TextureCollection.h"
#include "../../Core/PlatformProfiler.h"
#include "../../Core/Profiler.h"
#include "../../Core/Log.h"
#include "../../Rendering/DX12/Dx12Diagnostics.h"
#include "../../Rendering/DX12/RenderDeviceDX12.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include "../../Rendering/DX12/Dx12ResourceBuilder.h"
#include "../../Rendering/RenderInstanceRenderer.h"
#include "../../Rendering/PrimitiveBatchRenderer.h"
#include "../../Rendering/RenderGpuTimingOwner.h"
#include "../../Rendering/RenderGraph.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Rendering/RenderRasterBindingContract.h"
#include "../../World/Terrain.h"
#include "../../World/SkyBox.h"
#include "../../World/WorldEnvironment.h"

#include <cstdio>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using SkullbonezCore::Rendering::PrimitiveBatchRenderer;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
namespace Textures = SkullbonezCore::Textures;
using SkullbonezCore::Geometry::XZBounds;
using SkullbonezCore::Math::Vector::Vector3;

namespace
{
namespace Rendering = SkullbonezCore::Rendering;
constexpr int RENDER_TEXTURE_SLOT_COUNT = SkullbonezCore::Rendering::TEXTURE_SLOT_COUNT;
constexpr unsigned int RENDER_TEXTURE_SLOT_0 = 1u << 0;
constexpr unsigned int RENDER_TEXTURE_SLOT_1 = 1u << 1;
constexpr unsigned int RENDER_TEXTURE_SLOT_2 = 1u << 2;
constexpr unsigned int RENDER_TEXTURE_SLOT_3 = 1u << 3;
constexpr unsigned int RENDER_TEXTURE_SLOT_5 = 1u << 5;
constexpr SkullbonezCore::Rendering::PassRasterStateBucket
    FULLSCREEN_OPAQUE_RASTER = SkullbonezCore::Rendering::MakePassRasterStateBucket( 0, { false, false, false } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket SHADOW_DEPTH_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0, { true,
                                    true,
                                    false,
                                    SkullbonezCore::Rendering::BlendFactor::One,
                                    SkullbonezCore::Rendering::BlendFactor::Zero,
                                    SkullbonezCore::Rendering::CullMode::Back,
                                    { true, 4.0f, 2.0f } } );

constexpr SkullbonezCore::Rendering::PassRasterStateBucket WATER_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0, { true, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                    SkullbonezCore::Rendering::BlendFactor::OneMinusSrcAlpha } );

constexpr SkullbonezCore::Rendering::PassRasterStateBucket
    TERRAIN_RASTER = SkullbonezCore::Rendering::MakePassRasterStateBucket( 0, { true, true, false } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket REPLAY_RIBBON_DEPTH_HINT_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0,
                               { false, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket REPLAY_RIBBON_VISIBLE_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 1,
                               { true, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket
    REPLAY_LINE_RASTER = SkullbonezCore::Rendering::MakePassRasterStateBucket( 2, { false, false, false } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket LAUNCHER_RASTER_BUCKET = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0,
                               { false, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );
constexpr int LAUNCHER_MAX_VERTICES = static_cast<int>( RenderToolOverlayView::LAUNCHER_SHOT_CAPACITY * 96 );
constexpr float LAUNCHER_AFTERIMAGE_HALF_WIDTH = 0.62f;
constexpr float LAUNCHER_OUTER_HALF_WIDTH = 0.40f;
constexpr float LAUNCHER_CORE_HALF_WIDTH = 0.12f;
constexpr float LAUNCHER_IMPACT_HALF_SIZE = 1.45f;
constexpr float LAUNCHER_IMPACT_DISC_HALF_SIZE = 0.68f;
constexpr float LAUNCHER_MIN_SEGMENT_LENGTH = 0.25f;

Vector3 NormalizeOr( const Vector3& value, const Vector3& fallback )
{
    const float lenSq = VectorMagSquared( value );
    return lenSq <= TOLERANCE * TOLERANCE ? fallback : value * ( 1.0f / sqrtf( lenSq ) );
}

void RenderReplayVisualPacket( const ReplayVisualPacket& packet, const Matrix4& viewProjection,
                               Rendering::Dx12GeometryOwner& renderCommands )
{
    if ( !packet.HasGeometry() )
    {
        return;
    }

    const Rendering::RetainedGeometryStreamToken retainedStream = { packet.retainedPredictionStreamId,
                                                                    packet.retainedPredictionRevision };

    if ( !packet.retainedPredictionOrdinaryLines.empty() )
    {
        renderCommands.DrawRetainedLinesColored( packet.retainedPredictionOrdinaryLines, retainedStream, false,
                                                 viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.retainedPredictionPriorityLines.empty() )
    {
        renderCommands.DrawRetainedLinesColored( packet.retainedPredictionPriorityLines, retainedStream, true,
                                                 viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.combinedLines.empty() )
    {
        renderCommands.DrawLinesColored( packet.combinedLines, viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.retainedPredictionRibbonVertices.empty() )
    {
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionRibbonVertices, retainedStream, false,
                                                   viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionRibbonVertices, retainedStream, false,
                                                   viewProjection, Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.retainedPredictionPriorityRibbonVertices.empty() )
    {
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionPriorityRibbonVertices, retainedStream, true,
                                                   viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionPriorityRibbonVertices, retainedStream, true,
                                                   viewProjection, Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.retainedPredictionRibbonRanges.empty() )
    {
        renderCommands.DrawRetainedGeometryRanges( packet.retainedPredictionCompactRibbonRecords,
                                                   packet.retainedPredictionRibbonRanges, retainedStream, viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
        renderCommands.DrawRetainedGeometryRanges( packet.retainedPredictionCompactRibbonRecords,
                                                   packet.retainedPredictionRibbonRanges, retainedStream, viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.expandedRibbonVertices.empty() )
    {
        renderCommands.DrawTransientColoredTriangles( packet.expandedRibbonVertices, viewProjection,
                                                      Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                      REPLAY_RIBBON_DEPTH_HINT_RASTER );
        renderCommands.DrawTransientColoredTriangles( packet.expandedRibbonVertices, viewProjection,
                                                      Rendering::TransientTriangleStyle::InstancedRibbon,
                                                      REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.priorityExpandedRibbonVertices.empty() )
    {
        renderCommands.DrawTransientColoredTriangles( packet.priorityExpandedRibbonVertices, viewProjection,
                                                      Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                      REPLAY_RIBBON_DEPTH_HINT_RASTER );
        renderCommands.DrawTransientColoredTriangles( packet.priorityExpandedRibbonVertices, viewProjection,
                                                      Rendering::TransientTriangleStyle::InstancedRibbon,
                                                      REPLAY_RIBBON_VISIBLE_RASTER );
    }
}
constexpr SkullbonezCore::Rendering::PassRasterStateBucket DEBUG_LINE_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0,
                               { false, false, false, SkullbonezCore::Rendering::BlendFactor::One,
                                 SkullbonezCore::Rendering::BlendFactor::Zero, SkullbonezCore::Rendering::CullMode::None } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket RETAINED_OVERLAY_DEPTH_HINT_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 1,
                               { false, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );
constexpr SkullbonezCore::Rendering::PassRasterStateBucket RETAINED_OVERLAY_VISIBLE_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 2,
                               { true, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );

void ClearRenderTextureSlotsExcept( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures, unsigned int keptSlots )
{
    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        if ( ( keptSlots & ( 1u << slot ) ) == 0u )
        {
            renderTextures.BindTexture( 0, slot );
        }
    }
}

void ClearAllRenderTextureSlots( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures )
{
    ClearRenderTextureSlotsExcept( renderTextures, 0u );
}

int CopyDxrRenderInstanceMatrices( const SkullbonezCore::Rendering::RenderInstanceStore& renderStore, Matrix4* outMatrices,
                                   int maxModelCount )
{
    if ( !outMatrices || maxModelCount <= 0 )
    {
        return 0;
    }

    const auto instances = renderStore.Records();
    const int modelCount = (std::min)( static_cast<int>( instances.size() ), maxModelCount );

    for ( int i = 0; i < modelCount; ++i )
    {
        outMatrices[i] = instances[static_cast<std::size_t>( i )].modelMatrix;
    }

    return modelCount;
}

bool HasCollisionVisualizerFrameView( const RuntimeRenderCollisionDebugView& collisionDebug )
{
    return collisionDebug.modelCount > 0;
}

CollisionVisualizerFrameView BuildCollisionVisualizerFrameView( const RuntimeRenderCollisionDebugView& collisionDebug )
{
    return CollisionVisualizerFrameView { collisionDebug.colliders,
                                          collisionDebug.renderInstances,
                                          collisionDebug.collisionVisualContacts,
                                          collisionDebug.sleepStates,
                                          collisionDebug.sleepIslandVisualIds,
                                          collisionDebug.modelCount };
}

bool HasPhysicsDebugFrameView( const RuntimeRenderPhysicsDebugView& physicsDebug )
{
    return physicsDebug.modelCount > 0;
}

PhysicsDebugFrameView BuildPhysicsDebugFrameView( const RuntimeRenderPhysicsDebugView& physicsDebug )
{
    const PhysicsDebugBodyView bodies { physicsDebug.bodyStore, physicsDebug.colliders, physicsDebug.modelCount };
    return PhysicsDebugFrameView {
        bodies,
        PhysicsDebugContactView { physicsDebug.bodyStore, physicsDebug.physicsDebugContacts },
        PhysicsDebugSleepView { bodies, physicsDebug.sleepStates, physicsDebug.sleepSupportedStates,
                                physicsDebug.sleepInhibitedStates },
        PhysicsDebugPipelineView { physicsDebug.bodyStore, physicsDebug.physicsPipelineTrace },
    };
}

void BindRenderTextureSlots( SkullbonezCore::Rendering::Dx12TextureOwner& renderTextures, uint32_t slot0, uint32_t slot1,
                             uint32_t slot2, uint32_t slot3, uint32_t slot4 = 0, uint32_t slot5 = 0 )
{
    // Invariant: ordinary raster shaders expose t0..t5. Slot t4 is reserved for
    // the object material table, but pass hygiene still clears it to the typed
    // null SRV; object batches bind the material table again immediately before
    // drawing. Terrain alone uses t5 for the tight object-shadow map.
    const uint32_t handles[RENDER_TEXTURE_SLOT_COUNT] = { slot0, slot1, slot2, slot3, slot4, slot5 };

    for ( int slot = 0; slot < RENDER_TEXTURE_SLOT_COUNT; ++slot )
    {
        renderTextures.BindTexture( handles[slot], slot );
    }
}

bool ReportRenderTextureResult( const char* passName, const SkullbonezCore::Core::SbResult& result )
{
    if ( result.Ok() )
    {
        return true;
    }

    // Why: render passes are void frame steps, so recoverable texture failures
    // surface at the pass boundary and the affected draw is skipped.
    std::fprintf( stderr, "%s texture failure [%s]: %s\n", passName ? passName : "Frame/Render", result.ErrorOwner(),
                  result.ErrorMessage() );

    return false;
}

bool SelectRenderTexture( Textures::TextureCollection& textures, uint32_t hash, const char* passName )
{
    return ReportRenderTextureResult( passName, textures.SelectTexture( hash ) );
}

bool ResolveRenderTextureHandle( Textures::TextureCollection& textures, uint32_t hash, const char* passName,
                                 uint32_t& outHandle )
{
    const Textures::TextureCollection::TextureHandleResult result = textures.GetTextureHandle( hash );

    if ( !ReportRenderTextureResult( passName, result.result ) )
    {
        return false;
    }

    outHandle = result.handle;
    return true;
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


ScreenSunPosition ProjectCinematicSunToScreen( const Vector3& eye, const Matrix4& viewProjection,
                                               const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    const Vector3 sunPoint = eye + ResolveCinematicSunDirection( cinematic ) * 1000.0f;
    const float clipX = viewProjection.m[0] * sunPoint.x + viewProjection.m[4] * sunPoint.y +
                        viewProjection.m[8] * sunPoint.z + viewProjection.m[12];

    const float clipY = viewProjection.m[1] * sunPoint.x + viewProjection.m[5] * sunPoint.y +
                        viewProjection.m[9] * sunPoint.z + viewProjection.m[13];

    const float clipW = viewProjection.m[3] * sunPoint.x + viewProjection.m[7] * sunPoint.y +
                        viewProjection.m[11] * sunPoint.z + viewProjection.m[15];

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

void DrawFullscreenQuad( SkullbonezCore::Rendering::Dx12GeometryOwner& renderCommands, uint32_t quadVB,
                         const SkullbonezCore::Rendering::PassRasterStateBucket& rasterState )
{
    // Shared post vertex contract: clip-space xy followed by UV. Keeping one
    // copy prevents sky, volumetric, and tonemap from quietly drifting apart.
    renderCommands.UploadAndDrawDynamicVB( quadVB, FULLSCREEN_QUAD_VERTS, rasterState );
}

void BindSkyPassParams( SkullbonezCore::Rendering::ShaderDX12& shader, const Matrix4& view, const Matrix4& projection,
                        const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    shader.SetVec4( "uSunParams", cinematic.sunAzimuth, cinematic.sunElevation, cinematic.sunIntensity,
                    cinematic.skyGlowStrength );

    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec3( "uHorizonColor", cinematic.skyHorizonR, cinematic.skyHorizonG, cinematic.skyHorizonB );
    shader.SetVec3( "uZenithColor", cinematic.skyZenithR, cinematic.skyZenithG, cinematic.skyZenithB );
    shader.SetMat4( "uInvView", view.Inverse() );
    shader.SetMat4( "uInvProjection", projection.Inverse() );
    shader.SetInt( "uSkyMode", cinematic.skyMode );
    shader.SetVec4( "uCloudParams", cinematic.cloudCoverage, cinematic.cloudSoftness, cinematic.cloudScale,
                    cinematic.cloudsEnabled ? cinematic.cloudIntensity : 0.0f );
}

void BindVolumetricPassParams( SkullbonezCore::Rendering::ShaderDX12& shader, const Vector3& eye,
                               const Matrix4& viewProjection, const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                               float frustumNear, float frustumFar )
{
    const ScreenSunPosition sunScreen = ProjectCinematicSunToScreen( eye, viewProjection, cinematic );
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetVec4( "uDepthParams", frustumNear, frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uSunShaftParams", sunScreen.x, sunScreen.y,
                    cinematic.godRaysEnabled ? cinematic.sunShaftStrength : 0.0f, cinematic.sunShaftFalloff );

    shader.SetVec3( "uSunColor", cinematic.sunColorR, cinematic.sunColorG, cinematic.sunColorB );
    shader.SetVec4( "uVolumetricParams", cinematic.volumetricStrength, cinematic.volumetricDensity,
                    cinematic.volumetricDecay, cinematic.fogDensity );
}

void BindTonemapPassParams( SkullbonezCore::Rendering::ShaderDX12& shader,
                            const SkullbonezCore::Core::CinematicRenderConfig& cinematic, float frustumNear,
                            float frustumFar, int sceneWidth, int sceneHeight, bool volumetricReady )
{
    shader.SetInt( "uSceneTex", 0 );
    shader.SetInt( "uDepthTex", 1 );
    shader.SetInt( "uVolumetricTex", 2 );
    shader.SetFloat( "uExposure", cinematic.exposure );
    shader.SetFloat( "uGamma", cinematic.gamma );
    shader.SetVec4( "uDepthParams", frustumNear, frustumFar, 0.0f, 0.0f );
    shader.SetVec4( "uFogParams", cinematic.fogStart, cinematic.fogEnd, cinematic.fogEnabled ? cinematic.fogDensity : 0.0f,
                    cinematic.fogEnabled ? cinematic.fogMaxOpacity : 0.0f );

    shader.SetVec3( "uFogColor", cinematic.fogColorR, cinematic.fogColorG, cinematic.fogColorB );

    // Invariant: these are the actual HDR scene-target dimensions, not assumed
    // window dimensions. Supplying their inverse here keeps the per-pixel shader
    // free of GetDimensions queries while resize updates the bloom footprint.
    const float inverseSceneWidth = 1.0f / static_cast<float>( sceneWidth > 0 ? sceneWidth : 1 );
    const float inverseSceneHeight = 1.0f / static_cast<float>( sceneHeight > 0 ? sceneHeight : 1 );
    shader.SetVec4( "uBloomTexelSize", inverseSceneWidth, inverseSceneHeight, 0.0f, 0.0f );
    shader.SetVec4( "uBloomParams", cinematic.bloomThreshold, cinematic.bloomKnee,
                    cinematic.bloomEnabled ? cinematic.bloomStrength : 0.0f, cinematic.bloomRadius );

    shader.SetVec4( "uStyleGrade", cinematic.styleSaturation, cinematic.styleContrast, cinematic.styleVignette,
                    static_cast<float>( cinematic.skyMode ) );

    shader.SetFloat( "uVolumetricCompositeStrength", volumetricReady && cinematic.volumetricLightingEnabled ? 1.0f : 0.0f );
}

} // namespace

void RenderResourceLifecycleLog::Write( const char* phase, const char* step ) const
{
    const bool backendReady = m_renderDevice != nullptr && m_renderDevice->IsReady();
    const int backendWidth = m_renderDevice ? m_renderDevice->Width() : 0;
    const int backendHeight = m_renderDevice ? m_renderDevice->Height() : 0;
    SkullbonezCore::Core::Log()
        .WriteEventf( "render_resource_lifecycle phase=%s step=%s gfx_ready=%d backend_width=%d backend_height=%d "
                      "scene_index=%d load=%d",
                      phase ? phase : "unknown", step ? step : "unknown", backendReady ? 1 : 0, backendWidth, backendHeight,
                      m_sceneIndex, m_sceneLoadCount );
}


void FullscreenQuadPass::EnsureGpuResources( bool cinematicEnabled, Rendering::Dx12GeometryOwner& renderGeometry )
{
    if ( !cinematicEnabled )
    {
        return;
    }

    if ( m_resources.quadVB == 0 )
    {
        // Full-screen shaders draw one rectangle; each vertex stores screen xy
        // plus uv, and every pass gives that same geometry its own shader meaning.
        const int attribs[] = { 2, 2 };
        m_resources.quadVB = renderGeometry.CreateDynamicVB( attribs, 2, 6 );
    }
}


void FullscreenQuadPass::ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry )
{
    if ( renderGeometry && m_resources.quadVB != 0 )
    {
        renderGeometry->DestroyDynamicVB( m_resources.quadVB );
    }

    m_resources.quadVB = 0;
}


void SkyPass::EnsureGpuResources( bool cinematicEnabled, Assets::AssetSystem& assets,
                                  Rendering::Dx12ResourceBuilder& renderResources )
{
    // Lifetime: frame-resource publication is also the world-view publication
    // boundary. ReleaseGpuResources closes this borrow before any backend-owned
    // state is torn down; a later rebuild observes the current scene owner.
    m_worldViewLease.Open( m_skyBox.get() );

    if ( !cinematicEnabled )
    {
        return;
    }

    if ( !m_skyResources.atmosphereShader )
    {
        // Procedural sky shader: draws generated sunset/cloud color when the
        // cinematic config opts out of the authored cube-map skybox.
        m_skyResources.atmosphereShader = assets.CreateShader( renderResources, "shader.sky_atmosphere" );
    }
}


void SkyPass::ReleaseGpuResources()
{
    m_worldViewLease.Close();
    m_skyResources.atmosphereShader.reset();
}


SkullbonezCore::Geometry::SkyBox& SkyPass::RequireWorldView( const char* operation )
{
    return *m_worldViewLease.Require( operation );
}


void SceneTargetPass::EnsureGpuResources( bool cinematicEnabled, Rendering::Dx12ResourceBuilder& renderResources,
                                          int windowWidth, int windowHeight )
{
    if ( !cinematicEnabled )
    {
        return;
    }

    const int w = windowWidth;
    const int h = windowHeight;
    const bool needsSceneTarget = !m_resources.hdrTarget || m_resources.hdrTarget->GetWidth() != w ||
                                  m_resources.hdrTarget->GetHeight() != h ||
                                  m_resources.hdrTarget->GetColorFormat() !=
                                      SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F;

    if ( needsSceneTarget )
    {
        // RGBA16F preserves bright sky/fog values until TonemapPass compresses
        // them back to display color on the window backbuffer.
        if ( m_resources.hdrTarget )
        {
            m_resources.hdrTarget->ResetResources();
        }

        m_resources.hdrTarget.reset();
        m_resources.hdrTarget = renderResources
                                    .CreateFramebuffer( w, h, SkullbonezCore::Rendering::FramebufferColorFormat::RGBA16F );
    }
}


void SceneTargetPass::ReleaseGpuResources()
{
    if ( m_resources.hdrTarget )
    {
        m_resources.hdrTarget->ResetResources();
    }

    m_resources.hdrTarget.reset();
}


bool SceneTargetPass::IsReady() const
{
    return m_resources.hdrTarget != nullptr;
}


void ReflectionPass::EnsureGpuResources( Rendering::Dx12ResourceBuilder& renderResources, int windowWidth, int windowHeight )
{
    // Why: water distortion already filters this image, while a 2x target made
    // the procedural reflection sky shade four times as many pixels as the main
    // view every frame. Native resolution preserves the authored reflection and
    // post style without spending supersampling on a warped secondary image.
    const int fboW = windowWidth;
    const int fboH = windowHeight;
    const bool needsReflectionTarget = !m_resources.target || m_resources.target->GetWidth() != fboW ||
                                       m_resources.target->GetHeight() != fboH ||
                                       m_resources.target->GetColorFormat() !=
                                           SkullbonezCore::Rendering::FramebufferColorFormat::RGBA8;

    if ( needsReflectionTarget )
    {
        m_lifecycleLog.Write( "window_resize", "reflection_target_recreate_if_needed" );

        if ( m_resources.target )
        {
            m_resources.target->ResetResources();
        }

        m_resources.target.reset();
        m_resources.target = renderResources.CreateFramebuffer( fboW, fboH );
    }
}


void ReflectionPass::ReleaseGpuResources()
{
    m_lifecycleLog.Write( "reflection_reset", "reflection_target" );

    // Lifetime: ResetResources gives the backend a chance to release device
    // objects before the unique_ptr destructor drops the concrete target owner.
    if ( m_resources.target )
    {
        m_resources.target->ResetResources();
    }

    m_resources.target.reset();
}


void ShadowPass::EnsureGpuResources( Rendering::Dx12ResourceBuilder& renderResources,
                                     const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    if ( !cinematic.shadow.enabled )
    {
        return;
    }

    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/EnsureResources" );

    // Concept: the shadow map is a concrete DX12 depth framebuffer. It is
    // intentionally owned outside the cinematic HDR target because the same
    // light-space depth texture is useful in normal backbuffer rendering,
    // cinematic rendering, and screenshot/perf scenes. The cinematic config
    // still supplies map size and bias/softness values, but the feature itself
    // is no longer gated by the cinematic post-processing path.
    const int mapSize = std::clamp( cinematic.shadow.mapSize, 256, 8192 );
    auto ensureTarget = [&]( std::unique_ptr<Rendering::FramebufferDX12>& target )
    {
        const bool needsTarget = !target || target->GetWidth() != mapSize || target->GetHeight() != mapSize;

        if ( needsTarget )
        {
            target.reset();
            target = renderResources.CreateFramebuffer( mapSize, mapSize );
        }
    };

    ensureTarget( m_resources.terrainTarget );
    ensureTarget( m_resources.objectTarget );
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

    for ( const ShadowResetPhase& phase : resetSteps )
    {
        m_lifecycleLog.Write( "shadow_reset", phase.name );

        switch ( phase.step )
        {
        case ShadowResetStep::TerrainShadowFBO:

            if ( m_resources.terrainTarget )
            {
                m_resources.terrainTarget->ResetResources();
            }

            m_resources.terrainTarget.reset();
            break;
        case ShadowResetStep::ObjectShadowFBO:

            if ( m_resources.objectTarget )
            {
                m_resources.objectTarget->ResetResources();
            }

            m_resources.objectTarget.reset();
            break;
        case ShadowResetStep::FramePayloads:
            m_resources.terrainFrame = Rendering::ShadowFrameData();
            m_resources.objectFrame = Rendering::ShadowFrameData();
            m_resources.objectCasterBatches.Clear();
            break;
        }
    }
}


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildTerrainFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                   const Math::Vector::Vector3& lightDirectionWorld, Geometry::Terrain* terrain ) const
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildTerrainFrame" );

    Rendering::ShadowFrameData shadowFrame;

    if ( !terrain || !m_resources.terrainTarget )
    {
        return shadowFrame;
    }

    // Shadow maps need a stable light-space camera. `lightDirectionWorld` is
    // treated as the vector from the scene toward the light source. The visible
    // ordinary and cinematic shaders use the same directional-sun contract, so
    // shadow visibility blocks the direct light the BRDF is actually shading.
    Vector3 lightDir = NormalizeShadowLightDirection( lightDirectionWorld );

    const XZBounds terrainBounds = terrain->GetXZBounds();
    const float extentX = (std::max)( terrainBounds.m_xMax - terrainBounds.m_xMin, 1.0f );
    const float extentZ = (std::max)( terrainBounds.m_zMax - terrainBounds.m_zMin, 1.0f );
    const float terrainHeightRange = (std::max)( terrain->GetMaxHeight() - terrain->GetMinHeight(), 64.0f );

    const float terrainRadius = (std::max)( extentX, extentZ ) * 0.5f;
    const float shadowRadius = std::clamp( terrainRadius + 180.0f, 128.0f,
                                           (std::max)( cinematic.shadow.maxDistance, 128.0f ) );

    // Center the orthographic projection over the whole terrain instead of the
    // camera. This is a simple single-map v1: it avoids camera-dependent popping
    // and makes screenshots deterministic, at the cost of spreading resolution
    // across the authored terrain bounds instead of using cascades.
    const Vector3 focus( ( terrainBounds.m_xMin + terrainBounds.m_xMax ) * 0.5f,
                         ( terrain->GetMinHeight() + terrain->GetMaxHeight() ) * 0.5f,
                         ( terrainBounds.m_zMin + terrainBounds.m_zMax ) * 0.5f );
    const float lightBackDistance = shadowRadius + terrainHeightRange + 650.0f;
    const Vector3 lightEye = focus + lightDir * lightBackDistance;
    const Vector3 lightUp = fabsf( lightDir.y ) > 0.92f ? Vector3( 0.0f, 0.0f, 1.0f ) : Vector3( 0.0f, 1.0f, 0.0f );
    const float nearPlane = 1.0f;
    const float farPlane = lightBackDistance * 2.0f + terrainHeightRange + shadowRadius;

    shadowFrame.lightView = Matrix4::LookAt( lightEye, focus, lightUp );
    shadowFrame.lightProjection = Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius,
                                                           nearPlane, farPlane );

    shadowFrame.mapSize = m_resources.terrainTarget->GetWidth();
    Rendering::SnapShadowProjectionToTexelGrid( shadowFrame.lightProjection, shadowFrame.lightView, shadowFrame.mapSize );

    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = m_resources.terrainTarget->GetDepthTextureHandle();

    // Everything below is copied into shader uniforms by ApplyShadowReceiverUniforms.
    // Keeping the values in one payload makes balls, boxes, terrain, and any
    // future backend consume the same shadow decision for the frame.
    shadowFrame.pcfRadius = std::clamp( cinematic.shadow.pcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadow.strength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadow.depthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadow.slopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadow.softness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = cinematic.shadow.terrainReceives;
    shadowFrame.objectsReceive = cinematic.shadow.objectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


SkullbonezCore::Rendering::ShadowFrameData
ShadowPass::BuildObjectFrameData( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  const Math::Vector::Vector3& lightDirectionWorld, const Math::Vector::Vector3& focusHint,
                                  Rendering::RenderInstanceRenderer& instanceRenderer )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/BuildObjectFrame" );

    Rendering::ShadowFrameData shadowFrame;

    if ( !m_resources.objectTarget || !cinematic.shadow.objectsCast || !cinematic.shadow.objectsReceive )
    {
        return shadowFrame;
    }

    Vector3 focus;
    float shadowRadius = 0.0f;
    float heightRange = 0.0f;
    const float objectSearchDistance = std::clamp( cinematic.shadow.maxDistance * 0.15f, 180.0f, 320.0f );

    if ( !instanceRenderer.GetObjectShadowBounds( m_profiler, focusHint, objectSearchDistance, focus, shadowRadius,
                                                  heightRange ) )
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
    shadowFrame.lightProjection = Matrix4::OrthoZeroToOne( -shadowRadius, shadowRadius, -shadowRadius, shadowRadius,
                                                           nearPlane, farPlane );

    shadowFrame.mapSize = m_resources.objectTarget->GetWidth();
    Rendering::SnapShadowProjectionToTexelGrid( shadowFrame.lightProjection, shadowFrame.lightView, shadowFrame.mapSize );

    shadowFrame.lightViewProjection = shadowFrame.lightProjection * shadowFrame.lightView;
    shadowFrame.lightDirectionWorld = lightDir;
    shadowFrame.depthTextureHandle = m_resources.objectTarget->GetDepthTextureHandle();
    shadowFrame.pcfRadius = std::clamp( cinematic.shadow.pcfRadius, 0, 3 );
    shadowFrame.strength = std::clamp( cinematic.shadow.strength, 0.0f, 1.0f );
    shadowFrame.depthBias = (std::max)( cinematic.shadow.depthBias, 0.0f );
    shadowFrame.slopeBias = (std::max)( cinematic.shadow.slopeBias, 0.0f );
    shadowFrame.texelSize = shadowFrame.mapSize > 0 ? 1.0f / static_cast<float>( shadowFrame.mapSize ) : 0.0f;
    shadowFrame.softness = (std::max)( cinematic.shadow.softness, 0.25f );
    shadowFrame.zeroToOneDepth = true;
    shadowFrame.terrainReceives = false;
    shadowFrame.objectsReceive = cinematic.shadow.objectsReceive;
    shadowFrame.valid = shadowFrame.depthTextureHandle != 0 && shadowFrame.mapSize > 0;
    return shadowFrame;
}


void ShadowPass::RenderShadowMap( Rendering::FramebufferDX12& target, Rendering::RenderInstanceRenderer& instanceRenderer,
                                  Rendering::Dx12Diagnostics& renderDiagnostics, const char* shadowShaderBaseName,
                                  const Rendering::ShadowFrameData& shadowFrame,
                                  const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12TextureOwner& renderTextures,
                                  bool renderTerrain, const Rendering::ShadowCasterBatches& objectCasters,
                                  Geometry::Terrain* terrain )
{
    PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap" );
    DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap" );

    if ( !shadowFrame.valid )
    {
        return;
    }

    if ( ( !renderTerrain || !cinematic.shadow.terrainCasts ) && !cinematic.shadow.objectsCast )
    {
        return;
    }

    // Render from the light's point of view into the depth attachment. The pass
    // does not need color output; the framebuffer abstraction still gives us a
    // color target on some backends, but receivers sample only the depth texture
    // handle stored in ShadowFrameData.
    target.Bind();
    renderFrame.SetViewport( 0, 0, target.GetWidth(), target.GetHeight() );
    renderFrame.Clear( {} );

    // The bucket applies opaque depth writes, back-face culling, and the
    // rasterizer bias to each caster PSO without mutating later passes.
    // Pass contract: shadow depth shaders write depth only and sample no
    // textures. Clear inherited slots so descriptor state from the visible
    // scene cannot leak into this off-screen pass.
    ClearAllRenderTextureSlots( renderTextures );

    if ( renderTerrain && cinematic.shadow.terrainCasts && !m_activeTerrainHidden && terrain )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap/TerrainCasters" );

        // Terrain must cast with the same optional render-only relief that the
        // visible terrain uses. Otherwise cinematic basin relief would receive
        // shadows from the flat CPU height map and the contact would visibly
        // detach. With normal rendering the relief amount is zero by default.
        terrain->RenderShadowDepth( m_profiler, shadowFrame.lightView, shadowFrame.lightProjection, SHADOW_DEPTH_RASTER,
                                    &cinematic );
    }

    if ( cinematic.shadow.objectsCast && !m_activeCollisionVisualizerVisible )
    {
        PROFILE_SCOPED( "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Shadows/ShadowMap/RenderMap/ObjectCasters" );

        // Balls, boxes, and pine-style box visuals all write depth here. The
        // prepared render store keeps separate instanced batches so each caster
        // shape uses the same mesh silhouette as the visible forward pass.
        // Why: both passes submit the same prepared caster shape, so the map
        // selection at this orchestration boundary preserves per-view evidence.
        const Rendering::RenderVisibilityView visibilityView = renderTerrain ? Rendering::RenderVisibilityView::TerrainShadow
                                                                             : Rendering::RenderVisibilityView::ObjectShadow;

        // Invariant: shadow collection always targets the frame-owned batches
        // reserved during RuntimeRenderResources construction. A stack fallback
        // would begin with zero-capacity vectors inside the render phase.
        instanceRenderer.SubmitShadowCasterBatches( m_profiler, shadowShaderBaseName, objectCasters, shadowFrame.lightView,
                                                    shadowFrame.lightProjection, &cinematic, visibilityView );
    }

    target.Unbind();
    renderFrame.SetViewport( 0, 0, m_activeWindowWidth, m_activeWindowHeight );
}


ShadowPassOutput ShadowPass::ResetFrameOutputs()
{
    // Invariant: always clear the receiver payloads at the start of the pass.
    // If shadows are disabled, downstream terrain/object passes must see null
    // outputs instead of last frame's depth texture handles.
    m_resources.terrainFrame = Rendering::ShadowFrameData();
    m_resources.objectFrame = Rendering::ShadowFrameData();
    m_resources.objectCasterBatches.Clear();
    return ShadowPassOutput();
}


ShadowPassOutput ShadowPass::Render( const ShadowPassInputs& inputs )
{
    (void)ResetFrameOutputs();

    if ( inputs.cinematic )
    {
        // Build shadow maps before any receiver pass. Terrain receives the broad
        // map, while objects receive a second tight map centered on nearby bodies
        // so ball-on-ball shadows have enough texel density.
        PROFILE_SCOPED( "Frame/Shadows" );
        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Shadows" );
        PROFILE_GPU_BEGIN( inputs.gpuTiming, "Frame/Shadows/ShadowMap" );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Shadows/ShadowMap" );
            Vector3 lightDirection( inputs.camera.lightPosition[0], inputs.camera.lightPosition[1],
                                    inputs.camera.lightPosition[2] );
            Rendering::ShadowCasterBatches& objectCasters = m_resources.objectCasterBatches;
            const bool shouldBuildObjectCasters = inputs.cinematic->shadow.objectsCast && !inputs.collisionVisualizerVisible;

            if ( shouldBuildObjectCasters )
            {
                inputs.instanceRenderer.BuildShadowCasterBatches( m_profiler, objectCasters );
            }

            m_activeTerrainHidden = inputs.terrainHidden;
            m_activeCollisionVisualizerVisible = inputs.collisionVisualizerVisible;
            m_activeWindowWidth = inputs.windowWidth;
            m_activeWindowHeight = inputs.windowHeight;
            m_resources.terrainFrame = BuildTerrainFrameData( *inputs.cinematic, lightDirection, inputs.terrain );

            if ( m_resources.terrainTarget )
            {
                RenderShadowMap( *m_resources.terrainTarget, inputs.instanceRenderer, inputs.renderDiagnostics,
                                 inputs.shadowShaderBaseName, m_resources.terrainFrame, *inputs.cinematic,
                                 inputs.renderFrame, inputs.renderTextures, true, objectCasters, inputs.terrain );
            }

            // Anchor the tight object-shadow map to the render look target, not
            // the eye. Locked/inspect zoom moves the eye around a stable target;
            // using the eye makes nearby-object bounds pop as the user zooms.
            m_resources.objectFrame = BuildObjectFrameData( *inputs.cinematic, lightDirection, inputs.camera.viewCenter,
                                                            inputs.instanceRenderer );

            if ( m_resources.objectTarget )
            {
                RenderShadowMap( *m_resources.objectTarget, inputs.instanceRenderer, inputs.renderDiagnostics,
                                 inputs.shadowShaderBaseName, m_resources.objectFrame, *inputs.cinematic, inputs.renderFrame,
                                 inputs.renderTextures, false, objectCasters, inputs.terrain );
            }
        }
        PROFILE_GPU_END( inputs.gpuTiming, "Frame/Shadows/ShadowMap" );
    }

    ShadowPassOutput output;
    output.terrainShadow = m_resources.terrainFrame.valid ? &m_resources.terrainFrame : nullptr;
    output.objectShadow = m_resources.objectFrame.valid ? &m_resources.objectFrame : output.terrainShadow;
    return output;
}


void SkyPass::RenderCinematicSky( const RenderCameraLighting& camera, const Math::Transformation::Matrix4& view,
                                  const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                                  Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures )
{
    // Invariant: the active cinematic choice is a frame snapshot, while the
    // generated-sky shader and fullscreen vertex buffer are pass resources.
    // This path should not reach back through Run state for either.
    if ( !cinematic.skyAtmosphereEnabled || !m_skyResources.atmosphereShader || m_fullscreenResources.quadVB == 0 )
    {
        return;
    }

    // The sky is painted as a full-screen background. It should not test against
    // terrain depth and it should not blend with whatever was previously there.
    // Pass contract: this generated sky samples no textures. Clear inherited
    // SRV slots before the fullscreen draw so stale pass inputs cannot be
    // recopied by the backend while the sky shader is active.
    ClearAllRenderTextureSlots( renderTextures );
    m_skyResources.atmosphereShader->Use();
    BindSkyPassParams( *m_skyResources.atmosphereShader, view, camera.projection, cinematic );
    DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );
}


void SkyPass::Render( const RenderCameraLighting& camera, const Math::Transformation::Matrix4& view,
                      const SkullbonezCore::Core::CinematicRenderConfig* cinematic,
                      Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                      SkyPassMode mode )
{
    const bool useCinematicAtmosphere = UsesCinematicAtmosphere( cinematic, mode );

    if ( useCinematicAtmosphere )
    {
        RenderCinematicSky( camera, view, *cinematic, renderGeometry, renderTextures );
        return;
    }

    // Fatal invariant: only the authored cube-map path borrows the world SkyBox. Guard
    // that borrow before matrix work or texture-owner commands; the independent
    // cinematic path above remains valid without a SkyBox.
    SkullbonezCore::Geometry::SkyBox& skyBox = RequireWorldView( "Render" );

    // The cube-map sky follows camera X/Z so the box feels infinitely far away,
    // while its Y stays authored by config to preserve the long-standing horizon.
    Matrix4 skyView = view * Matrix4::Translate( camera.eye.x, m_config.skybox.renderHeight, camera.eye.z ) *
                      Matrix4::Scale( m_config.skybox.scale );

    // Pass contract: cube-map skybox faces sample only slot 0. Slots owned by
    // water, post, or shadows must not leak into these six mesh draws.
    ClearRenderTextureSlotsExcept( renderTextures, RENDER_TEXTURE_SLOT_0 );
    ReportRenderTextureResult( "Frame/Render/Skybox", skyBox.Render( skyView, camera.projection ) );
}


void SceneTargetPass::Begin( const RenderCameraLighting& camera,
                             const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                             Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GeometryOwner& renderGeometry,
                             Rendering::Dx12TextureOwner& renderTextures, Rendering::Dx12Diagnostics& renderDiagnostics,
                             Rendering::RenderGpuTimingOwner* gpuTiming )
{
    // Invariant: from this point onward, draw the world into the HDR scene
    // target instead of directly into the window. The post pass later moves it
    // to the backbuffer with the cinematic effects applied.
    m_resources.hdrTarget->Bind();
    renderFrame.SetViewport( 0, 0, m_resources.hdrTarget->GetWidth(), m_resources.hdrTarget->GetHeight() );
    renderFrame.Clear( {} );

    PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/CinematicSky" );
    {
        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Render/CinematicSky" );
        m_skyPass.Render( camera, camera.baseView, &cinematic, renderGeometry, renderTextures,
                          SkyPassMode::CinematicIfEnabled );
    }
    PROFILE_GPU_END( gpuTiming, "Frame/Render/CinematicSky" );
}


ReflectionPassOutput ReflectionPass::Render( const ReflectionPassInputs& inputs )
{
    ReflectionPassOutput output;
    Rendering::RenderGpuTimingOwner* gpuTiming = inputs.gpuTiming;

    // Concept: two implementations, one water-pass contract. The planar path
    // renders the above-water scene from a mirrored camera into an FBO. The DXR
    // path rebuilds the raytracing TLAS and writes a screen-space reflection
    // texture directly. Both feed the same water shader later.
    // Hazard: texture resolution and framebuffer creation have recoverable
    // early returns. A lexical scope keeps both profiler stacks balanced on
    // every exit instead of requiring each fallback to duplicate an end call.
    PROFILE_GPU_SCOPED( gpuTiming, "Frame/Render/Reflection" );
    DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/Reflection" );

    output.usedDxr = inputs.useDxrReflection;

    if ( inputs.useDxrReflection )
    {
        // Lifetime: the DX12 backend owns the raytracing acceleration
        // structures. The prepared render store streams current per-model
        // transforms into the TLAS before one reflection ray per texture pixel.
        const int ballCount = m_dxrReflectionTransforms
                                  ? CopyDxrRenderInstanceMatrices( inputs.models.renderInstances, m_dxrReflectionTransforms,
                                                                   m_dxrReflectionTransformCapacity )
                                  : 0;

        // Terrain/sphere BLAS objects are owned by the DX12 backend, so the
        // runtime supplies only per-instance sphere transforms here.
        inputs.rayTracing.BuildTLAS(
            std::span<const Matrix4>( m_dxrReflectionTransforms, static_cast<std::size_t>( ballCount ) ) );

        // Ray generation reconstructs world-space rays from screen pixels, so
        // it needs the inverse of the main camera view-projection matrix.
        Rendering::WaterReflectionRayDesc reflection;
        reflection.inverseViewProjection = inputs.camera.viewProjection.Inverse();
        reflection.cameraPosition = inputs.camera.eye;
        reflection.lightPosition = Vector3( inputs.camera.lightPosition[0], inputs.camera.lightPosition[1],
                                            inputs.camera.lightPosition[2] );

        reflection.waterHeight = inputs.waterY;
        reflection.simulationTimeSeconds = inputs.simulationTimeSeconds;

        // Default colors preserve ordinary-water sky misses; cinematic scenes
        // replace the typed values so ray misses match authored void colors.
        if ( inputs.cinematic && inputs.cinematic->enabled )
        {
            reflection.skyColorTop = Vector3( inputs.cinematic->skyZenithR, inputs.cinematic->skyZenithG,
                                              inputs.cinematic->skyZenithB );

            reflection.skyColorBottom = Vector3( inputs.cinematic->skyHorizonR, inputs.cinematic->skyHorizonG,
                                                 inputs.cinematic->skyHorizonB );
        }

        if ( !ResolveRenderTextureHandle( inputs.textures, TEXTURE_BOUNDING_SPHERE, "Frame/Render/Reflection/DXR",
                                          reflection.textures.sphere ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_GROUND, "Frame/Render/Reflection/DXR",
                                          reflection.textures.terrain ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_UP, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyUp ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_DOWN, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyDown ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_RIGHT, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyRight ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_LEFT, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyLeft ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_FRONT, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyFront ) ||
             !ResolveRenderTextureHandle( inputs.textures, TEXTURE_SKY_BACK, "Frame/Render/Reflection/DXR",
                                          reflection.textures.skyBack ) )
        {
            return output;
        }

        inputs.rayTracing.DispatchReflectionRays( reflection );
        output.reflectionTextureHandle = inputs.rayTracing.GetReflectionUAVTexture();
        output.reflectionSampleViewProjection = inputs.camera.viewProjection;
    }
    else
    {
        if ( !m_resources.target )
        {
            return output;
        }

        // Invariant: the planar path binds only its own reflection target and
        // restores the viewport to the window size before water renders.
        m_resources.target->Bind();
        inputs.renderFrame.SetViewport( 0, 0, m_resources.target->GetWidth(), m_resources.target->GetHeight() );
        inputs.renderFrame.Clear( {} );

        // Skybox reflected (XZ follows eye; Y anchored at runtime config).
        // Cinematic mode can reflect the generated sunset sky into the water
        // instead of the usual cube-map sky.
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/Reflection/Skybox" );
        {
            DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/Reflection/Skybox" );
            m_skyPass.Render( inputs.camera, inputs.reflectionView, inputs.cinematic, inputs.renderGeometry,
                              inputs.renderTextures, SkyPassMode::CinematicIfEnabled );
        }
        PROFILE_GPU_END( gpuTiming, "Frame/Render/Reflection/Skybox" );

        // Why: clip at the water surface so the reflection texture contains only
        // the above-water portion of models. The water shader supplies the
        // below-surface visual from the main scene.
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/Reflection/Balls" );
        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/Reflection/Balls" );
        inputs.primitiveRenderer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.waterY );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, -inputs.waterY );

        if ( inputs.collisionStateColorsVisible )
        {
            // Pass contract: collision-state solids are vertex-colored and do
            // not sample textures.
            ClearAllRenderTextureSlots( inputs.renderTextures );

            if ( HasCollisionVisualizerFrameView( inputs.collisionDebug ) )
            {
                const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.collisionDebug );
                m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
                m_collisionVisualizer.Render( inputs.renderGeometry, inputs.renderDiagnostics, frameView,
                                              inputs.reflectionView, inputs.camera.projection, inputs.camera.lightPosition );

                m_collisionVisualizer.SetAlphaOverride( -1.0f );
            }
        }
        else
        {
            // Pass contract: reflected lit models read material color from slot
            // 0 and optional shadow depth from slot 3.
            ClearRenderTextureSlotsExcept( inputs.renderTextures,
                                           RENDER_TEXTURE_SLOT_0 |
                                               ( inputs.objectShadow && inputs.objectShadow->valid ? RENDER_TEXTURE_SLOT_3
                                                                                                   : 0u ) );

            if ( SelectRenderTexture( inputs.textures, TEXTURE_BOUNDING_SPHERE, "Frame/Render/Reflection/Balls" ) )
            {
                inputs.instanceRenderer.RenderReflectionModels( inputs.primitiveShaderBaseName,
                                                                { inputs.reflectionView, inputs.camera.projection,
                                                                  inputs.camera.lightPosition },
                                                                inputs.cinematic, inputs.objectShadow, inputs.bodyAlpha );
            }
        }

        inputs.primitiveRenderer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        m_collisionVisualizer.SetClipPlane( 0.0f, 1.0f, 0.0f, 1.0e9f );
        PROFILE_GPU_END( gpuTiming, "Frame/Render/Reflection/Balls" );

        m_resources.target->Unbind();
        inputs.renderFrame.SetViewport( 0, 0, inputs.windowWidth, inputs.windowHeight );
        output.reflectionTextureHandle = m_resources.target->GetColorTextureHandle();
        output.reflectionSampleViewProjection = inputs.reflectionViewProjection;
    }

    return output;
}


void ObjectPass::Render( const ObjectPassInputs& inputs )
{
    const bool transparentPass = inputs.mode == ObjectPassMode::Transparent;
    const char* passName = transparentPass ? "Frame/Render/TransparentBalls" : "Frame/Render/Balls";
    const uint32_t passHash = transparentPass ? HashStr( "Frame/Render/TransparentBalls" ) : HashStr( "Frame/Render/Balls" );

#if defined( SKULLBONEZ_PROFILE_ENABLED )
    SkullbonezCore::Rendering::RenderGpuTimingScope profileScope( inputs.gpuTiming, passName, passHash );
#endif
    Rendering::DrawCallTraceScope drawTraceScope( inputs.renderDiagnostics, passName, passHash );
    Rendering::Dx12TextureOwner& renderTextures = inputs.renderTextures;

    if ( inputs.collisionStateColorsVisible )
    {
        // Pass contract: collision-state solids are vertex-colored and do not
        // sample textures.
        ClearAllRenderTextureSlots( renderTextures );

        if ( HasCollisionVisualizerFrameView( inputs.collisionDebug ) )
        {
            const CollisionVisualizerFrameView frameView = BuildCollisionVisualizerFrameView( inputs.collisionDebug );
            m_collisionVisualizer.SetAlphaOverride( inputs.collisionVisualizerAlphaOverride );
            m_collisionVisualizer.Render( inputs.renderGeometry, inputs.renderDiagnostics, frameView, inputs.camera.baseView,
                                          inputs.camera.projection, inputs.camera.lightPosition );

            m_collisionVisualizer.SetAlphaOverride( -1.0f );
        }
    }
    else
    {
        // Pass contract: lit model shaders read the material texture in slot 0
        // and optionally the shadow depth texture in slot 3.
        ClearRenderTextureSlotsExcept( renderTextures,
                                       RENDER_TEXTURE_SLOT_0 |
                                           ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) );

        if ( SelectRenderTexture( inputs.textures, TEXTURE_BOUNDING_SPHERE, passName ) )
        {
            const Rendering::RenderModelSelection selection = !inputs.modelMask
                                                                  ? Rendering::RenderModelSelection::All()
                                                                  : ( inputs.drawMaskedModels
                                                                          ? Rendering::RenderModelSelection::Marked(
                                                                                *inputs.modelMask )
                                                                          : Rendering::RenderModelSelection::Unmarked(
                                                                                *inputs.modelMask ) );
            inputs.instanceRenderer.RenderModels( inputs.primitiveShaderBaseName,
                                                  { inputs.camera.baseView, inputs.camera.projection,
                                                    inputs.camera.lightPosition },
                                                  inputs.cinematic, inputs.shadow, inputs.bodyAlpha, selection );
        }
    }
}


void ObjectPass::EnsureGpuResources( Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                                     Rendering::Dx12GeometryOwner& renderGeometry )
{
    // Collision-state solids can be selected by the ordinary or reflection
    // object pass. Prepare their lazy backend objects here while the caller owns
    // the BackendInit allocation phase, before either guarded draw path.
    PrepareCollisionVisualizerResourcePhase(
        [&]() { m_collisionVisualizer.EnsureGpuResources( assets, renderResources, renderGeometry ); },
        [&]() { return m_collisionVisualizer.ResourcesReady(); } );
}


void TerrainPass::Render( const TerrainPassInputs& inputs )
{
    if ( inputs.terrainHidden || !inputs.terrain )
    {
        return;
    }

    PROFILE_GPU_BEGIN( inputs.gpuTiming, "Frame/Render/Terrain" );
    DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/Terrain" );

    // Pass contract: terrain reads ground albedo from t0, the broad shadow map
    // from t3, and the tight object-shadow map from t5. The material table stays
    // at t4 for instanced object draws and is never repurposed here.
    Rendering::Dx12TextureOwner& renderTextures = inputs.renderTextures;
    ClearRenderTextureSlotsExcept( renderTextures,
                                   RENDER_TEXTURE_SLOT_0 |
                                       ( inputs.shadow && inputs.shadow->valid ? RENDER_TEXTURE_SLOT_3 : 0u ) |
                                       ( inputs.detailShadow && inputs.detailShadow->valid ? RENDER_TEXTURE_SLOT_5 : 0u ) );

    if ( SelectRenderTexture( inputs.textures, TEXTURE_GROUND, "Frame/Render/Terrain" ) )
    {
        inputs.terrain->Render( inputs.camera.baseView, inputs.camera.projection, renderTextures,
                                inputs.camera.lightPosition, inputs.clipPlane, TERRAIN_RASTER, inputs.cinematic,
                                inputs.shadow, inputs.detailShadow );
    }

    PROFILE_GPU_END( inputs.gpuTiming, "Frame/Render/Terrain" );
}


void TerrainPass::EnsureGpuResources( Geometry::Terrain* terrain, Assets::AssetSystem& assets,
                                      Rendering::Dx12ResourceBuilder& renderResources )
{
    // Terrain mesh/material resources live on Terrain; this pass owns ordering
    // and the receiver texture-slot contract.
    if ( terrain )
    {
        terrain->EnsureRenderResources( m_config, assets, renderResources );
    }
}


void TerrainPass::ReleaseGpuResources( Geometry::Terrain* terrain )
{
    if ( terrain )
    {
        terrain->ReleaseRenderResources();
    }
}


void WaterPass::Render( const WaterPassInputs& inputs )
{
    m_debugInfo = WaterPassDebugInfo();
    m_debugInfo.skippedHidden = inputs.waterHidden;
    m_debugInfo.skippedModeOff = inputs.cinematicEnabled && inputs.cinematic && inputs.cinematic->waterMode == 0;
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

    PROFILE_GPU_BEGIN( inputs.gpuTiming, "Frame/Render/Water" );
    DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/Water" );

    // Pass contract: water samples only the reflection texture in slot 1.
    Rendering::Dx12TextureOwner& renderTextures = inputs.renderTextures;
    ClearRenderTextureSlotsExcept( renderTextures, RENDER_TEXTURE_SLOT_1 );
    float waterTime = inputs.freezeTime ? inputs.frozenTime : inputs.liveWaterTime;
    m_debugInfo.rendered = true;
    m_debugInfo.waterTime = waterTime;
    SkullbonezCore::Environment::WaterReflectionInput reflectionInput;
    reflectionInput.sampleViewProjection = inputs.reflection.reflectionSampleViewProjection;
    reflectionInput.textureHandle = inputs.reflection.reflectionTextureHandle;
    reflectionInput.noReflection = inputs.noReflection;
    reflectionInput.raytraced = inputs.reflection.usedDxr;

    m_world.RenderFluid( inputs.camera.baseView, inputs.camera.projection, inputs.camera.eye, renderTextures,
                         reflectionInput, WATER_RASTER, waterTime, inputs.flatWater, inputs.cinematicEnabled,
                         inputs.cinematic );

    PROFILE_GPU_END( inputs.gpuTiming, "Frame/Render/Water" );
}


void WaterPass::EnsureGpuResources( Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources )
{
    // Water shader/mesh resources are owned by WorldEnvironment; this pass
    // makes reflection input explicit and keeps water downstream of reflection.
    m_world.EnsureRenderResources( m_config, assets, renderResources );
}


void WaterPass::ReleaseGpuResources()
{
    // WorldEnvironment owns fluid render resources.
}


bool DebugOverlayPass::Render( const DebugOverlayPassInputs& inputs )
{
    // Debug overlays intentionally stay out of the object/material pass. They
    // draw diagnostic geometry over the final world view and should not inherit
    // production material binding assumptions.
    if ( !HasOverlayWork( inputs ) )
    {
        return false;
    }

    Rendering::RenderGpuTimingOwner* gpuTiming = inputs.gpuTiming;
    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();

    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay" );
    }

    DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Frame/Render/DebugOverlay" );
    if ( inputs.snapshot.broadphaseOverlayVisible )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay/Broadphase" );
        }

        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "Broadphase" );

        // Pass contract: broadphase owns grid-line generation, while renderer
        // readiness/capability stays with the one-frame debug overlay context.
        const bool supportsDebugLines = inputs.renderDiagnostics.GetCapabilities().supportsDebugLines;
        m_broadphaseVisualizer.Render( inputs.camera.viewProjection, inputs.renderGeometry, supportsDebugLines );

        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay/Broadphase" );
        }
    }

    if ( !inputs.snapshot.worldExtensionDebugLines.empty() )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay/WorldExtension" );
        }

        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "WorldExtension" );

        if ( inputs.renderDiagnostics.GetCapabilities().supportsDebugLines )
        {
            inputs.renderGeometry.DrawLinesColored( inputs.snapshot.worldExtensionDebugLines, inputs.camera.viewProjection,
                                                    DEBUG_LINE_RASTER );
        }

        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay/WorldExtension" );
        }
    }

    // Invariant: production submission and validation observe this same
    // replay-owned packet; neither may rebuild geometry from tracer internals.
    PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay/ReplayVisuals" );
    RenderReplayVisualPacket( inputs.replayVisualPacket, inputs.camera.viewProjection, inputs.renderGeometry );
    PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay/ReplayVisuals" );

    if ( inputs.retainedOverlay.HasGeometry() )
    {
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay/RetainedOverlay" );

        if ( !inputs.retainedOverlay.ribbonRanges.empty() )
        {
            inputs.renderGeometry.DrawRetainedGeometryRanges( inputs.retainedOverlay.compactRibbonRecords,
                                                              inputs.retainedOverlay.ribbonRanges,
                                                              inputs.retainedOverlay.stream, inputs.camera.viewProjection,
                                                              Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                              RETAINED_OVERLAY_DEPTH_HINT_RASTER );
            inputs.renderGeometry.DrawRetainedGeometryRanges( inputs.retainedOverlay.compactRibbonRecords,
                                                              inputs.retainedOverlay.ribbonRanges,
                                                              inputs.retainedOverlay.stream, inputs.camera.viewProjection,
                                                              Rendering::TransientTriangleStyle::InstancedRibbon,
                                                              RETAINED_OVERLAY_VISIBLE_RASTER );
        }

        if ( !inputs.retainedOverlay.coloredLineVertices.empty() )
        {
            inputs.renderGeometry.DrawRetainedLinesColored( inputs.retainedOverlay.coloredLineVertices,
                                                            inputs.retainedOverlay.stream, false,
                                                            inputs.camera.viewProjection, DEBUG_LINE_RASTER );
        }

        PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay/RetainedOverlay" );
    }

    RenderLauncherShots( inputs );

    if ( inputs.contactPresentation.HasGeometry() )
    {
        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "ContactManifold" );
        m_physicsDebugVisualizer.RenderContactManifold( inputs.contactPresentation, inputs.camera.viewProjection,
                                                        inputs.renderGeometry,
                                                        inputs.renderDiagnostics.GetCapabilities().supportsDebugLines );
    }

    if ( inputs.snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/DebugOverlay/PhysicsDebug" );
        }

        DRAW_CALL_TRACE_SCOPE( inputs.renderDiagnostics, "PhysicsDebug" );
        m_physicsDebugVisualizer.SetFlags( inputs.snapshot.physicsDebugFlags );
        m_physicsDebugVisualizer.SetPipelineStageCursor( inputs.snapshot.physicsDebugPipelineStageCursor );

        if ( HasPhysicsDebugFrameView( inputs.physicsDebug ) )
        {
            const PhysicsDebugFrameView frameView = BuildPhysicsDebugFrameView( inputs.physicsDebug );

            // Pass contract: physics debug owns diagnostic line generation,
            // while renderer readiness/capability stays with this frame pass.
            const bool supportsDebugLines = inputs.renderDiagnostics.GetCapabilities().supportsDebugLines;
            m_physicsDebugVisualizer.Render( frameView, inputs.camera.viewProjection, inputs.renderGeometry,
                                             supportsDebugLines, inputs.terrain );
        }

        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay/PhysicsDebug" );
        }
    }

    if ( detailMarkers )
    {
        PROFILE_GPU_END( gpuTiming, "Frame/Render/DebugOverlay" );
    }

    return true;
}


bool DebugOverlayPass::HasOverlayWork( const DebugOverlayPassInputs& inputs ) const
{
    const DebugOverlaySnapshot& snapshot = inputs.snapshot;

    if ( snapshot.broadphaseOverlayVisible )
    {
        return true;
    }

    if ( !snapshot.worldExtensionDebugLines.empty() )
    {
        return true;
    }

    if ( snapshot.physicsDebugFlags != PHYSICS_DEBUG_NONE )
    {
        return true;
    }

    if ( inputs.contactPresentation.HasGeometry() )
    {
        return true;
    }

    // Invariant: replay owns a complete frame packet. Its fixed-capacity lines
    // and ribbons are sufficient pass work even when editor tools are hidden.
    if ( inputs.replayVisualPacket.HasGeometry() )
    {
        return true;
    }

    if ( inputs.retainedOverlay.HasGeometry() )
    {
        return true;
    }

    if ( !snapshot.launcherShots.empty() )
    {
        for ( const RenderToolOverlayView::LauncherShot& shot : snapshot.launcherShots )
        {
            if ( shot.active )
            {
                return true;
            }
        }
    }

    return snapshot.editorOverlayWorkVisible;
}


DebugOverlayPass::~DebugOverlayPass() = default;

void DebugOverlayPass::ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry )
{
    if ( renderGeometry && m_launcherDynamicVB != 0 )
    {
        renderGeometry->DestroyDynamicVB( m_launcherDynamicVB );
    }

    m_launcherDynamicVB = 0;
    m_launcherShader.reset();
    m_launcherRasterStatePrepared = false;
    m_launcherVertices.clear();
}

void DebugOverlayPass::EmitLauncherVertex( const Vector3& point, float r, float g, float b, float a )
{
    m_launcherVertices.insert( m_launcherVertices.end(), { point.x, point.y, point.z, r, g, b, a } );
}

void DebugOverlayPass::EmitLauncherQuad( const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, float r,
                                         float g, float blue, float alpha )
{
    EmitLauncherVertex( a, r, g, blue, alpha );
    EmitLauncherVertex( b, r, g, blue, alpha );
    EmitLauncherVertex( c, r, g, blue, alpha );
    EmitLauncherVertex( a, r, g, blue, alpha );
    EmitLauncherVertex( c, r, g, blue, alpha );
    EmitLauncherVertex( d, r, g, blue, alpha );
}

void DebugOverlayPass::EmitLauncherRibbon( const Vector3& a, const Vector3& b, const Vector3& widthAxis, float halfWidth,
                                           float r, float g, float blue, float alpha )
{
    const Vector3 width = widthAxis * halfWidth;
    EmitLauncherQuad( a - width, b - width, b + width, a + width, r, g, blue, alpha );
}

void DebugOverlayPass::EmitLauncherShot( const RenderToolOverlayView::LauncherShot& shot )
{
    if ( !shot.active || shot.lifetimeSeconds <= TOLERANCE )
    {
        return;
    }

    const Vector3 segment = shot.end - shot.start;
    const float segmentLengthSquared = VectorMagSquared( segment );

    if ( segmentLengthSquared <= LAUNCHER_MIN_SEGMENT_LENGTH * LAUNCHER_MIN_SEGMENT_LENGTH )
    {
        return;
    }

    const float normalizedAge = std::clamp( shot.ageSeconds / shot.lifetimeSeconds, 0.0f, 1.0f );
    const float afterimageFade = std::sqrt( 1.0f - normalizedAge );
    const float coreFade = ( 1.0f - normalizedAge ) * ( 1.0f - normalizedAge );

    if ( afterimageFade <= 0.0f )
    {
        return;
    }

    const Vector3 direction = segment * ( 1.0f / sqrtf( segmentLengthSquared ) );
    Vector3 screenRight = NormalizeOr( shot.cameraRight, CrossProduct( direction, Vector3( 0.0f, 1.0f, 0.0f ) ) );

    if ( VectorMagSquared( screenRight ) <= TOLERANCE * TOLERANCE )
    {
        screenRight = CrossProduct( direction, Vector3( 0.0f, 1.0f, 0.0f ) );
    }

    screenRight = NormalizeOr( screenRight, Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 screenUp = NormalizeOr( shot.cameraUp, NormalizeOr( CrossProduct( screenRight, direction ),
                                                                      Vector3( 0.0f, 1.0f, 0.0f ) ) );

    EmitLauncherRibbon( shot.start, shot.end, screenRight, LAUNCHER_AFTERIMAGE_HALF_WIDTH, 0.02f, 0.45f, 1.0f,
                        0.12f * afterimageFade );
    EmitLauncherRibbon( shot.start, shot.end, screenUp, LAUNCHER_AFTERIMAGE_HALF_WIDTH * 0.55f, 0.06f, 0.82f, 1.0f,
                        0.08f * afterimageFade );
    EmitLauncherRibbon( shot.start, shot.end, screenRight, LAUNCHER_OUTER_HALF_WIDTH, 0.05f, 0.96f, 1.0f,
                        0.30f * afterimageFade );
    EmitLauncherRibbon( shot.start, shot.end, screenUp, LAUNCHER_OUTER_HALF_WIDTH * 0.42f, 0.22f, 0.98f, 1.0f,
                        0.22f * afterimageFade );
    EmitLauncherRibbon( shot.start, shot.end, screenRight, LAUNCHER_CORE_HALF_WIDTH, 1.0f, 0.95f, 0.28f, 0.98f * coreFade );
    EmitLauncherRibbon( shot.start, shot.end, screenUp, LAUNCHER_CORE_HALF_WIDTH * 0.72f, 1.0f, 0.58f, 0.16f,
                        0.82f * coreFade );

    if ( shot.hit )
    {
        const Vector3 x = screenRight * LAUNCHER_IMPACT_DISC_HALF_SIZE;
        const Vector3 y = screenUp * LAUNCHER_IMPACT_DISC_HALF_SIZE;
        EmitLauncherQuad( shot.end - x - y, shot.end + x - y, shot.end + x + y, shot.end - x + y, 1.0f, 0.72f, 0.18f,
                          0.58f * afterimageFade );
        EmitLauncherRibbon( shot.end - screenRight * LAUNCHER_IMPACT_HALF_SIZE,
                            shot.end + screenRight * LAUNCHER_IMPACT_HALF_SIZE, screenUp, LAUNCHER_CORE_HALF_WIDTH * 1.5f,
                            1.0f, 0.46f, 0.12f, 0.90f * coreFade );
        EmitLauncherRibbon( shot.end - screenUp * LAUNCHER_IMPACT_HALF_SIZE, shot.end + screenUp * LAUNCHER_IMPACT_HALF_SIZE,
                            screenRight, LAUNCHER_CORE_HALF_WIDTH * 1.5f, 1.0f, 0.84f, 0.22f, 0.82f * coreFade );
    }
}

void DebugOverlayPass::RenderLauncherShots( const DebugOverlayPassInputs& inputs )
{
    m_launcherVertices.clear();

    for ( const RenderToolOverlayView::LauncherShot& shot : inputs.snapshot.launcherShots )
    {
        EmitLauncherShot( shot );
    }

    if ( m_launcherVertices.empty() )
    {
        return;
    }

    if ( !m_launcherShader )
    {
        m_launcherShader = inputs.assets.CreateShader( inputs.renderResources, "shader.launcher_laser" );
    }

    if ( m_launcherDynamicVB == 0 )
    {
        const int attributes[] = { 3, 4 };
        m_launcherDynamicVB = inputs.renderGeometry.CreateDynamicVB( attributes, 2, LAUNCHER_MAX_VERTICES );
        m_launcherVertices.reserve( static_cast<std::size_t>( LAUNCHER_MAX_VERTICES ) * 7u );
    }

    if ( !m_launcherShader || m_launcherDynamicVB == 0 )
    {
        return;
    }

    m_launcherShader->Use();

    if ( !m_launcherRasterStatePrepared )
    {
        m_launcherRasterStatePrepared = inputs.renderGeometry.PrecompileDynamicVBRasterState( m_launcherDynamicVB,
                                                                                              LAUNCHER_RASTER_BUCKET );
    }

    if ( !m_launcherRasterStatePrepared )
    {
        return;
    }

    m_launcherShader->SetMat4( "uViewProj", inputs.camera.viewProjection );
    inputs.renderGeometry.UploadAndDrawDynamicVB( m_launcherDynamicVB, m_launcherVertices, LAUNCHER_RASTER_BUCKET );
}


void VolumetricPass::EnsureGpuResources( bool cinematicEnabled, Assets::AssetSystem& assets,
                                         Rendering::Dx12ResourceBuilder& renderResources )
{
    if ( !cinematicEnabled )
    {
        return;
    }

    if ( !m_volumetricResources.shader )
    {
        // Half-resolution pass: creates warm light shafts that tonemap can add
        // without making every world shader understand volumetric lighting.
        m_volumetricResources.shader = assets.CreateShader( renderResources, "shader.post_volumetric_light" );
    }
}


void VolumetricPass::ReleaseGpuResources()
{
    m_volumetricResources.shader.reset();
}


bool VolumetricPass::CanRender( bool cinematicEnabled, const SkullbonezCore::Core::CinematicRenderConfig* cinematic ) const
{
    return cinematicEnabled && cinematic && cinematic->volumetricLightingEnabled && m_sceneResources.hdrTarget &&
           m_volumetricResources.shader && m_fullscreenResources.quadVB != 0;
}


bool VolumetricPass::Render( const RenderCameraLighting& camera,
                             const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                             Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                             Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12GraphTransientPool& renderGraph,
                             Rendering::Dx12Diagnostics& renderDiagnostics, Rendering::RenderGpuTimingOwner* gpuTiming,
                             int windowWidth, int windowHeight, const Rendering::RenderGraphTextureBinding* graphOutput )
{
    if ( !CanRender( true, &cinematic ) )
    {
        return false;
    }

    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();

    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/VolumetricLight" );
    }

    DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Render/VolumetricLight" );
    const bool useGraphOutput = graphOutput && graphOutput->IsValid() && graphOutput->renderTarget;

    if ( !useGraphOutput )
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/VolumetricLight" );
        }

        return false;
    }

    renderGraph.BeginGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderFrame.SetViewport( 0, 0, graphOutput->width, graphOutput->height );

    // This is another screen-space effect, so depth testing and blending are
    // disabled while the full-screen quad is generated.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/VolumetricLight/Draw" );
        }

        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Draw" );
        m_volumetricResources.shader->Use();
        BindVolumetricPassParams( *m_volumetricResources.shader, camera.eye, camera.viewProjection, cinematic,
                                  m_config.camera.frustumNear, m_config.camera.frustumFar );

        // Pass contract: texture slot 0 is rendered color, slot 1 is rendered
        // depth. The shader uses depth to tell sky pixels from solid geometry so
        // rays pass through sky and fade when they cross hills/balls.
        BindRenderTextureSlots( renderTextures, m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(), 0, 0 );

        DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );

        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/VolumetricLight/Draw" );
        }
    }

    renderGraph.EndGraphTextureRenderTarget( *graphOutput, "VolumetricLightPass" );
    renderFrame.SetViewport( 0, 0, windowWidth, windowHeight );

    if ( detailMarkers )
    {
        PROFILE_GPU_END( gpuTiming, "Frame/Render/VolumetricLight" );
    }

    return true;
}


void TonemapPass::EnsureGpuResources( bool cinematicEnabled, Assets::AssetSystem& assets,
                                      Rendering::Dx12ResourceBuilder& renderResources )
{
    if ( !cinematicEnabled )
    {
        return;
    }

    if ( !m_tonemapResources.shader )
    {
        // Final full-screen shader: combines HDR scene color, depth fog, bloom,
        // grade, vignette, and optional volumetric light into the backbuffer.
        m_tonemapResources.shader = assets.CreateShader( renderResources, "shader.post_tonemap" );
    }
}


void TonemapPass::ReleaseGpuResources()
{
    m_tonemapResources.shader.reset();
}


void TonemapPass::Render( const SkullbonezCore::Core::CinematicRenderConfig& cinematic,
                          Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12TextureOwner& renderTextures,
                          Rendering::Dx12FrameOwner& renderFrame, Rendering::Dx12Diagnostics& renderDiagnostics,
                          Rendering::RenderGpuTimingOwner* gpuTiming, int windowWidth, int windowHeight,
                          bool sceneAlreadyUnbound, bool volumetricReady,
                          const Rendering::RenderGraphTextureBinding* graphVolumetric )
{
    if ( !m_sceneResources.hdrTarget || !m_tonemapResources.shader || m_fullscreenResources.quadVB == 0 )
    {
        return;
    }

    const bool detailMarkers = SkullbonezCore::Core::PlatformProfiler::AreDetailedRangesEnabled();

    if ( detailMarkers )
    {
        PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/Tonemap" );
    }

    DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Frame/Render/Tonemap" );

    if ( !sceneAlreadyUnbound )
    {
        m_sceneResources.hdrTarget->Unbind();
    }

    renderFrame.SetViewport( 0, 0, windowWidth, windowHeight );

    // Concept: "resolve" means "turn our off-screen cinematic render target
    // into the final image on the window." This is where the HDR scene becomes
    // normal display color and where bloom/fog/rays are layered in.
    {
        if ( detailMarkers )
        {
            PROFILE_GPU_BEGIN( gpuTiming, "Frame/Render/Tonemap/Draw" );
        }

        DRAW_CALL_TRACE_SCOPE( renderDiagnostics, "Draw" );
        m_tonemapResources.shader->Use();
        BindTonemapPassParams( *m_tonemapResources.shader, cinematic, m_config.camera.frustumNear,
                               m_config.camera.frustumFar, m_sceneResources.hdrTarget->GetWidth(),
                               m_sceneResources.hdrTarget->GetHeight(), volumetricReady );

        const bool useGraphVolumetric = volumetricReady && graphVolumetric && graphVolumetric->IsValid() &&
                                        graphVolumetric->shaderResource;

        const uint32_t volumetricTexture = useGraphVolumetric ? graphVolumetric->textureHandle
                                                              : m_sceneResources.hdrTarget->GetColorTextureHandle();

        // Pass contract: slot 0 is the bright HDR scene, slot 1 is its depth
        // buffer for fog, and slot 2 is the sole completed shaft texture or a
        // harmless fallback when the volumetric pass is disabled.
        BindRenderTextureSlots( renderTextures, m_sceneResources.hdrTarget->GetColorTextureHandle(),
                                m_sceneResources.hdrTarget->GetDepthTextureHandle(), volumetricTexture, 0 );

        DrawFullscreenQuad( renderGeometry, m_fullscreenResources.quadVB, FULLSCREEN_OPAQUE_RASTER );

        if ( detailMarkers )
        {
            PROFILE_GPU_END( gpuTiming, "Frame/Render/Tonemap/Draw" );
        }
    }

    if ( detailMarkers )
    {
        PROFILE_GPU_END( gpuTiming, "Frame/Render/Tonemap" );
    }
}
