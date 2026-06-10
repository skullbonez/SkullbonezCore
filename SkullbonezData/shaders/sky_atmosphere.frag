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
    else if (mode == 6 || mode == 11)
    {
        finalSky = floor(finalSky * 5.0) / 5.0;
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
