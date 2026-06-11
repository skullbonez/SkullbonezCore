#pragma once

#include "SkullbonezIRenderBackend.h"
#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Rendering
{
inline constexpr int SHADOW_TEXTURE_SLOT = 3;

struct ShadowFrameData
{
    Math::Transformation::Matrix4 lightView;
    Math::Transformation::Matrix4 lightProjection;
    Math::Transformation::Matrix4 lightViewProjection;
    Math::Vector::Vector3 lightDirectionWorld;
    uint32_t depthTextureHandle = 0;
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

inline void ApplyShadowReceiverUniforms( IShader& shader, const ShadowFrameData* shadow, bool receive )
{
    const bool enabled = shadow && shadow->valid && receive && shadow->depthTextureHandle != 0;
    Math::Transformation::Matrix4 identity;
    shader.SetMat4( "uShadowViewProj", enabled ? shadow->lightViewProjection : identity );
    shader.SetVec4( "uShadowParams", enabled ? shadow->strength : 0.0f, enabled ? shadow->depthBias : 0.0f, enabled ? shadow->slopeBias : 0.0f, enabled ? shadow->texelSize * shadow->softness : 0.0f );
    shader.SetVec4( "uShadowFlags", enabled ? 1.0f : 0.0f, receive ? 1.0f : 0.0f, enabled ? static_cast<float>( shadow->pcfRadius ) : 0.0f, enabled && shadow->zeroToOneDepth ? 1.0f : 0.0f );
    shader.SetVec4( "uShadowLightDir", enabled ? shadow->lightDirectionWorld.x : 0.0f, enabled ? shadow->lightDirectionWorld.y : 1.0f, enabled ? shadow->lightDirectionWorld.z : 0.0f, 0.0f );
    shader.SetInt( "uShadowMap", SHADOW_TEXTURE_SLOT );
    if ( enabled )
    {
        Gfx().BindTexture( shadow->depthTextureHandle, SHADOW_TEXTURE_SLOT );
    }
}
} // namespace Rendering
} // namespace SkullbonezCore
