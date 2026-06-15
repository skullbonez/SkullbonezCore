/*
File: SkullbonezData/shaders/post_volumetric_light.hlsl
Purpose:
  Runs the post_volumetric_light HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
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
    float4 uCloudParams;       // coverage, softness, scale, intensity
};

Texture2D    uSceneTex : register(t0);
Texture2D    uDepthTex : register(t1);
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

float Hash21(float2 p)
{
    // Cheap repeatable pseudo-random value from a 2D point. Used to build soft
    // cloud breakup without needing a cloud texture.
    p = frac(p * float2(127.1f, 311.7f));
    p += dot(p, p + 74.7f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    // Smooth value noise. Neighboring positions get similar values, so the cloud
    // gaps feel cloudy instead of speckled.
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float CloudBreakup(float2 screenUV)
{
    // Adds smaller holes and unevenness to the ray mask so beams do not look like
    // perfectly smooth computer cones.
    float2 p = screenUV * float2(5.2f, 2.2f) + float2(0.17f, 1.31f);
    float v = ValueNoise(p) * 0.55f;
    v += ValueNoise(p * 2.03f + 4.0f) * 0.30f;
    v += ValueNoise(p * 4.11f + 9.0f) * 0.15f;
    return smoothstep(0.28f, 0.86f, v);
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
    // Hand-placed cloud forms that line up with the cinematic sky composition.
    // These shapes block parts of the rays like the reference image clouds.
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

float CloudLayerMask(float2 screenUV)
{
    // Combines broad noise, fine detail, erosion, and hand-placed cloud shapes
    // into a single 0..1 "cloud is here" mask.
    float2 lowerUV = float2(screenUV.x * 1.28f + 0.12f, screenUV.y * 2.55f + 0.18f) * max(uCloudParams.z, 0.001f);
    lowerUV.x += sin(screenUV.y * 5.0f) * 0.07f;
    float broad = ValueNoise(lowerUV) * 0.50f;
    broad += ValueNoise(lowerUV * 2.07f + 3.4f) * 0.25f;
    broad += ValueNoise(lowerUV * 4.28f + 8.1f) * 0.125f;
    float detail = ValueNoise(lowerUV * 2.45f + float2(6.8f, 1.7f)) * 0.50f;
    detail += ValueNoise(lowerUV * 5.04f + float2(9.4f, 4.2f)) * 0.25f;
    float erosion = ValueNoise(lowerUV * 4.20f + float2(11.4f, 5.7f)) * 0.55f;
    erosion += ValueNoise(lowerUV * 8.38f + float2(12.9f, 7.1f)) * 0.25f;
    float cloudShape = broad * 0.80f + detail * 0.20f - (erosion - 0.42f) * 0.12f;

    float threshold = lerp(0.76f, 0.34f, saturate(uCloudParams.x));
    float lowerMask = smoothstep(threshold, threshold + max(uCloudParams.y * 1.55f, 0.001f), cloudShape);
    lowerMask *= smoothstep(0.18f, 0.82f, 1.0f - erosion * 0.34f);
    float lowerBand = smoothstep(0.34f, 0.50f, screenUV.y) * (1.0f - smoothstep(0.76f, 0.92f, screenUV.y));
    return saturate(max(lowerMask * lowerBand * 0.0f, HeroCloudMask(screenUV) * 1.0f) * clamp(uCloudParams.w, 0.0f, 1.5f));
}

float CloudRayOpen(float2 screenUV)
{
    // Converts the cloud mask into "how open is this part of the sky?" 1 means
    // open sky, 0 means cloud is blocking most of the light.
    float cloud = CloudLayerMask(screenUV);
    float breakup = CloudBreakup(screenUV + uSunShaftParams.xy * 0.17f);
    return saturate(0.18f + (1.0f - cloud) * 0.62f + breakup * 0.20f);
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
    float rawDepth = uDepthTex.Sample(sSampler1, uv).r;
    float3 sceneColor = uSceneTex.Sample(sSampler1, ClampScreenUV(uv)).rgb;
    float skyMask = rawDepth >= 0.9999f ? 1.0f : 0.0f;
    float linearDepth = LinearizeDepth(rawDepth);
    float distantGeometry = smoothstep(90.0f, 1250.0f, linearDepth) * (1.0f - skyMask);
    float brightness = max(max(sceneColor.r, sceneColor.g), sceneColor.b);
    float brightPath = smoothstep(0.25f, 2.2f, brightness);
    float2 screenUV = float2(uv.x, 1.0f - uv.y);
    float cloudOpen = CloudRayOpen(screenUV);
    return (skyMask * (0.38f + brightPath * 0.62f) + distantGeometry * 0.22f) * cloudOpen;
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
    float rawDepth = uDepthTex.Sample(sSampler1, input.texCoord).r;
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
