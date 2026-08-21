/*
File: SkullbonezData/shaders/post_volumetric_light.hlsl
Purpose:
  Render screen-space sun god rays and atmospheric light scattering.

Summary:
  Performs radial blur from the projected sun position across the occlusion
  mask, accumulating light shafts that are composited additively onto scene color.

Invariants:
  - Sun position is projected to screen coordinates; off-screen sun fades smoothly.
  - Sampling density and decay parameters match authored cinematic sky settings.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/World/SkyBox.cpp
*/

#pragma pack_matrix(column_major)

// =============================================================================
// CINEMATIC VOLUMETRIC LIGHT SHADER (DirectX)
// =============================================================================
//
// This is the canonical DX12 volumetric-light pass. It builds a soft
// half-resolution light texture by marching from each pixel toward the sun and
// measuring how much bright, unblocked sky is along that path. The final tonemap
// pass composites this warm texture over the scene.
// =============================================================================

cbuffer Uniforms : register(b0)
{
    float4 uDepthParams;       // near, far, unused, unused
    float4 uSunShaftParams;    // x/y screen position, strength, falloff
    float3 uSunColor;
    float _padding0;
    float4 uVolumetricParams;  // strength, density, decay, fog density
};

// Invariant: b1 carries stable indices into the directly indexed shader-visible
// heap; the pipeline owner writes all six root constants before every draw.
cbuffer BindlessTextureIndices : register(b1)
{
    uint4 _textureDescriptorIndices0;
    uint2 _textureDescriptorIndices1;
};

uint BindlessTextureIndex(uint slot)
{
    return slot < 4u ? _textureDescriptorIndices0[slot] : _textureDescriptorIndices1[slot - 4u];
}

SamplerState sSampler0 : register(s0);
SamplerState sSampler1 : register(s1);

struct VS_IN
{
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Full-screen post pass: position is already in clip space. The UV is flipped
    // for DirectX texture coordinates.
    output.position = float4(input.position, 0.0f, 1.0f);
    output.texCoord = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    return output;
}

float LinearizeDepth(float depth)
{
    // Convert depth-buffer values back into approximate scene distance so far
    // hills and near objects can influence the amount of haze differently.
    float nearPlane = max(uDepthParams.x, 0.0001f);
    float farPlane = max(uDepthParams.y, nearPlane + 0.0001f);
    return (nearPlane * farPlane) / (farPlane - depth * (farPlane - nearPlane));
}

float2 ClampScreenUV(float2 uv)
{
    return saturate(uv);
}

float SampleLightTransmittance(float2 uv)
{
    if (uv.x < 0.0f || uv.y < 0.0f || uv.x > 1.0f || uv.y > 1.0f)
    {
        return 0.0f;
    }

    // Sky pixels have depth near 1. Solid pixels can still contribute a little
    // if they are distant and bright, which makes far haze glow near the horizon.
    // Ownership: cloud shape is resolved by the world-space sky pass. A
    // camera-locked mask in this post pass would make cloud occlusion slide.
    Texture2D<float4> sceneTexture = ResourceDescriptorHeap[BindlessTextureIndex(0u)];
    Texture2D<float4> depthTexture = ResourceDescriptorHeap[BindlessTextureIndex(1u)];
    float rawDepth = depthTexture.Sample(sSampler1, uv).r;
    float3 sceneColor = sceneTexture.Sample(sSampler1, ClampScreenUV(uv)).rgb;
    float skyMask = rawDepth >= 0.9999f ? 1.0f : 0.0f;
    float linearDepth = LinearizeDepth(rawDepth);
    float distantGeometry = smoothstep(90.0f, 1250.0f, linearDepth) * (1.0f - skyMask);
    float brightness = max(max(sceneColor.r, sceneColor.g), sceneColor.b);
    float brightPath = smoothstep(0.25f, 2.2f, brightness);
    return skyMask * (0.38f + brightPath * 0.62f) + distantGeometry * 0.22f;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float2 sunUV = float2(uSunShaftParams.x, 1.0f - uSunShaftParams.y);
    if (sunUV.x < -0.15f || sunUV.y < -0.15f || sunUV.x > 1.15f || sunUV.y > 1.15f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // The receiver term fades shafts differently over sky and solid geometry.
    // Geometry gets a fog-based amount so the light feels suspended in air.
    Texture2D<float4> depthTexture = ResourceDescriptorHeap[BindlessTextureIndex(1u)];
    float rawDepth = depthTexture.Sample(sSampler1, input.texCoord).r;
    float linearDepth = LinearizeDepth(rawDepth);
    float geometryMask = rawDepth < 0.9999f ? 1.0f : 0.0f;
    float distanceFog = 1.0f - exp(-max(linearDepth, 0.0f) * max(uVolumetricParams.w, 0.0f) * 0.70f);
    float receiver = lerp(1.0f, clamp(distanceFog * 1.4f, 0.20f, 0.72f), geometryMask);

    // Step from this pixel toward the sun. Each step asks whether that point is
    // open sky or blocked by cloud/geometry. The accumulated answer becomes a
    // warm light shaft.
    static const int sampleCount = 48;
    float2 delta = (sunUV - input.texCoord) * max(uVolumetricParams.y, 0.05f) / (float)sampleCount;
    float2 sampleUV = input.texCoord;
    float decay = clamp(uVolumetricParams.z, 0.80f, 0.995f);
    float illuminationDecay = 1.0f;
    float accum = 0.0f;
    [loop]
    for (int i = 0; i < sampleCount; ++i)
    {
        sampleUV += delta;
        accum += SampleLightTransmittance(sampleUV) * illuminationDecay;
        illuminationDecay *= decay;
    }

    // Shape the shaft so it is strongest below/near the sun and fades outward.
    float sunDistance = length(input.texCoord - sunUV);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001f));
    float2 screenUV = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    float belowSun = smoothstep(0.0f, 0.44f, uSunShaftParams.y - screenUV.y);
    float verticalColumn = pow(max(0.0f, 1.0f - abs(screenUV.x - uSunShaftParams.x) * 3.8f), 2.0f);
    float shaft = accum / (float)sampleCount;
    shaft *= radialFalloff * belowSun * (0.55f + verticalColumn * 0.45f) * receiver;
    shaft *= max(uVolumetricParams.x, 0.0f) * max(uSunShaftParams.z, 0.0f);

    return float4(uSunColor * shaft, 1.0f);
}
