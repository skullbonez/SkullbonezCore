#version 330 core

// =============================================================================
// CINEMATIC SKY ATMOSPHERE SHADER (OpenGL)
// =============================================================================
//
// This shader draws the sunset sky as a full-screen background. It is procedural:
// there is no sky photograph or cube map here. The shader blends a horizon color
// into a zenith color, adds a bright sun disk/glow, and layers art-directed cloud
// shapes over the top.
//
// Because this is screen-space, it is cheap and easy to tune from the Cine tab.
// =============================================================================

in vec2 vTexCoord;

uniform vec4 uSunParams; // x/y screen position, z disk intensity, w glow strength.
uniform vec3 uSunColor;
uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;
uniform vec4 uCloudParams; // coverage, softness, scale, intensity
uniform int uSkyMode;

out vec4 FragColor;

float Hash21(vec2 p)
{
    // Tiny deterministic random number generator. Same input gives same output,
    // which is important because clouds should not flicker frame to frame.
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p)
{
    // Smooth random field. This is one ingredient for soft cloud interiors.
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float CloudFBM(vec2 p)
{
    // Fractal noise: stack several noise layers at different scales. This gives
    // clouds broad forms plus smaller wispy variation.
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += ValueNoise(p) * amp;
        p *= 2.07;
        amp *= 0.5;
    }
    return v;
}

float CloudLobe(vec2 uv, vec2 center, vec2 radius, float seed)
{
    // One soft oval cloud puff. Several lobes are combined below to build the
    // large backlit cloud banks seen near the sun.
    vec2 q = (uv - center) / radius;
    float broad = length(q * vec2(0.88, 1.12));
    float cap = length((uv - center + vec2(0.04, -0.025)) / (radius * vec2(0.78, 0.86)));
    float ragged = (sin((uv.x + seed) * 31.0) + sin((uv.y - seed) * 37.0)) * 0.018;
    float body = 1.0 - smoothstep(0.72, 1.24, broad + ragged);
    float crown = 1.0 - smoothstep(0.45, 1.06, cap + ragged * 0.45);
    return clamp(body * 0.72 + crown * 0.28, 0.0, 1.0);
}

float LowPolyCloudStreak(vec2 uv, vec2 center, vec2 size, float skew)
{
    vec2 q = uv - center;
    q.x += q.y * skew;
    float diamond = abs(q.x) / max(size.x, 0.001) + abs(q.y) / max(size.y, 0.001);
    float mask = 1.0 - smoothstep(0.76, 1.02, diamond);
    return floor(clamp(mask, 0.0, 1.0) * 3.0 + 0.5) / 3.0;
}

float HeroCloudMask(vec2 uv)
{
    // These are deliberate screen-space cloud placements, not random clouds.
    // That makes the first cinematic view read like the reference composition.
    float mask = 0.0;
    mask = max(mask, CloudLobe(uv, vec2(0.20, 0.64), vec2(0.28, 0.105), 2.1) * 0.84);
    mask = max(mask, CloudLobe(uv, vec2(0.40, 0.68), vec2(0.22, 0.090), 5.7) * 0.62);
    mask = max(mask, CloudLobe(uv, vec2(0.66, 0.78), vec2(0.26, 0.090), 8.6) * 0.48);
    mask = max(mask, CloudLobe(uv, vec2(0.84, 0.64), vec2(0.30, 0.110), 12.3) * 0.54);
    mask = max(mask, CloudLobe(uv, vec2(0.55, 0.55), vec2(0.26, 0.080), 17.2) * 0.36);
    mask = max(mask, CloudLobe(uv, vec2(0.10, 0.55), vec2(0.24, 0.080), 21.4) * 0.46);
    mask = max(mask, CloudLobe(uv, vec2(0.74, 0.58), vec2(0.34, 0.095), 24.9) * 0.42);
    mask = max(mask, CloudLobe(uv, vec2(0.96, 0.54), vec2(0.22, 0.080), 28.5) * 0.38);
    return clamp(mask, 0.0, 1.0);
}

float LowPolyRidgeHeight(float x, float baseY, float amplitude, float frequency, float phase)
{
    float primary = 1.0 - abs(fract(x * frequency + phase) * 2.0 - 1.0);
    float secondary = 1.0 - abs(fract(x * frequency * 1.73 + phase * 1.91 + 0.17) * 2.0 - 1.0);
    float broad = 1.0 - abs(fract(x * frequency * 0.48 + phase * 0.63 + 0.41) * 2.0 - 1.0);
    return baseY + amplitude * (primary * 0.62 + secondary * 0.28 + broad * 0.22);
}

float CloudLayerMask(vec2 uv, out float cloudShape)
{
    // Build a cloud mask. Coverage moves the threshold up/down, softness controls
    // edge width, and intensity is applied later when mixing cloud color.
    vec2 lowerUV = vec2(uv.x * 1.28 + 0.12, uv.y * 2.55 + 0.18) * max(uCloudParams.z, 0.001);
    lowerUV.x += sin(uv.y * 5.0) * 0.07;
    float broad = CloudFBM(lowerUV);
    float detail = CloudFBM(lowerUV * 2.45 + vec2(6.8, 1.7));
    float erosion = CloudFBM(lowerUV * 4.20 + vec2(11.4, 5.7));
    cloudShape = broad * 0.80 + detail * 0.20 - (erosion - 0.42) * 0.12;

    float coverage = clamp(uCloudParams.x, 0.0, 1.0);
    float threshold = mix(0.76, 0.34, coverage);
    float softness = max(uCloudParams.y * 1.55, 0.001);
    float lowerMask = smoothstep(threshold, threshold + softness, cloudShape);
    lowerMask *= smoothstep(0.18, 0.82, 1.0 - erosion * 0.34);
    float lowerBand = smoothstep(0.34, 0.50, uv.y) * (1.0 - smoothstep(0.76, 0.92, uv.y));

    float hero = HeroCloudMask(uv);
    return clamp(max(lowerMask * lowerBand * 0.0, hero * 1.0), 0.0, 1.0);
}

void main()
{
    // Vertical gradient: warm/orange near the horizon and darker higher up.
    float height = clamp(vTexCoord.y, 0.0, 1.0);
    float vertical = pow(height, 0.65);
    vec3 skyColor = mix(uHorizonColor, uZenithColor, vertical);

    // The sun is made from a hard disk plus two exponential glows. The glow is
    // intentionally larger than the disk to create the blown-out sunset feel.
    float sunDistance = distance(vTexCoord, uSunParams.xy);
    float sunDisk = 1.0 - smoothstep(0.018, 0.045, sunDistance);
    float innerGlow = exp(-sunDistance * 18.0);
    float outerGlow = exp(-sunDistance * 4.6);
    float horizonScatter = exp(-abs(vTexCoord.y - uSunParams.y) * 3.4) *
                           (1.0 - smoothstep(0.08, 0.65, sunDistance));

    vec3 sun = uSunColor * (sunDisk * uSunParams.z +
                            innerGlow * uSunParams.w +
                            outerGlow * (uSunParams.w * 0.35) +
                            horizonScatter * 0.8);

    // Clouds are darker on their bodies but pick up strong orange light on edges
    // that face the sun. This is the "silver lining" effect, just warmer.
    float cloudShape = 0.0;
    float cloudMask = CloudLayerMask(vTexCoord, cloudShape);
    float sunLit = exp(-sunDistance * 3.2);
    float cloudEdge = 1.0 - smoothstep(0.00, 0.16, abs(cloudShape - mix(0.76, 0.34, clamp(uCloudParams.x, 0.0, 1.0))));
    cloudEdge = max(cloudEdge, smoothstep(0.08, 0.42, cloudMask) * (1.0 - smoothstep(0.58, 0.95, cloudMask)));
    float sunEdge = cloudEdge * (0.35 + sunLit * 1.2);
    vec3 cloudShadow = vec3(0.22, 0.10, 0.075);
    vec3 cloudMid = vec3(0.68, 0.27, 0.10);
    vec3 cloudLight = vec3(2.05, 1.02, 0.35) * (0.68 + sunLit * 0.92);
    vec3 cloudColor = mix(cloudShadow, cloudMid, 0.45 + sunLit * 0.22);
    cloudColor = mix(cloudColor, cloudLight, clamp(sunEdge, 0.0, 1.0));
    float cloudAmount = cloudMask * clamp(uCloudParams.w, 0.0, 1.5);
    skyColor = mix(skyColor, cloudColor, clamp(cloudAmount * 0.42, 0.0, 1.0));
    // Let clouds dim the sun where they overlap it so the disk can peek through
    // gaps instead of always drawing on top.
    float sunOcclusion = cloudAmount * smoothstep(0.42, 0.04, sunDistance);
    sun *= 1.0 - clamp(sunOcclusion * 0.72 + cloudAmount * 0.20, 0.0, 0.86);

    int mode = uSkyMode;
    vec3 finalSky = skyColor + sun;
    if (mode == 1)
    {
        finalSky = mix(vec3(0.15, 0.16, 0.15), vec3(0.46, 0.43, 0.36), pow(1.0 - height, 1.8)) + cloudMask * vec3(0.10, 0.09, 0.07);
    }
    else if (mode == 2)
    {
        finalSky = mix(vec3(0.035, 0.038, 0.042), vec3(0.42, 0.45, 0.48), pow(1.0 - height, 2.2));
    }
    else if (mode == 3 || mode == 15)
    {
        float scan = pow(max(0.0, 1.0 - abs(vTexCoord.y - 0.18) * 8.0), 2.0);
        finalSky = vec3(0.004, 0.008, 0.018) + vec3(0.0, 0.80, 1.0) * scan * 0.45 + vec3(1.0, 0.0, 0.75) * pow(max(0.0, 1.0 - abs(vTexCoord.x - 0.75) * 3.0), 4.0) * 0.20;
    }
    else if (mode == 4)
    {
        float secondSun = exp(-distance(vTexCoord, vec2(0.74, 0.68)) * 8.0);
        finalSky = mix(vec3(0.12, 0.04, 0.20), vec3(0.48, 0.18, 0.72), 1.0 - height) + vec3(0.20, 1.20, 0.72) * secondSun;
    }
    else if (mode == 5)
    {
        finalSky = mix(vec3(0.18, 0.13, 0.08), vec3(0.76, 0.48, 0.20), pow(1.0 - height, 1.1)) + cloudMask * vec3(0.20, 0.12, 0.04);
    }
    else if (mode == 6)
    {
        finalSky = floor(finalSky * 5.0) / 5.0;
    }
    else if (mode == 11)
    {
        vec3 horizon = clamp(mix(vec3(1.00, 0.95, 0.80), uHorizonColor, 0.78), 0.0, 1.8);
        vec3 zenith = clamp(mix(vec3(0.55, 0.75, 1.00), uZenithColor, 0.78), 0.0, 1.8);
        vec3 middle = clamp(mix(horizon, zenith, 0.44) + vec3(0.05, 0.04, 0.02), 0.0, 1.8);
        vec3 lowPolySky = mix(horizon, middle, smoothstep(0.08, 0.55, height));
        lowPolySky = mix(lowPolySky, zenith, smoothstep(0.50, 1.0, height));
        float band = floor(height * 7.0) / 7.0;
        vec3 bandedSky = mix(horizon, middle, smoothstep(0.08, 0.55, band));
        bandedSky = mix(bandedSky, zenith, smoothstep(0.50, 1.0, band));
        lowPolySky = mix(lowPolySky, bandedSky, 0.32);

        float farRidge = LowPolyRidgeHeight(vTexCoord.x, 0.60, 0.18, 2.25, 0.11);
        float midRidge = LowPolyRidgeHeight(vTexCoord.x, 0.55, 0.16, 3.35, 0.37);
        float nearRidge = LowPolyRidgeHeight(vTexCoord.x, 0.49, 0.13, 4.55, 0.68);
        float ridgeFade = smoothstep(0.43, 0.54, height) * (1.0 - smoothstep(0.82, 0.92, height));
        float farMask = (1.0 - smoothstep(farRidge - 0.014, farRidge + 0.022, height)) * ridgeFade;
        float midMask = (1.0 - smoothstep(midRidge - 0.012, midRidge + 0.020, height)) * ridgeFade;
        float nearMask = (1.0 - smoothstep(nearRidge - 0.010, nearRidge + 0.018, height)) * ridgeFade;
        farMask = floor(farMask * 4.0 + 0.5) / 4.0;
        midMask = floor(midMask * 4.0 + 0.5) / 4.0;
        nearMask = floor(nearMask * 4.0 + 0.5) / 4.0;
        vec3 farMountain = clamp(mix(zenith, horizon, 0.32) * vec3(0.44, 0.38, 0.78), 0.0, 1.4);
        vec3 midMountain = clamp(mix(horizon, uSunColor, 0.18) * vec3(0.50, 0.38, 0.48), 0.0, 1.4);
        vec3 nearMountain = clamp(mix(horizon, vec3(0.16, 0.10, 0.09), 0.62) * vec3(0.50, 0.38, 0.34), 0.0, 1.3);
        lowPolySky = mix(lowPolySky, farMountain, clamp(farMask * 0.88, 0.0, 0.88));
        lowPolySky = mix(lowPolySky, midMountain, clamp(midMask * 0.90, 0.0, 0.90));
        lowPolySky = mix(lowPolySky, nearMountain, clamp(nearMask * 0.82, 0.0, 0.82));

        // Low-poly mode uses deliberate flat cloud cards instead of the broader
        // cinematic cloud bank. This keeps the sky clean and composed.
        float cardCloud = 0.0;
        cardCloud = max(cardCloud, CloudLobe(vTexCoord, vec2(0.16, 0.63), vec2(0.17, 0.052), 3.0));
        cardCloud = max(cardCloud, CloudLobe(vTexCoord, vec2(0.36, 0.68), vec2(0.22, 0.055), 6.0) * 0.92);
        cardCloud = max(cardCloud, CloudLobe(vTexCoord, vec2(0.56, 0.57), vec2(0.20, 0.050), 7.4) * 0.70);
        cardCloud = max(cardCloud, CloudLobe(vTexCoord, vec2(0.76, 0.66), vec2(0.23, 0.055), 9.0) * 0.84);
        cardCloud = max(cardCloud, CloudLobe(vTexCoord, vec2(0.91, 0.53), vec2(0.16, 0.045), 12.0) * 0.64);
        cardCloud *= smoothstep(0.38, 0.46, height) * (1.0 - smoothstep(0.82, 0.90, height));
        float cloudBand = smoothstep(0.035, 0.32, cardCloud);
        cloudBand = floor(cloudBand * 3.0 + 0.5) / 3.0;
        vec3 flatCloudShadow = clamp(mix(vec3(0.72, 0.58, 0.72), uHorizonColor * vec3(0.80, 0.50, 0.54), 0.72), 0.0, 1.6);
        vec3 flatCloudLight = clamp(mix(vec3(1.24, 0.88, 0.52), uSunColor * vec3(1.36, 0.98, 0.70), 0.58), 0.0, 1.8);
        vec3 flatCloud = mix(flatCloudShadow, flatCloudLight, clamp(sunLit * 0.50 + 0.38, 0.0, 1.0));
        lowPolySky = mix(lowPolySky, flatCloud, clamp(cloudBand * 0.94, 0.0, 0.94));

        float polyStreak = 0.0;
        polyStreak = max(polyStreak, LowPolyCloudStreak(vTexCoord, vec2(0.20, 0.58), vec2(0.24, 0.030), -1.00));
        polyStreak = max(polyStreak, LowPolyCloudStreak(vTexCoord, vec2(0.42, 0.63), vec2(0.30, 0.034), -0.80) * 0.94);
        polyStreak = max(polyStreak, LowPolyCloudStreak(vTexCoord, vec2(0.64, 0.70), vec2(0.24, 0.030), -0.72) * 0.84);
        polyStreak = max(polyStreak, LowPolyCloudStreak(vTexCoord, vec2(0.80, 0.56), vec2(0.22, 0.028), -1.10) * 0.72);
        polyStreak = max(polyStreak, LowPolyCloudStreak(vTexCoord, vec2(0.52, 0.78), vec2(0.18, 0.024), -0.96) * 0.58);
        polyStreak *= smoothstep(0.46, 0.52, height) * (1.0 - smoothstep(0.82, 0.92, height));
        vec3 streakWarm = clamp(mix(vec3(1.34, 0.66, 0.36), uSunColor * vec3(1.18, 0.78, 0.50), 0.66), 0.0, 1.9);
        vec3 streakDust = clamp(mix(vec3(0.80, 0.48, 0.56), uHorizonColor * vec3(0.72, 0.46, 0.50), 0.50), 0.0, 1.6);
        vec3 polyStreakColor = mix(streakDust, streakWarm, clamp(sunLit * 0.36 + 0.44, 0.0, 1.0));
        lowPolySky = mix(lowPolySky, polyStreakColor, clamp(polyStreak * 0.86, 0.0, 0.86));

        float upperShard = 0.0;
        upperShard = max(upperShard, LowPolyCloudStreak(vTexCoord, vec2(0.19, 0.80), vec2(0.090, 0.016), -1.85));
        upperShard = max(upperShard, LowPolyCloudStreak(vTexCoord, vec2(0.31, 0.86), vec2(0.115, 0.018), -1.70) * 0.82);
        upperShard = max(upperShard, LowPolyCloudStreak(vTexCoord, vec2(0.47, 0.82), vec2(0.082, 0.014), -1.55) * 0.62);
        upperShard = max(upperShard, LowPolyCloudStreak(vTexCoord, vec2(0.69, 0.84), vec2(0.130, 0.020), -1.65) * 0.90);
        upperShard = max(upperShard, LowPolyCloudStreak(vTexCoord, vec2(0.82, 0.76), vec2(0.100, 0.016), -1.80) * 0.74);
        upperShard *= smoothstep(0.68, 0.74, height) * (1.0 - smoothstep(0.92, 0.98, height));
        upperShard = floor(upperShard * 3.0 + 0.5) / 3.0;
        vec3 shardShadow = clamp(mix(vec3(0.82, 0.52, 0.58), uHorizonColor * vec3(0.76, 0.46, 0.48), 0.46), 0.0, 1.5);
        vec3 shardLight = clamp(mix(vec3(1.38, 0.80, 0.48), uSunColor * vec3(1.22, 0.88, 0.68), 0.56), 0.0, 1.9);
        vec3 shardColor = mix(shardShadow, shardLight, clamp(sunLit * 0.46 + 0.46, 0.0, 1.0));
        lowPolySky = mix(lowPolySky, shardColor, clamp(upperShard * uCloudParams.w * 0.66, 0.0, 0.66));

        float streakCloud = 0.0;
        streakCloud = max(streakCloud, CloudLobe(vTexCoord, vec2(0.17, 0.82), vec2(0.10, 0.026), 31.0));
        streakCloud = max(streakCloud, CloudLobe(vTexCoord, vec2(0.37, 0.77), vec2(0.085, 0.022), 33.0) * 0.70);
        streakCloud = max(streakCloud, CloudLobe(vTexCoord, vec2(0.64, 0.83), vec2(0.12, 0.028), 35.0) * 0.82);
        streakCloud = max(streakCloud, CloudLobe(vTexCoord, vec2(0.86, 0.74), vec2(0.09, 0.024), 37.0) * 0.62);
        streakCloud *= smoothstep(0.69, 0.75, height) * (1.0 - smoothstep(0.88, 0.96, height));
        streakCloud = floor(streakCloud * 3.0 + 0.5) / 3.0;
        vec3 streakShadow = clamp(mix(vec3(0.72, 0.58, 0.66), uHorizonColor * vec3(0.84, 0.58, 0.60), 0.62), 0.0, 1.7);
        vec3 streakLight = clamp(mix(vec3(1.18, 0.78, 0.50), uSunColor * vec3(1.18, 0.90, 0.72), 0.60), 0.0, 1.9);
        vec3 streakColor = mix(streakShadow, streakLight, clamp(sunLit * 0.54 + 0.36, 0.0, 1.0));
        lowPolySky = mix(lowPolySky, streakColor, clamp(streakCloud * uCloudParams.w * 0.52, 0.0, 0.52));

        float horizonHaze = smoothstep(0.18, 0.36, height) * (1.0 - smoothstep(0.48, 0.62, height));
        lowPolySky = mix(lowPolySky, clamp(uHorizonColor * vec3(0.88, 0.82, 0.74), 0.0, 1.6), horizonHaze * 0.12);
        float cleanSun = sunDisk * 1.55 + innerGlow * 0.34 + outerGlow * 0.050;
        vec3 lowPolySun = clamp(mix(vec3(1.10, 0.98, 0.72), uSunColor * vec3(1.00, 0.92, 0.64), 0.42), 0.0, 2.0);
        lowPolySky += lowPolySun * cleanSun * 0.74;
        finalSky = lowPolySky;
    }
    else if (mode == 7)
    {
        finalSky = mix(vec3(0.02, 0.08, 0.13), vec3(0.95, 0.38, 0.16), pow(1.0 - height, 1.35)) + sun * 0.35;
    }
    else if (mode == 8)
    {
        finalSky = mix(vec3(0.26, 0.32, 0.36), vec3(0.58, 0.62, 0.62), 1.0 - height) + cloudMask * vec3(0.08);
    }
    else if (mode == 9)
    {
        finalSky = mix(vec3(0.02, 0.18, 0.38), vec3(0.62, 0.82, 1.0), pow(1.0 - height, 1.4)) + sun * 0.25;
    }
    else if (mode == 10)
    {
        finalSky = mix(vec3(0.020, 0.023, 0.026), vec3(0.10, 0.12, 0.14), 1.0 - height);
    }
    else if (mode == 12)
    {
        float planet = 1.0 - smoothstep(0.19, 0.205, distance(vTexCoord, vec2(0.76, 0.72)));
        finalSky = vec3(0.006, 0.008, 0.020) + vec3(0.38, 0.48, 0.72) * planet + sun * 0.18;
    }
    else if (mode == 13)
    {
        float storm = cloudMask + ValueNoise(vTexCoord * 12.0) * 0.25;
        finalSky = mix(vec3(0.045, 0.050, 0.055), vec3(0.26, 0.30, 0.34), storm) + uSunColor * sunDisk * 0.8;
    }
    else if (mode == 16)
    {
        finalSky = mix(vec3(0.42, 0.24, 0.70), vec3(1.0, 0.58, 0.82), 1.0 - height) + vec3(0.38, 0.86, 1.0) * outerGlow * 0.28;
    }
    else if (mode == 17)
    {
        finalSky = mix(vec3(0.55, 0.68, 0.82), vec3(0.94, 0.96, 0.98), pow(1.0 - height, 1.6));
    }
    else if (mode == 18)
    {
        finalSky = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + vTexCoord.x * 7.0 + vTexCoord.y * 5.0);
    }
    else if (mode == 19)
    {
        finalSky = mix(vec3(0.42, 0.72, 1.0), vec3(1.0, 0.74, 0.42), pow(1.0 - height, 1.8)) + sun * 0.18;
    }

    FragColor = vec4(finalSky, 1.0);
}
