/*
File: SkullbonezData/shaders/post_tonemap.hlsl
Purpose:
  Runs the post_tonemap HLSL shader program used by the renderer.

Mental model:
  post_tonemap.hlsl is shader source for the renderer's post_tonemap pass.
  Keep edits anchored on shader inputs, bindings, and render-output contracts
  and on the glossary/invariants below.

Glossary:
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma pack_matrix(column_major)

// =============================================================================
// CINEMATIC TONEMAP / FINAL COMPOSITE SHADER (DirectX)
// =============================================================================
//
// This is the canonical DX12 tonemap pass. It runs after the world has already
// been rendered into an HDR scene texture. Its job is to turn that bright
// off-screen image into the final window image by layering fog, the completed
// half-resolution volumetric-light texture, bloom, exposure, gamma, and a
// subtle vignette.
//
// The vertex shader draws a full-screen rectangle. The pixel shader then runs
// once per window pixel and samples the scene/depth/volumetric textures.
// =============================================================================

cbuffer Uniforms : register(b0)
{
    float uExposure;
    float uGamma;
    float uVolumetricCompositeStrength;
    float _padding0;
    float4 uDepthParams; // near, far, unused, unused
    float4 uFogParams;   // start, end, density, max opacity
    float3 uFogColor;
    float _padding1;
    float4 uBloomParams; // threshold, knee, strength, radius
    float4 uStyleGrade;  // saturation, contrast, vignette floor, sky mode
};

Texture2D    uSceneTex : register(t0);
Texture2D    uDepthTex : register(t1);
Texture2D    uVolumetricTex : register(t2);
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
    // Positions are already in clip space (-1..1), so this vertex shader only
    // forwards them to the rasterizer and flips the UV vertically for DirectX.
    output.position = float4(input.position, 0.0f, 1.0f);
    output.texCoord = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    return output;
}

float3 TonemapACES(float3 color)
{
    // ACES is a film-style curve. It keeps bright highlights warm and punchy
    // without simply clipping them to flat white.
    return saturate((color * (2.51f * color + 0.03f)) /
                    (color * (2.43f * color + 0.59f) + 0.14f));
}

float LinearizeDepth(float depth)
{
    // Hardware depth is stored non-linearly for precision. This converts it
    // back into an approximate camera distance so fog can fade by scene depth.
    float nearPlane = max(uDepthParams.x, 0.0001f);
    float farPlane = max(uDepthParams.y, nearPlane + 0.0001f);
    return (nearPlane * farPlane) / (farPlane - depth * (farPlane - nearPlane));
}

float3 PrefilterBloom(float3 color)
{
    // Bloom should only come from bright pixels. The threshold chooses what is
    // bright enough, and the knee softens the cutoff so bloom fades in smoothly.
    float brightness = max(max(color.r, color.g), color.b);
    float threshold = max(uBloomParams.x, 0.0f);
    float knee = max(uBloomParams.y, 0.0001f);
    float soft = saturate((brightness - threshold + knee) / (2.0f * knee));
    soft = soft * soft * knee;
    float contribution = max(brightness - threshold, soft) / max(brightness, 0.0001f);
    return color * contribution;
}

float2 ClampScreenUV(float2 uv)
{
    return saturate(uv);
}

float3 SampleScene(float2 uv)
{
    return uSceneTex.Sample(sSampler1, ClampScreenUV(uv)).rgb;
}

float3 SampleBloom(float2 uv)
{
    if (uBloomParams.z <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    // This is a tiny blur kernel. We sample neighboring pixels around the
    // current pixel, keep only their bright parts, and add them as glow.
    uint width;
    uint height;
    uSceneTex.GetDimensions(width, height);
    float2 texel = 1.0f / float2(max(width, 1u), max(height, 1u));
    float radius = max(uBloomParams.w, 0.25f);
    float3 bloom = PrefilterBloom(SampleScene(uv)) * 0.20f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( radius,  0.0f))) * 0.10f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(-radius,  0.0f))) * 0.10f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( 0.0f,  radius))) * 0.10f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( 0.0f, -radius))) * 0.10f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( radius,  radius))) * 0.07f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(-radius,  radius))) * 0.07f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( radius, -radius))) * 0.07f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(-radius, -radius))) * 0.07f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2( radius * 2.5f, 0.0f))) * 0.04f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(-radius * 2.5f, 0.0f))) * 0.04f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(0.0f,  radius * 2.5f))) * 0.04f;
    bloom += PrefilterBloom(SampleScene(uv + texel * float2(0.0f, -radius * 2.5f))) * 0.04f;
    return bloom * uBloomParams.z;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Start with the fully rendered HDR world color and its depth.
    float rawDepth = uDepthTex.Sample(sSampler1, input.texCoord).r;
    float3 hdrColor = SampleScene(input.texCoord);

    // Depth fog: terrain and objects fade into warm air with distance. Sky pixels
    // are excluded so the fog does not wash out the procedural sky background.
    float linearDepth = LinearizeDepth(rawDepth);
    float fogRange = max(uFogParams.y - uFogParams.x, 0.0001f);
    float rangeFog = saturate((linearDepth - uFogParams.x) / fogRange);
    float densityFog = 1.0f - exp(-max(linearDepth, 0.0f) * max(uFogParams.z, 0.0f));
    float geometryMask = rawDepth < 0.9999f ? 1.0f : 0.0f;
    float fogAmount = min(max(uFogParams.w, 0.0f), rangeFog * densityFog) * geometryMask;
    hdrColor = lerp(hdrColor, uFogColor, fogAmount);

    float2 screenUV = float2(input.texCoord.x, 1.0f - input.texCoord.y);
    // Extra low haze near the horizon sells the basin scale and backlit dust.
    float horizonBand = exp(-abs(screenUV.y - 0.52f) * 7.0f);
    float basinHaze = horizonBand * rangeFog * geometryMask * max(uFogParams.w, 0.0f) * 0.18f;
    hdrColor = lerp(hdrColor, uFogColor * 1.08f, basinHaze);

    // The sole screen-space sun march runs in the half-resolution volumetric
    // pass. Tonemap only composites that completed result; it must not grow a
    // second full-resolution shaft path.
    hdrColor += uVolumetricTex.Sample(sSampler1, ClampScreenUV(input.texCoord)).rgb * max(uVolumetricCompositeStrength, 0.0f);
    hdrColor += SampleBloom(input.texCoord);

    // Convert HDR to monitor color, then apply display gamma. The small contrast,
    // saturation, and vignette pushes are deliberately done last.
    float3 mapped = TonemapACES(hdrColor * max(uExposure, 0.0f));
    float safeGamma = max(uGamma, 0.001f);
    mapped = pow(mapped, 1.0f / safeGamma);
    float luminance = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    mapped = lerp(float3(luminance, luminance, luminance), mapped, max(uStyleGrade.x, 0.0f));
    mapped = saturate((mapped - 0.5f) * max(uStyleGrade.y, 0.0f) + 0.5f);
    float vignette = 1.0f - smoothstep(0.28f, 0.86f, distance(screenUV, float2(0.52f, 0.48f)));
    mapped *= lerp(saturate(uStyleGrade.z), 1.0f, vignette);
    int styleMode = (int)floor(uStyleGrade.w + 0.5f);
    if (styleMode == 11)
    {
        float3 pastel = pow(mapped, float3(0.94f, 0.94f, 0.94f)) * float3(1.00f, 1.03f, 0.99f) + float3(0.006f, 0.012f, 0.018f);
        float3 poster = floor(saturate(pastel) * 18.0f + 0.5f) / 18.0f;
        mapped = lerp(mapped, poster, 0.26f);
    }
    return float4(mapped, 1.0f);
}
