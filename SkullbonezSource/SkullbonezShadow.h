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

    // Opaque backend texture handle for the framebuffer depth attachment. GL,
    // DX11, and DX12 all expose different native resource types, so render code
    // passes this neutral handle back to IRenderBackend::BindTexture.
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
