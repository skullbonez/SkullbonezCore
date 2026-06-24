/*
File: SkullbonezData/shaders/sky_atmosphere.hlsl
Purpose:
  Runs the sky_atmosphere HLSL shader program used by the renderer.

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
// WORLD-SPACE CINEMATIC SKY (DirectX)
// =============================================================================
//
// The sky is a procedural, world-stable background. Every pixel is converted
// into a world-space view ray, then shaded from a normalized sun direction,
// low-poly horizon/zenith palette, procedural clouds, and distant stylized
// ridges. No external sky textures are required.
// =============================================================================

cbuffer Uniforms : register(b0)
{
    float4 uSunParams;    // azimuth 0..1, elevation 0..1, intensity, glow
    float3 uSunColor;
    float _padding0;
    float3 uHorizonColor;
    float _padding1;
    float3 uZenithColor;
    float _padding2;
    float4 uCloudParams; // coverage, softness, scale, intensity
    float4x4 uInvView;
    float4x4 uInvProjection;
    int    uSkyMode;
    float3 _padding3;
};

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

static const float PI = 3.14159265359f;
static const float TWO_PI = 6.28318530718f;

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float CloudFBM(float2 p)
{
    float v = 0.0f;
    float amp = 0.52f;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        v += ValueNoise(p) * amp;
        p = p * 2.03f + float2(3.17f, 1.73f);
        amp *= 0.52f;
    }
    return v;
}

float3 SkyWorldDirection(float2 screenUv)
{
    float2 ndc = screenUv * 2.0f - 1.0f;
    float4 viewPos = mul(uInvProjection, float4(ndc, 1.0f, 1.0f));
    viewPos.xyz /= max(abs(viewPos.w), 0.0001f);
    float4 worldDir = mul(uInvView, float4(normalize(viewPos.xyz), 0.0f));
    return normalize(worldDir.xyz);
}

float3 SunDirection()
{
    float azimuth = frac(uSunParams.x) * TWO_PI;
    float elevation = lerp(-0.08f, 1.05f, saturate(uSunParams.y));
    float cosElevation = cos(elevation);
    return normalize(float3(sin(azimuth) * cosElevation, sin(elevation), cos(azimuth) * cosElevation));
}

float2 DirectionCoord(float3 dir)
{
    float longitude = frac(atan2(dir.x, dir.z) / TWO_PI + 0.50f);
    float height = saturate(dir.y * 0.64f + 0.50f);
    return float2(longitude, height);
}

float LowPolyRidgeHeight(float x, float baseY, float amplitude, float frequency, float phase)
{
    float primary = 1.0f - abs(frac(x * frequency + phase) * 2.0f - 1.0f);
    float secondary = 1.0f - abs(frac(x * frequency * 1.61f + phase * 1.87f + 0.19f) * 2.0f - 1.0f);
    float broad = 1.0f - abs(frac(x * frequency * 0.43f + phase * 0.71f + 0.37f) * 2.0f - 1.0f);
    return baseY + amplitude * (primary * 0.58f + secondary * 0.28f + broad * 0.20f);
}

float RidgeMask(float2 coord, float baseY, float amplitude, float frequency, float phase)
{
    float ridge = LowPolyRidgeHeight(coord.x, baseY, amplitude, frequency, phase);
    float mask = 1.0f - smoothstep(ridge - 0.010f, ridge + 0.030f, coord.y);
    mask *= smoothstep(ridge - 0.20f, ridge - 0.050f, coord.y);
    mask *= smoothstep(0.08f, 0.25f, coord.y) * (1.0f - smoothstep(0.52f, 0.70f, coord.y));
    return floor(saturate(mask) * 4.0f + 0.5f) / 4.0f;
}

float CloudMask(float2 coord, float3 dir, out float cloudShape)
{
    float scale = max(uCloudParams.z, 0.05f);
    float2 p = float2(coord.x * 2.0f, coord.y * 1.42f);
    p.x += dir.x * 0.15f + sin(coord.y * 5.4f) * 0.055f;
    p *= scale;

    float broad = CloudFBM(p * float2(0.52f, 0.82f) + float2(0.1f, 2.7f));
    float detail = CloudFBM(p * float2(1.24f, 1.78f) + float2(7.4f, 1.9f));
    float erosion = CloudFBM(p * float2(2.58f, 2.12f) + float2(12.3f, 6.1f));
    cloudShape = broad * 0.72f + detail * 0.24f - (erosion - 0.42f) * 0.18f;

    float threshold = lerp(0.78f, 0.34f, saturate(uCloudParams.x));
    float softness = max(uCloudParams.y, 0.001f) * 1.45f;
    float mask = smoothstep(threshold, threshold + softness, cloudShape);
    float band = smoothstep(0.30f, 0.46f, coord.y) * (1.0f - smoothstep(0.78f, 0.94f, coord.y));
    float streakBand = smoothstep(0.46f, 0.60f, coord.y) * (1.0f - smoothstep(0.88f, 0.98f, coord.y));
    float streak = 1.0f - smoothstep(0.035f, 0.16f, abs(frac(coord.x * 7.0f + coord.y * 1.65f) - 0.5f));
    streak *= streakBand * 0.34f;
    mask = saturate(max(mask * band, streak));

    return floor(mask * 5.0f + 0.5f) / 5.0f;
}

float3 ApplyLowPolyGrade(float3 color, float posterizeAmount)
{
    float3 poster = floor(saturate(color) * 18.0f + 0.5f) / 18.0f;
    return lerp(color, poster, posterizeAmount);
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float3 dir = SkyWorldDirection(input.texCoord);
    float3 sunDir = SunDirection();
    float2 coord = DirectionCoord(dir);
    int styleMode = uSkyMode;

    float height = saturate((dir.y + 0.05f) / 1.05f);
    float bandedHeight = floor(height * 11.0f + 0.5f) / 11.0f;
    float vertical = lerp(pow(height, 0.72f), pow(bandedHeight, 0.78f), styleMode == 11 ? 0.42f : 0.24f);

    float3 horizon = clamp(uHorizonColor, 0.0f, 2.2f);
    float3 zenith = clamp(uZenithColor, 0.0f, 2.2f);
    float3 midSky = clamp(lerp(horizon, zenith, 0.62f) + float3(-0.02f, 0.04f, 0.10f), 0.0f, 2.2f);
    float3 skyColor = lerp(horizon, midSky, smoothstep(0.05f, 0.48f, vertical));
    skyColor = lerp(skyColor, zenith, smoothstep(0.34f, 0.88f, vertical));

    float horizonWarmth = exp(-abs(dir.y) * 4.2f);
    skyColor += horizon * horizonWarmth * 0.085f;

    float sunDot = dot(dir, sunDir);
    float sunForward = saturate(sunDot);
    float sunDisk = smoothstep(0.99940f, 0.99992f, sunDot);
    float innerGlow = pow(sunForward, 96.0f);
    float outerGlow = pow(sunForward, 10.0f);
    float lowHaze = exp(-abs(dir.y - sunDir.y) * 3.2f) * pow(sunForward, 1.55f);
    float3 sunColor = clamp(uSunColor, 0.0f, 3.2f);
    float3 sun = sunColor * (sunDisk * uSunParams.z + innerGlow * uSunParams.w * 0.86f +
                             outerGlow * uSunParams.w * 0.24f + lowHaze * 0.36f);

    float farRidge = RidgeMask(coord, 0.34f, 0.10f, 3.2f, 0.10f);
    float midRidge = RidgeMask(coord, 0.30f, 0.105f, 4.6f, 0.36f);
    float nearRidge = RidgeMask(coord, 0.26f, 0.092f, 6.1f, 0.68f);
    float3 farColor = clamp(lerp(zenith, horizon, 0.36f) * float3(0.42f, 0.55f, 0.68f), 0.0f, 1.8f);
    float3 midColor = clamp(lerp(zenith, horizon, 0.46f) * float3(0.38f, 0.48f, 0.58f), 0.0f, 1.8f);
    float3 nearColor = clamp(lerp(zenith, horizon, 0.62f) * float3(0.28f, 0.38f, 0.40f), 0.0f, 1.6f);
    skyColor = lerp(skyColor, farColor, farRidge * 0.58f);
    skyColor = lerp(skyColor, midColor, midRidge * 0.68f);
    skyColor = lerp(skyColor, nearColor, nearRidge * 0.78f);

    float cloudShape = 0.0f;
    float cloudMask = CloudMask(coord, dir, cloudShape) * clamp(uCloudParams.w, 0.0f, 1.5f);
    float sunLit = pow(saturate(dot(normalize(float3(dir.x, max(dir.y, 0.0f), dir.z)), sunDir)), 3.2f);
    float3 cloudShadow = clamp(lerp(horizon, zenith, 0.20f) * float3(0.54f, 0.40f, 0.46f), 0.0f, 1.8f);
    float3 cloudMid = clamp(lerp(horizon, sunColor, 0.24f), 0.0f, 2.2f);
    float3 cloudLight = clamp(lerp(horizon, sunColor, 0.62f) * (0.80f + sunLit * 0.82f), 0.0f, 2.6f);
    float edge = 1.0f - smoothstep(0.06f, 0.26f, abs(cloudShape - lerp(0.78f, 0.34f, saturate(uCloudParams.x))));
    float3 cloudColor = lerp(cloudShadow, cloudMid, 0.42f + sunLit * 0.24f);
    cloudColor = lerp(cloudColor, cloudLight, saturate(edge * 0.72f + sunLit * 0.28f));
    skyColor = lerp(skyColor, cloudColor, saturate(cloudMask * 0.52f));

    float sunOcclusion = cloudMask * smoothstep(0.9972f, 0.9999f, sunDot);
    sun *= 1.0f - saturate(sunOcclusion * 0.70f);

    float3 finalSky = skyColor + sun;
    finalSky = ApplyLowPolyGrade(finalSky, styleMode == 11 ? 0.32f : 0.18f);
    return float4(finalSky, 1.0f);
}
