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

float LowPolyCloudStreak(float2 uv, float2 center, float2 size, float skew)
{
    float2 q = uv - center;
    q.x += q.y * skew;
    float diamond = abs(q.x) / max(size.x, 0.001f) + abs(q.y) / max(size.y, 0.001f);
    float mask = 1.0f - smoothstep(0.76f, 1.02f, diamond);
    return floor(saturate(mask) * 3.0f + 0.5f) / 3.0f;
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

float LowPolyRidgeHeight(float x, float baseY, float amplitude, float frequency, float phase)
{
    float primary = 1.0f - abs(frac(x * frequency + phase) * 2.0f - 1.0f);
    float secondary = 1.0f - abs(frac(x * frequency * 1.73f + phase * 1.91f + 0.17f) * 2.0f - 1.0f);
    float broad = 1.0f - abs(frac(x * frequency * 0.48f + phase * 0.63f + 0.41f) * 2.0f - 1.0f);
    return baseY + amplitude * (primary * 0.62f + secondary * 0.28f + broad * 0.22f);
}

float3 SkyWorldDirection(float2 screenUv)
{
    float2 ndc = screenUv * 2.0f - 1.0f;
    float4 viewPos = mul(uInvProjection, float4(ndc, 1.0f, 1.0f));
    viewPos.xyz /= max(abs(viewPos.w), 0.0001f);
    float4 worldDir = mul(uInvView, float4(normalize(viewPos.xyz), 0.0f));
    return normalize(worldDir.xyz);
}

float2 SkyboxCoord(float2 screenUv)
{
    float3 dir = SkyWorldDirection(screenUv);
    float longitude = atan2(dir.x, dir.z);
    // Repeat the painted panorama over a half-turn so the authored cloud and
    // ridge shapes remain visible through a normal gameplay FOV.
    float u = frac(longitude / 3.14159265359f + 0.47f);
    float v = saturate(dir.y * 0.5f + 0.5f);
    return float2(u, v);
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
    int mode = uSkyMode;
    float2 skyCoord = mode == 11 ? SkyboxCoord(input.texCoord) : input.texCoord;

    // Vertical gradient: warm/orange near the horizon and darker higher up.
    float height = saturate(skyCoord.y);
    float vertical = pow(height, 0.65f);
    float3 skyColor = lerp(uHorizonColor, uZenithColor, vertical);

    // The sun is made from a hard disk plus two exponential glows. The glow is
    // intentionally larger than the disk to create the blown-out sunset feel.
    float sunDistance = distance(skyCoord, uSunParams.xy);
    float sunDisk = 1.0f - smoothstep(0.018f, 0.045f, sunDistance);
    float innerGlow = exp(-sunDistance * 18.0f);
    float outerGlow = exp(-sunDistance * 4.6f);
    float horizonScatter = exp(-abs(skyCoord.y - uSunParams.y) * 3.4f) *
                           (1.0f - smoothstep(0.08f, 0.65f, sunDistance));

    float3 sun = uSunColor * (sunDisk * uSunParams.z +
                              innerGlow * uSunParams.w +
                              outerGlow * (uSunParams.w * 0.35f) +
                              horizonScatter * 0.8f);

    // Clouds are darker on their bodies but pick up strong orange light on edges
    // that face the sun. This is the "silver lining" effect, just warmer.
    float cloudShape = 0.0f;
    float cloudMask = CloudLayerMask(skyCoord, cloudShape);
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

    float3 finalSky = skyColor + sun;
    if (mode == 1)
    {
        finalSky = lerp(float3(0.15f, 0.16f, 0.15f), float3(0.46f, 0.43f, 0.36f), pow(1.0f - height, 1.8f)) + cloudMask * float3(0.10f, 0.09f, 0.07f);
    }
    else if (mode == 2)
    {
        finalSky = lerp(float3(0.035f, 0.038f, 0.042f), float3(0.42f, 0.45f, 0.48f), pow(1.0f - height, 2.2f));
    }
    else if (mode == 3 || mode == 15)
    {
        float scan = pow(max(0.0f, 1.0f - abs(skyCoord.y - 0.18f) * 8.0f), 2.0f);
        finalSky = float3(0.004f, 0.008f, 0.018f) + float3(0.0f, 0.80f, 1.0f) * scan * 0.45f + float3(1.0f, 0.0f, 0.75f) * pow(max(0.0f, 1.0f - abs(skyCoord.x - 0.75f) * 3.0f), 4.0f) * 0.20f;
    }
    else if (mode == 4)
    {
        float secondSun = exp(-distance(skyCoord, float2(0.74f, 0.68f)) * 8.0f);
        finalSky = lerp(float3(0.12f, 0.04f, 0.20f), float3(0.48f, 0.18f, 0.72f), 1.0f - height) + float3(0.20f, 1.20f, 0.72f) * secondSun;
    }
    else if (mode == 5)
    {
        finalSky = lerp(float3(0.18f, 0.13f, 0.08f), float3(0.76f, 0.48f, 0.20f), pow(1.0f - height, 1.1f)) + cloudMask * float3(0.20f, 0.12f, 0.04f);
    }
    else if (mode == 6)
    {
        finalSky = floor(finalSky * 5.0f) / 5.0f;
    }
    else if (mode == 11)
    {
        float3 horizon = clamp(lerp(float3(1.00f, 0.95f, 0.80f), uHorizonColor, 0.78f), 0.0f, 1.8f);
        float3 zenith = clamp(lerp(float3(0.55f, 0.75f, 1.00f), uZenithColor, 0.78f), 0.0f, 1.8f);
        float3 middle = clamp(lerp(horizon, zenith, 0.44f) + float3(0.05f, 0.04f, 0.02f), 0.0f, 1.8f);
        float3 lowPolySky = lerp(horizon, middle, smoothstep(0.08f, 0.55f, height));
        lowPolySky = lerp(lowPolySky, zenith, smoothstep(0.50f, 1.0f, height));
        float band = floor(height * 9.0f) / 9.0f;
        float3 bandedSky = lerp(horizon, middle, smoothstep(0.08f, 0.55f, band));
        bandedSky = lerp(bandedSky, zenith, smoothstep(0.50f, 1.0f, band));
        lowPolySky = lerp(lowPolySky, bandedSky, 0.18f);

        float farRidge = LowPolyRidgeHeight(skyCoord.x, 0.57f, 0.12f, 4.60f, 0.11f);
        float midRidge = LowPolyRidgeHeight(skyCoord.x, 0.52f, 0.11f, 6.10f, 0.37f);
        float nearRidge = LowPolyRidgeHeight(skyCoord.x, 0.48f, 0.09f, 7.80f, 0.68f);
        float ridgeFade = smoothstep(0.39f, 0.48f, height) * (1.0f - smoothstep(0.60f, 0.72f, height));
        float farMask = (1.0f - smoothstep(farRidge - 0.016f, farRidge + 0.024f, height)) * ridgeFade;
        float midMask = (1.0f - smoothstep(midRidge - 0.014f, midRidge + 0.022f, height)) * ridgeFade;
        float nearMask = (1.0f - smoothstep(nearRidge - 0.012f, nearRidge + 0.020f, height)) * ridgeFade;
        farMask *= smoothstep(farRidge - 0.18f, farRidge - 0.08f, height);
        midMask *= smoothstep(midRidge - 0.16f, midRidge - 0.07f, height);
        nearMask *= smoothstep(nearRidge - 0.14f, nearRidge - 0.06f, height);
        farMask = floor(farMask * 4.0f + 0.5f) / 4.0f;
        midMask = floor(midMask * 4.0f + 0.5f) / 4.0f;
        nearMask = floor(nearMask * 4.0f + 0.5f) / 4.0f;
        float3 farMountain = clamp(lerp(float3(0.34f, 0.39f, 0.58f), horizon, 0.10f), 0.0f, 1.4f);
        float3 midMountain = clamp(lerp(float3(0.43f, 0.31f, 0.40f), horizon, 0.12f), 0.0f, 1.4f);
        float3 nearMountain = clamp(lerp(float3(0.28f, 0.24f, 0.27f), uSunColor, 0.08f), 0.0f, 1.3f);
        lowPolySky = lerp(lowPolySky, farMountain, clamp(farMask * 0.72f, 0.0f, 0.72f));
        lowPolySky = lerp(lowPolySky, midMountain, clamp(midMask * 0.74f, 0.0f, 0.74f));
        lowPolySky = lerp(lowPolySky, nearMountain, clamp(nearMask * 0.66f, 0.0f, 0.66f));

        // Low-poly mode uses deliberate flat cloud cards instead of the broader
        // cinematic cloud bank. This keeps the sky clean and composed.
        float cardCloud = 0.0f;
        cardCloud = max(cardCloud, CloudLobe(skyCoord, float2(0.16f, 0.63f), float2(0.17f, 0.052f), 3.0f));
        cardCloud = max(cardCloud, CloudLobe(skyCoord, float2(0.36f, 0.68f), float2(0.22f, 0.055f), 6.0f) * 0.92f);
        cardCloud = max(cardCloud, CloudLobe(skyCoord, float2(0.56f, 0.57f), float2(0.20f, 0.050f), 7.4f) * 0.70f);
        cardCloud = max(cardCloud, CloudLobe(skyCoord, float2(0.76f, 0.66f), float2(0.23f, 0.055f), 9.0f) * 0.84f);
        cardCloud = max(cardCloud, CloudLobe(skyCoord, float2(0.91f, 0.53f), float2(0.16f, 0.045f), 12.0f) * 0.64f);
        cardCloud *= smoothstep(0.38f, 0.46f, height) * (1.0f - smoothstep(0.82f, 0.90f, height));
        float cloudBand = smoothstep(0.035f, 0.32f, cardCloud);
        cloudBand = floor(cloudBand * 3.0f + 0.5f) / 3.0f;
        float3 flatCloudShadow = clamp(lerp(float3(0.84f, 0.55f, 0.62f), uHorizonColor * float3(0.82f, 0.48f, 0.52f), 0.66f), 0.0f, 1.6f);
        float3 flatCloudLight = clamp(lerp(float3(1.38f, 0.86f, 0.52f), uSunColor * float3(1.42f, 0.98f, 0.68f), 0.62f), 0.0f, 1.8f);
        float3 flatCloud = lerp(flatCloudShadow, flatCloudLight, saturate(sunLit * 0.52f + 0.40f));
        lowPolySky = lerp(lowPolySky, flatCloud, clamp(cloudBand * 0.98f, 0.0f, 0.98f));

        float polyStreak = 0.0f;
        polyStreak = max(polyStreak, LowPolyCloudStreak(skyCoord, float2(0.20f, 0.58f), float2(0.24f, 0.030f), -1.00f));
        polyStreak = max(polyStreak, LowPolyCloudStreak(skyCoord, float2(0.42f, 0.63f), float2(0.30f, 0.034f), -0.80f) * 0.94f);
        polyStreak = max(polyStreak, LowPolyCloudStreak(skyCoord, float2(0.64f, 0.70f), float2(0.24f, 0.030f), -0.72f) * 0.84f);
        polyStreak = max(polyStreak, LowPolyCloudStreak(skyCoord, float2(0.80f, 0.56f), float2(0.22f, 0.028f), -1.10f) * 0.72f);
        polyStreak = max(polyStreak, LowPolyCloudStreak(skyCoord, float2(0.52f, 0.78f), float2(0.18f, 0.024f), -0.96f) * 0.58f);
        polyStreak *= smoothstep(0.46f, 0.52f, height) * (1.0f - smoothstep(0.82f, 0.92f, height));
        float3 streakWarm = clamp(lerp(float3(1.34f, 0.66f, 0.36f), uSunColor * float3(1.18f, 0.78f, 0.50f), 0.66f), 0.0f, 1.9f);
        float3 streakDust = clamp(lerp(float3(0.80f, 0.48f, 0.56f), uHorizonColor * float3(0.72f, 0.46f, 0.50f), 0.50f), 0.0f, 1.6f);
        float3 polyStreakColor = lerp(streakDust, streakWarm, saturate(sunLit * 0.36f + 0.44f));
        lowPolySky = lerp(lowPolySky, polyStreakColor, clamp(polyStreak * 0.86f, 0.0f, 0.86f));

        float upperShard = 0.0f;
        upperShard = max(upperShard, LowPolyCloudStreak(skyCoord, float2(0.19f, 0.80f), float2(0.090f, 0.016f), -1.85f));
        upperShard = max(upperShard, LowPolyCloudStreak(skyCoord, float2(0.31f, 0.86f), float2(0.115f, 0.018f), -1.70f) * 0.82f);
        upperShard = max(upperShard, LowPolyCloudStreak(skyCoord, float2(0.47f, 0.82f), float2(0.082f, 0.014f), -1.55f) * 0.62f);
        upperShard = max(upperShard, LowPolyCloudStreak(skyCoord, float2(0.69f, 0.84f), float2(0.130f, 0.020f), -1.65f) * 0.90f);
        upperShard = max(upperShard, LowPolyCloudStreak(skyCoord, float2(0.82f, 0.76f), float2(0.100f, 0.016f), -1.80f) * 0.74f);
        upperShard *= smoothstep(0.68f, 0.74f, height) * (1.0f - smoothstep(0.92f, 0.98f, height));
        upperShard = floor(upperShard * 3.0f + 0.5f) / 3.0f;
        float3 shardShadow = clamp(lerp(float3(0.82f, 0.52f, 0.58f), uHorizonColor * float3(0.76f, 0.46f, 0.48f), 0.46f), 0.0f, 1.5f);
        float3 shardLight = clamp(lerp(float3(1.38f, 0.80f, 0.48f), uSunColor * float3(1.22f, 0.88f, 0.68f), 0.56f), 0.0f, 1.9f);
        float3 shardColor = lerp(shardShadow, shardLight, saturate(sunLit * 0.46f + 0.46f));
        lowPolySky = lerp(lowPolySky, shardColor, clamp(upperShard * uCloudParams.w * 0.66f, 0.0f, 0.66f));

        float streakCloud = 0.0f;
        streakCloud = max(streakCloud, CloudLobe(skyCoord, float2(0.17f, 0.82f), float2(0.10f, 0.026f), 31.0f));
        streakCloud = max(streakCloud, CloudLobe(skyCoord, float2(0.37f, 0.77f), float2(0.085f, 0.022f), 33.0f) * 0.70f);
        streakCloud = max(streakCloud, CloudLobe(skyCoord, float2(0.64f, 0.83f), float2(0.12f, 0.028f), 35.0f) * 0.82f);
        streakCloud = max(streakCloud, CloudLobe(skyCoord, float2(0.86f, 0.74f), float2(0.09f, 0.024f), 37.0f) * 0.62f);
        streakCloud *= smoothstep(0.69f, 0.75f, height) * (1.0f - smoothstep(0.88f, 0.96f, height));
        streakCloud = floor(streakCloud * 3.0f + 0.5f) / 3.0f;
        float3 streakShadow = clamp(lerp(float3(0.72f, 0.58f, 0.66f), uHorizonColor * float3(0.84f, 0.58f, 0.60f), 0.62f), 0.0f, 1.7f);
        float3 streakLight = clamp(lerp(float3(1.28f, 0.82f, 0.50f), uSunColor * float3(1.22f, 0.90f, 0.70f), 0.62f), 0.0f, 1.9f);
        float3 streakColor = lerp(streakShadow, streakLight, saturate(sunLit * 0.54f + 0.36f));
        lowPolySky = lerp(lowPolySky, streakColor, clamp(streakCloud * uCloudParams.w * 0.58f, 0.0f, 0.58f));

        float horizonHaze = smoothstep(0.18f, 0.36f, height) * (1.0f - smoothstep(0.48f, 0.62f, height));
        lowPolySky = lerp(lowPolySky, clamp(uHorizonColor * float3(0.88f, 0.82f, 0.74f), 0.0f, 1.6f), horizonHaze * 0.12f);
        float cleanSun = sunDisk * 1.55f + innerGlow * 0.34f + outerGlow * 0.050f;
        float3 lowPolySun = clamp(lerp(float3(1.10f, 0.98f, 0.72f), uSunColor * float3(1.00f, 0.92f, 0.64f), 0.42f), 0.0f, 2.0f);
        lowPolySky += lowPolySun * cleanSun * 0.74f;
        finalSky = lowPolySky;
    }
    else if (mode == 7)
    {
        finalSky = lerp(float3(0.02f, 0.08f, 0.13f), float3(0.95f, 0.38f, 0.16f), pow(1.0f - height, 1.35f)) + sun * 0.35f;
    }
    else if (mode == 8)
    {
        finalSky = lerp(float3(0.26f, 0.32f, 0.36f), float3(0.58f, 0.62f, 0.62f), 1.0f - height) + cloudMask * float3(0.08f, 0.08f, 0.08f);
    }
    else if (mode == 9)
    {
        finalSky = lerp(float3(0.02f, 0.18f, 0.38f), float3(0.62f, 0.82f, 1.0f), pow(1.0f - height, 1.4f)) + sun * 0.25f;
    }
    else if (mode == 10)
    {
        finalSky = lerp(float3(0.020f, 0.023f, 0.026f), float3(0.10f, 0.12f, 0.14f), 1.0f - height);
    }
    else if (mode == 12)
    {
        float planet = 1.0f - smoothstep(0.19f, 0.205f, distance(skyCoord, float2(0.76f, 0.72f)));
        finalSky = float3(0.006f, 0.008f, 0.020f) + float3(0.38f, 0.48f, 0.72f) * planet + sun * 0.18f;
    }
    else if (mode == 13)
    {
        float storm = cloudMask + ValueNoise(skyCoord * 12.0f) * 0.25f;
        finalSky = lerp(float3(0.045f, 0.050f, 0.055f), float3(0.26f, 0.30f, 0.34f), storm) + uSunColor * sunDisk * 0.8f;
    }
    else if (mode == 16)
    {
        finalSky = lerp(float3(0.42f, 0.24f, 0.70f), float3(1.0f, 0.58f, 0.82f), 1.0f - height) + float3(0.38f, 0.86f, 1.0f) * outerGlow * 0.28f;
    }
    else if (mode == 17)
    {
        finalSky = lerp(float3(0.55f, 0.68f, 0.82f), float3(0.94f, 0.96f, 0.98f), pow(1.0f - height, 1.6f));
    }
    else if (mode == 18)
    {
        finalSky = 0.5f + 0.5f * cos(float3(0.0f, 2.0f, 4.0f) + skyCoord.x * 7.0f + skyCoord.y * 5.0f);
    }
    else if (mode == 19)
    {
        finalSky = lerp(float3(0.42f, 0.72f, 1.0f), float3(1.0f, 0.74f, 0.42f), pow(1.0f - height, 1.8f)) + sun * 0.18f;
    }

    return float4(finalSky, 1.0f);
}
