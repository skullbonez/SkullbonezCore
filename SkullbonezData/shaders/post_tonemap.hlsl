/*
File: SkullbonezData/shaders/post_tonemap.hlsl
Purpose:
  Runs the post_tonemap HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
  DX12 (DirectX 12): Production renderer API that owns this shader's root
  signature, input layout, and descriptor bindings.
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
// off-screen image into the final window image by layering fog, god rays,
// volumetric light, bloom, exposure, gamma, and a subtle vignette.
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
    float4 uSunShaftParams; // x/y screen position, strength, falloff
    float3 uSunColor;
    float _padding2;
    float4 uBloomParams; // threshold, knee, strength, radius
    float4 uCloudParams; // coverage, softness, scale, intensity
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

float Hash21(float2 p)
{
    // A cheap repeatable pseudo-random number from a 2D point. Shaders cannot
    // call rand(), so little hash functions like this are common building blocks.
    p = frac(p * float2(127.1f, 311.7f));
    p += dot(p, p + 74.7f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    // Smooth grid noise. We use it to make cloud openings and ray masks feel
    // organic instead of perfectly mathematical.
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float CloudLobe(float2 screenUV, float2 center, float2 radius, float seed)
{
    float2 q = (screenUV - center) / radius;
    float broad = length(q * float2(0.88f, 1.12f));
    float cap = length((screenUV - center + float2(0.04f, -0.025f)) / (radius * float2(0.78f, 0.86f)));
    float ragged = (sin((screenUV.x + seed) * 31.0f) + sin((screenUV.y - seed) * 37.0f)) * 0.018f;
    float body = 1.0f - smoothstep(0.72f, 1.24f, broad + ragged);
    float crown = 1.0f - smoothstep(0.45f, 1.06f, cap + ragged * 0.45f);
    return saturate(body * 0.72f + crown * 0.28f);
}

float HeroCloudMask(float2 screenUV)
{
    // Hand-placed cloud blobs that roughly match the reference composition.
    // They are screen-space shapes: stable, art-directed, and cheap to evaluate.
    float mask = 0.0f;
    mask = max(mask, CloudLobe(screenUV, float2(0.20f, 0.64f), float2(0.28f, 0.105f), 2.1f) * 0.84f);
    mask = max(mask, CloudLobe(screenUV, float2(0.40f, 0.68f), float2(0.22f, 0.090f), 5.7f) * 0.62f);
    mask = max(mask, CloudLobe(screenUV, float2(0.66f, 0.78f), float2(0.26f, 0.090f), 8.6f) * 0.48f);
    mask = max(mask, CloudLobe(screenUV, float2(0.84f, 0.64f), float2(0.30f, 0.110f), 12.3f) * 0.54f);
    mask = max(mask, CloudLobe(screenUV, float2(0.55f, 0.55f), float2(0.26f, 0.080f), 17.2f) * 0.36f);
    mask = max(mask, CloudLobe(screenUV, float2(0.10f, 0.55f), float2(0.24f, 0.080f), 21.4f) * 0.46f);
    mask = max(mask, CloudLobe(screenUV, float2(0.74f, 0.58f), float2(0.34f, 0.095f), 24.9f) * 0.42f);
    mask = max(mask, CloudLobe(screenUV, float2(0.96f, 0.54f), float2(0.22f, 0.080f), 28.5f) * 0.38f);
    return saturate(mask);
}

float CloudRayOpen(float2 screenUV)
{
    // Sky openness at this screen position. 1 means light can pass
    // through freely; lower values mean a cloud is blocking the ray.
    float2 lowerUV = float2(screenUV.x * 1.28f + 0.12f, screenUV.y * 2.55f + 0.18f) * max(uCloudParams.z, 0.001f);
    lowerUV.x += sin(screenUV.y * 5.0f) * 0.07f;
    float cloudShape = ValueNoise(lowerUV) * 0.50f;
    cloudShape += ValueNoise(lowerUV * 2.07f + 3.4f) * 0.25f;
    cloudShape += ValueNoise(lowerUV * 4.28f + 8.1f) * 0.125f;
    float erosion = ValueNoise(lowerUV * 4.20f + float2(11.4f, 5.7f)) * 0.55f;
    erosion += ValueNoise(lowerUV * 8.38f + float2(12.9f, 7.1f)) * 0.25f;
    cloudShape -= (erosion - 0.42f) * 0.12f;
    float threshold = lerp(0.76f, 0.34f, saturate(uCloudParams.x));
    float cloud = smoothstep(threshold, threshold + max(uCloudParams.y * 1.55f, 0.001f), cloudShape);
    cloud *= smoothstep(0.18f, 0.82f, 1.0f - erosion * 0.34f);
    cloud *= smoothstep(0.34f, 0.50f, screenUV.y) * (1.0f - smoothstep(0.76f, 0.92f, screenUV.y)) * 0.0f;
    cloud = max(cloud, HeroCloudMask(screenUV) * 1.0f);
    cloud = saturate(cloud * clamp(uCloudParams.w, 0.0f, 1.5f));
    return saturate(0.20f + (1.0f - cloud) * 0.80f);
}

float SampleSkyTransmittance(float2 uv)
{
    if (uv.x < 0.0f || uv.y < 0.0f || uv.x > 1.0f || uv.y > 1.0f)
    {
        return 0.0f;
    }

    // A depth value near 1 means "nothing was drawn here", so the pixel is sky.
    // Rays are strongest when the sample is sky and the scene color is bright.
    float sampleDepth = uDepthTex.Sample(sSampler1, uv).r;
    float3 sampleColor = SampleScene(uv);
    float skyMask = sampleDepth >= 0.9999f ? 1.0f : 0.0f;
    float brightness = max(max(sampleColor.r, sampleColor.g), sampleColor.b);
    float brightSky = smoothstep(0.28f, 1.8f, brightness);
    float2 screenUV = float2(uv.x, 1.0f - uv.y);
    return skyMask * (0.35f + brightSky * 0.65f) * CloudRayOpen(screenUV);
}

float RadialGodRays(float2 uv)
{
    // March from the current pixel toward the sun in screen space. If many of
    // those samples are bright sky, the pixel receives a visible ray.
    float2 sunUV = float2(uSunShaftParams.x, 1.0f - uSunShaftParams.y);
    if (sunUV.x < -0.15f || sunUV.y < -0.15f || sunUV.x > 1.15f || sunUV.y > 1.15f)
    {
        return 0.0f;
    }

    static const int sampleCount = 36;
    float2 delta = (sunUV - uv) / (float)sampleCount;
    float2 sampleUV = uv;
    float illuminationDecay = 1.0f;
    float accum = 0.0f;
    [loop]
    for (int i = 0; i < sampleCount; ++i)
    {
        sampleUV += delta;
        float transmittance = SampleSkyTransmittance(sampleUV);
        accum += transmittance * illuminationDecay;
        illuminationDecay *= 0.95f;
    }

    float sunDistance = length(uv - sunUV);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001f));
    float2 screenUV = float2(uv.x, 1.0f - uv.y);
    float belowSun = smoothstep(0.0f, 0.45f, uSunShaftParams.y - screenUV.y);
    return accum / (float)sampleCount * radialFalloff * belowSun;
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

    // Screen-space god rays. The radial samples create broken shafts, while the
    // vertical column adds a stronger beam under the sun like the reference.
    float rayMask = RadialGodRays(input.texCoord);
    float2 toSun = uSunShaftParams.xy - screenUV;
    float sunDistance = length(toSun);
    float belowSun = smoothstep(0.0f, 0.35f, uSunShaftParams.y - screenUV.y);
    float verticalColumn = pow(max(0.0f, 1.0f - abs(screenUV.x - uSunShaftParams.x) * 4.0f), 2.0f);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001f));
    float occlusionSoftening = lerp(1.0f, 0.35f, geometryMask);
    float shaftAmount = radialFalloff * belowSun * (0.30f + verticalColumn * 0.70f) * uSunShaftParams.z * 0.20f * occlusionSoftening;
    shaftAmount += rayMask * uSunShaftParams.z * 0.42f * (0.85f - geometryMask * 0.35f);
    hdrColor += uSunColor * shaftAmount;
    // Add the cheaper half-resolution volumetric texture. It was generated in a
    // separate pass so we can keep this final pass simpler and faster.
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
