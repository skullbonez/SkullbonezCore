/*
File: SkullbonezSource/SkullbonezShadow.h
Purpose:
  Defines shadow-map frame data shared by renderers and scene objects.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "SkullbonezIRenderBackend.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"
#include <algorithm>

namespace SkullbonezCore
{
namespace Rendering
{
inline constexpr int SHADOW_TEXTURE_SLOT = 3;

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
    // passes this neutral handle back to IRenderBackend::BindTexture instead
    // of seeing the native DX12 resource or descriptor row.
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

inline void ApplyShadowReceiverUniforms( IShader& shader, const ShadowFrameData* shadow, bool receive, bool objectReceiver = false )
{
    // Receivers call this unconditionally, even when shadows are disabled. That
    // keeps all lit shaders using the same uniform layout and avoids stale GPU
    // state: disabled receivers get identity matrices, zero strength, and an
    // enabled flag of 0, so their shader returns full visibility.
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Math::Transformation::Matrix4 identity;
    const float depthBias = enabled
                                ? ( objectReceiver ? (std::max)( shadow->depthBias, 0.0015f ) : shadow->depthBias )
                                : 0.0f;
    const float slopeBias = enabled
                                ? ( objectReceiver ? (std::max)( shadow->slopeBias, 0.0035f ) : shadow->slopeBias )
                                : 0.0f;
    shader.SetMat4( "uShadowViewProj", enabled ? shadow->lightViewProjection : identity );
    shader.SetVec4( "uShadowParams", enabled ? shadow->strength : 0.0f, depthBias, slopeBias, enabled ? shadow->texelSize * shadow->softness : 0.0f );
    shader.SetVec4( "uShadowFlags", enabled ? 1.0f : 0.0f, receive ? 1.0f : 0.0f, enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f, enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f );
    shader.SetVec4( "uShadowLightDir", enabled ? shadow->lightDirectionWorld.x : 0.0f, enabled ? shadow->lightDirectionWorld.y : 1.0f, enabled ? shadow->lightDirectionWorld.z : 0.0f, 0.0f );
    shader.SetInt( "uShadowMap", SHADOW_TEXTURE_SLOT );
    if ( enabled )
    {
        // Bind only when enabled. Disabled shaders still receive the sampler
        // uniform index, but leaving the previous texture bound is harmless
        // because uShadowFlags.x tells the shader not to sample it.
        Gfx().BindTexture( shadow->depthTextureHandle, SHADOW_TEXTURE_SLOT );
    }
}
} // namespace Rendering
} // namespace SkullbonezCore
