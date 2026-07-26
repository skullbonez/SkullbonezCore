/*
File: SkullbonezSource/Rendering/Shadow.h
Purpose:
  Defines shadow-map frame data shared by renderers and scene objects.

Summary:
  Shadow.h defines shadow-map frame data shared by renderers and scene
  objects. As a public header, keep edits anchored on render submission and
  resource lifetime and on the glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Convex hull: Immutable authored collision geometry that can also provide a
    precise shadow-caster silhouette.
  Caster value: Prepared model transform plus conservative world-space radius
    used to make a per-shadow-map visibility decision without owner lookups.
  Percentage-closer filtering (PCF): Averages several depth comparisons to
    soften a shadow boundary without blurring stored depth values.

Invariants:
  - ShadowFrameData is frame-local render input; it does not own the depth
    texture backing its opaque handle.
  - Disabled receivers must clear the shadow texture binding so stale descriptor
    state cannot affect later draws.
  - Detail shadow sampling appends t5 and never aliases the t4 material table.
  - Prepared caster values borrow no model state; convex hull geometry is the
    only frame-local borrowed payload.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "RenderCommandTypes.h"
#include "DX12/RenderBackendDX12.h"
#include "DX12/ShaderDX12.h"
#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"
#include "../Physics/ConvexHullShape.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
inline constexpr int SHADOW_TEXTURE_SLOT = 3;
inline constexpr int DETAIL_SHADOW_TEXTURE_SLOT = 5;

struct ShadowCasterInstance
{
    Math::Transformation::Matrix4 model;
    float boundingRadius = 0.0f;
};

struct ShadowConvexHullCaster
{
    const Math::CollisionDetection::ConvexHullShape* hull = nullptr;
    ShadowCasterInstance instance;
};

struct ShadowCasterBatches
{

    // CPU-built caster streams. Worker jobs may fill these payloads, but only
    // the main thread may submit them through the active render command path.
    // Convex hull payloads borrow immutable hull geometry owned by the live
    // model collection for this frame.
    std::vector<ShadowCasterInstance> spheres;
    std::vector<ShadowCasterInstance> boxes;
    std::vector<ShadowCasterInstance> pines;
    std::vector<ShadowConvexHullCaster> convexHulls;

    void ReserveForModelCapacity( int capacity )
    {
        const std::size_t count = static_cast<std::size_t>( (std::max)( 0, capacity ) );
        spheres.reserve( count );
        boxes.reserve( count );
        pines.reserve( count );
        convexHulls.reserve( count );
    }

    bool HasCapacityForModelCount( int modelCount ) const
    {
        const std::size_t count = static_cast<std::size_t>( (std::max)( 0, modelCount ) );
        return spheres.capacity() >= count && boxes.capacity() >= count && pines.capacity() >= count &&
               convexHulls.capacity() >= count;
    }

    void Clear()
    {
        spheres.clear();
        boxes.clear();
        pines.clear();
        convexHulls.clear();
    }

    bool Empty() const
    {
        return spheres.empty() && boxes.empty() && pines.empty() && convexHulls.empty();
    }
};

struct ShadowFrameData
{

    // Light-space camera used by the shadow pass. The depth pass renders casters
    // with lightView/lightProjection, while receivers use lightViewProjection to
    // transform their world-space fragment position back into the same clip
    // space before sampling the depth texture.
    Math::Transformation::Matrix4 lightView;
    Math::Transformation::Matrix4 lightProjection;
    Math::Transformation::Matrix4 lightViewProjection;

    // Direction from the scene toward the light source. This is stored in world
    // space because receivers already have world positions and can derive their
    // own view-space lighting vectors separately.
    Math::Vector::Vector3 lightDirectionWorld;

    // Opaque texture handle for the framebuffer depth attachment. Render code
    // passes this neutral handle back to Dx12GeometryOwner::BindTexture
    // instead of seeing the native DX12 resource or descriptor row.
    uint32_t depthTextureHandle = 0;

    // Sampling controls copied from the active config for this frame. `texelSize`
    // is 1/mapSize, and shader code multiplies it by softness to spread PCF
    // taps without having to know the texture dimensions.
    int mapSize = 0;
    int pcfRadius = 1;
    float strength = 0.0f;
    float depthBias = 0.0015f;
    float slopeBias = 0.0035f;
    float texelSize = 0.0f;
    float softness = 1.0f;
    bool zeroToOneDepth = false;
    bool terrainReceives = true;
    bool objectsReceive = true;
    bool valid = false;
};

struct ShadowReceiverBias
{
    float depth = 0.0f;
    float slope = 0.0f;
};

inline ShadowReceiverBias ResolveShadowReceiverBias( const ShadowFrameData& shadow, bool objectReceiver )
{

    if ( !objectReceiver )
    {
        return { shadow.depthBias, shadow.slopeBias };
    }

    // High (2048) and Ultra (4096+) use resolution-aware object floors. The old
    // 0.0015/0.0035 clamps detached contact shadows; these bounded values retain
    // enough slope protection while allowing higher-resolution maps to reduce
    // peter-panning proportionally.
    const float resolutionScale = shadow.mapSize > 0
                                      ? std::clamp( 2048.0f / static_cast<float>( shadow.mapSize ), 0.5f, 1.0f )
                                      : 1.0f;
    return { (std::max)( shadow.depthBias, 0.00035f * resolutionScale ),
             (std::max)( shadow.slopeBias, 0.00075f * resolutionScale ) };
}

inline void SnapShadowProjectionToTexelGrid( Math::Transformation::Matrix4& projection,
                                             const Math::Transformation::Matrix4& view, int mapSize )
{

    if ( mapSize <= 0 )
    {
        return;
    }

    // Concept: lock the world origin to an integer shadow-map texel. The view
    // may follow a moving tight-object focus, but sub-texel motion is absorbed
    // by the orthographic projection translation instead of crawling across
    // receiver pixels. Matrix storage is column-major, so m[12]/m[13] are the
    // clip-space translation terms used by the shader-facing product.
    const Math::Transformation::Matrix4 viewProjection = projection * view;
    const float halfMapSize = static_cast<float>( mapSize ) * 0.5f;
    const float originTexelX = viewProjection.m[12] * halfMapSize;
    const float originTexelY = viewProjection.m[13] * halfMapSize;
    const float snappedTexelX = std::round( originTexelX );
    const float snappedTexelY = std::round( originTexelY );
    projection.m[12] += ( snappedTexelX - originTexelX ) / halfMapSize;
    projection.m[13] += ( snappedTexelY - originTexelY ) / halfMapSize;
}

inline void ApplyShadowReceiverUniforms( ShaderDX12& shader, Dx12TextureOwner& textures, const ShadowFrameData* shadow,
                                         bool receive, bool objectReceiver = false )
{

    // Receivers call this unconditionally, even when shadows are disabled. That
    // keeps all lit shaders using the same uniform layout and avoids stale GPU
    // state: disabled receivers get identity matrices, zero strength, and an
    // enabled flag of 0, so their shader returns full visibility.
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Math::Transformation::Matrix4 identity;
    const ShadowReceiverBias bias = enabled ? ResolveShadowReceiverBias( *shadow, objectReceiver ) : ShadowReceiverBias();
    shader.SetMat4( "uShadowViewProj", enabled ? shadow->lightViewProjection : identity );
    shader.SetVec4( "uShadowParams", enabled ? shadow->strength : 0.0f, bias.depth, bias.slope,
                    enabled ? shadow->texelSize * shadow->softness : 0.0f );
    shader.SetVec4( "uShadowFlags", enabled ? 1.0f : 0.0f, receive ? 1.0f : 0.0f,
                    enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f,
                    enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f );
    shader.SetVec4( "uShadowLightDir", enabled ? shadow->lightDirectionWorld.x : 0.0f,
                    enabled ? shadow->lightDirectionWorld.y : 1.0f, enabled ? shadow->lightDirectionWorld.z : 0.0f, 0.0f );
    shader.SetInt( "uShadowMap", SHADOW_TEXTURE_SLOT );

    if ( enabled )
    {
        textures.BindTexture( shadow->depthTextureHandle, SHADOW_TEXTURE_SLOT );
    }
    else
    {

        // Pass contract: disabled receivers must not inherit an old shadow map
        // binding. The shader would skip sampling, but clearing the slot keeps
        // descriptor lifetime visible to the backend.
        textures.BindTexture( 0, SHADOW_TEXTURE_SLOT );
    }
}

inline void ApplyDetailShadowReceiverUniforms( ShaderDX12& shader, Dx12TextureOwner& textures, const ShadowFrameData* shadow,
                                               bool receive )
{

    // Concept: terrain keeps its broad-map payload at t3 and layers a tighter
    // object projection through t5. This deliberately appends a binding after
    // the t4 material table instead of reinterpreting object material state.
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Math::Transformation::Matrix4 identity;
    shader.SetMat4( "uDetailShadowViewProj", enabled ? shadow->lightViewProjection : identity );
    shader.SetVec4( "uDetailShadowParams", enabled ? shadow->strength : 0.0f, enabled ? shadow->depthBias : 0.0f,
                    enabled ? shadow->slopeBias : 0.0f, enabled ? shadow->texelSize * shadow->softness : 0.0f );
    shader.SetVec4( "uDetailShadowFlags", enabled ? 1.0f : 0.0f, receive ? 1.0f : 0.0f,
                    enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f,
                    enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f );
    shader.SetInt( "uDetailShadowMap", DETAIL_SHADOW_TEXTURE_SLOT );

    // Lifetime: the frame payload borrows the depth handle. Clearing t5 on the
    // disabled path prevents a descriptor from surviving its producing pass.
    textures.BindTexture( enabled ? shadow->depthTextureHandle : 0, DETAIL_SHADOW_TEXTURE_SLOT );
}
} // namespace Rendering
} // namespace SkullbonezCore
