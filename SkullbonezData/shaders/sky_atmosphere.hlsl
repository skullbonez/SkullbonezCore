#pragma pack_matrix(column_major)

// =============================================================================
// CINEMATIC SKY ATMOSPHERE SHADER (DirectX)
// =============================================================================
//
// This is the DirectX version of sky_atmosphere.frag. It draws the sunset sky as
// a procedural full-screen background: horizon-to-zenith color, bright sun disk,
// warm glow, and hand-placed cloud banks.
// =============================================================================

cbuffer Uniforms : register(b0)
{
    float4 uSunParams;
    float3 uSunColor;
    float _padding0;
    float3 uHorizonColor;
    float _padding1;
    float3 uZenithColor;
    float _padding2;
    float4 uCloudParams; // coverage, softness, scale, intensity
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

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Full-screen sky pass: position is already in clip space, and UV tells the
    // pixel shader where this pixel sits on the screen.
    output.position = float4(input.position, 0.0f, 1.0f);
    output.texCoord = input.texCoord;
    return output;
}

float Hash21(float2 p)
{
    // Tiny deterministic random number generator. Same input gives same output,
    // which is important because clouds should not flicker frame to frame.
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    // Smooth random field. This is one ingredient for soft cloud interiors.
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
    // Fractal noise: stack several noise layers at different scales. This gives
    // clouds broad forms plus smaller wispy variation.
    float v = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        v += ValueNoise(p) * amp;
        p *= 2.07f;
        amp *= 0.5f;
    }
    return v;
}

float CloudLobe(float2 uv, float2 center, float2 radius, float seed)
{
    // One soft oval cloud puff. Several lobes are combined below to build the
    // large backlit cloud banks seen near the sun.
    float2 q = (uv - center) / radius;
    float broad = length(q * float2(0.88f, 1.12f));
    float cap = length((uv - center + float2(0.04f, -0.025f)) / (radius * float2(0.78f, 0.86f)));
    float ragged = (sin((uv.x + seed) * 31.0f) + sin((uv.y - seed) * 37.0f)) * 0.018f;
    float body = 1.0f - smoothstep(0.72f, 1.24f, broad + ragged);
    float crown = 1.0f - smoothstep(0.45f, 1.06f, cap + ragged * 0.45f);
    return saturate(body * 0.72f + crown * 0.28f);
}

float HeroCloudMask(float2 uv)
{
    // These are deliberate screen-space cloud placements, not random clouds.
    // That makes the first cinematic view read like the reference composition.
    float mask = 0.0f;
    mask = max(mask, CloudLobe(uv, float2(0.20f, 0.64f), float2(0.28f, 0.105f), 2.1f) * 0.84f);
    mask = max(mask, CloudLobe(uv, float2(0.40f, 0.68f), float2(0.22f, 0.090f), 5.7f) * 0.62f);
    mask = max(mask, CloudLobe(uv, float2(0.66f, 0.78f), float2(0.26f, 0.090f), 8.6f) * 0.48f);
    mask = max(mask, CloudLobe(uv, float2(0.84f, 0.64f), float2(0.30f, 0.110f), 12.3f) * 0.54f);
    mask = max(mask, CloudLobe(uv, float2(0.55f, 0.55f), float2(0.26f, 0.080f), 17.2f) * 0.36f);
    mask = max(mask, CloudLobe(uv, float2(0.10f, 0.55f), float2(0.24f, 0.080f), 21.4f) * 0.46f);
    mask = max(mask, CloudLobe(uv, float2(0.74f, 0.58f), float2(0.34f, 0.095f), 24.9f) * 0.42f);
    mask = max(mask, CloudLobe(uv, float2(0.96f, 0.54f), float2(0.22f, 0.080f), 28.5f) * 0.38f);
    return saturate(mask);
}

float CloudLayerMask(float2 uv, out float cloudShape)
{
    // Build a cloud mask. Coverage moves the threshold up/down, softness controls
    // edge width, and intensity is applied later when mixing cloud color.
    float2 lowerUV = float2(uv.x * 1.28f + 0.12f, uv.y * 2.55f + 0.18f) * max(uCloudParams.z, 0.001f);
    lowerUV.x += sin(uv.y * 5.0f) * 0.07f;
    float broad = CloudFBM(lowerUV);
    float detail = CloudFBM(lowerUV * 2.45f + float2(6.8f, 1.7f));
    float erosion = CloudFBM(lowerUV * 4.20f + float2(11.4f, 5.7f));
    cloudShape = broad * 0.80f + detail * 0.20f - (erosion - 0.42f) * 0.12f;

    float coverage = saturate(uCloudParams.x);
    float threshold = lerp(0.76f, 0.34f, coverage);
    float softness = max(uCloudParams.y * 1.55f, 0.001f);
    float lowerMask = smoothstep(threshold, threshold + softness, cloudShape);
    lowerMask *= smoothstep(0.18f, 0.82f, 1.0f - erosion * 0.34f);
    float lowerBand = smoothstep(0.34f, 0.50f, uv.y) * (1.0f - smoothstep(0.76f, 0.92f, uv.y));

    float hero = HeroCloudMask(uv);
    return saturate(max(lowerMask * lowerBand * 0.0f, hero * 1.0f));
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Vertical gradient: warm/orange near the horizon and darker higher up.
    float height = saturate(input.texCoord.y);
    float vertical = pow(height, 0.65f);
    float3 skyColor = lerp(uHorizonColor, uZenithColor, vertical);

    // The sun is made from a hard disk plus two exponential glows. The glow is
    // intentionally larger than the disk to create the blown-out sunset feel.
    float sunDistance = distance(input.texCoord, uSunParams.xy);
    float sunDisk = 1.0f - smoothstep(0.018f, 0.045f, sunDistance);
    float innerGlow = exp(-sunDistance * 18.0f);
    float outerGlow = exp(-sunDistance * 4.6f);
    float horizonScatter = exp(-abs(input.texCoord.y - uSunParams.y) * 3.4f) *
                           (1.0f - smoothstep(0.08f, 0.65f, sunDistance));

    float3 sun = uSunColor * (sunDisk * uSunParams.z +
                              innerGlow * uSunParams.w +
                              outerGlow * (uSunParams.w * 0.35f) +
                              horizonScatter * 0.8f);

    // Clouds are darker on their bodies but pick up strong orange light on edges
    // that face the sun. This is the "silver lining" effect, just warmer.
    float cloudShape = 0.0f;
    float cloudMask = CloudLayerMask(input.texCoord, cloudShape);
    float sunLit = exp(-sunDistance * 3.2f);
    float threshold = lerp(0.76f, 0.34f, saturate(uCloudParams.x));
    float cloudEdge = 1.0f - smoothstep(0.00f, 0.16f, abs(cloudShape - threshold));
    cloudEdge = max(cloudEdge, smoothstep(0.08f, 0.42f, cloudMask) * (1.0f - smoothstep(0.58f, 0.95f, cloudMask)));
    float sunEdge = cloudEdge * (0.35f + sunLit * 1.2f);
    float3 cloudShadow = float3(0.22f, 0.10f, 0.075f);
    float3 cloudMid = float3(0.68f, 0.27f, 0.10f);
    float3 cloudLight = float3(2.05f, 1.02f, 0.35f) * (0.68f + sunLit * 0.92f);
    float3 cloudColor = lerp(cloudShadow, cloudMid, 0.45f + sunLit * 0.22f);
    cloudColor = lerp(cloudColor, cloudLight, saturate(sunEdge));
    float cloudAmount = cloudMask * clamp(uCloudParams.w, 0.0f, 1.5f);
    skyColor = lerp(skyColor, cloudColor, saturate(cloudAmount * 0.42f));
    // Let clouds dim the sun where they overlap it so the disk can peek through
    // gaps instead of always drawing on top.
    float sunOcclusion = cloudAmount * smoothstep(0.42f, 0.04f, sunDistance);
    sun *= 1.0f - saturate(sunOcclusion * 0.72f + cloudAmount * 0.20f);

    return float4(skyColor + sun, 1.0f);
}
