#version 330 core

// =============================================================================
// CINEMATIC TONEMAP / FINAL COMPOSITE SHADER (OpenGL)
// =============================================================================
//
// This is the last shader in the cinematic render path. By the time we get here,
// the 3D world has already been drawn into uSceneTex as an HDR image. "HDR" means
// the texture can contain colors brighter than a monitor can directly show.
//
// This shader turns that bright off-screen image into the final visible frame:
//  1. Use the depth texture to add distance fog only over real geometry.
//  2. Add sun shafts/god rays where the sun shines through sky and cloud gaps.
//  3. Add the separate half-resolution volumetric-light texture.
//  4. Add bloom around pixels that are already very bright.
//  5. Tonemap HDR color back into normal monitor range.
//  6. Apply gamma, a small saturation/contrast lift, and a vignette.
//
// Every pixel on screen runs this shader once.
// =============================================================================

in vec2 vTexCoord;

uniform sampler2D uSceneTex;
uniform sampler2D uDepthTex;
uniform sampler2D uVolumetricTex;
uniform float uExposure;
uniform float uGamma;
uniform float uVolumetricCompositeStrength;
uniform vec4 uDepthParams; // near, far, unused, unused
uniform vec4 uFogParams;   // start, end, density, max opacity
uniform vec3 uFogColor;
uniform vec4 uSunShaftParams; // x/y screen position, strength, falloff
uniform vec3 uSunColor;
uniform vec4 uBloomParams; // threshold, knee, strength, radius
uniform vec4 uCloudParams; // coverage, softness, scale, intensity
uniform vec4 uStyleGrade;  // saturation, contrast, vignette floor, sky mode

out vec4 FragColor;

vec3 TonemapACES(vec3 color)
{
    // ACES is a film-style curve. It keeps bright highlights warm and punchy
    // without simply clipping them to flat white.
    return clamp((color * (2.51 * color + 0.03)) /
                 (color * (2.43 * color + 0.59) + 0.14),
                 0.0,
                 1.0);
}

float LinearizeDepth(float depth)
{
    // Hardware depth is stored non-linearly for precision. This converts it
    // back into an approximate world/camera distance so fog can fade by meters
    // instead of by the raw 0..1 depth-buffer value.
    float nearPlane = max(uDepthParams.x, 0.0001);
    float farPlane = max(uDepthParams.y, nearPlane + 0.0001);
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

vec3 PrefilterBloom(vec3 color)
{
    // Bloom should only come from bright pixels. The threshold chooses what is
    // bright enough, and the knee softens the cutoff so bloom fades in smoothly.
    float brightness = max(max(color.r, color.g), color.b);
    float threshold = max(uBloomParams.x, 0.0);
    float knee = max(uBloomParams.y, 0.0001);
    float soft = clamp((brightness - threshold + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft * knee;
    float contribution = max(brightness - threshold, soft) / max(brightness, 0.0001);
    return color * contribution;
}

vec2 ClampScreenUV(vec2 uv)
{
    return clamp(uv, vec2(0.0), vec2(1.0));
}

vec3 SampleScene(vec2 uv)
{
    return texture(uSceneTex, ClampScreenUV(uv)).rgb;
}

vec3 SampleBloom(vec2 uv)
{
    if (uBloomParams.z <= 0.0)
    {
        return vec3(0.0);
    }

    // This is a tiny blur kernel. We sample neighboring pixels around the
    // current pixel, keep only their bright parts, and add them as glow.
    vec2 texel = 1.0 / vec2(textureSize(uSceneTex, 0));
    float radius = max(uBloomParams.w, 0.25);
    vec3 bloom = PrefilterBloom(SampleScene(uv)) * 0.20;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( radius,  0.0))) * 0.10;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(-radius,  0.0))) * 0.10;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( 0.0,  radius))) * 0.10;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( 0.0, -radius))) * 0.10;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( radius,  radius))) * 0.07;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(-radius,  radius))) * 0.07;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( radius, -radius))) * 0.07;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(-radius, -radius))) * 0.07;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2( radius * 2.5, 0.0))) * 0.04;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(-radius * 2.5, 0.0))) * 0.04;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(0.0,  radius * 2.5))) * 0.04;
    bloom += PrefilterBloom(SampleScene(uv + texel * vec2(0.0, -radius * 2.5))) * 0.04;
    return bloom * uBloomParams.z;
}

float Hash21(vec2 p)
{
    // A cheap repeatable pseudo-random number from a 2D point. Shaders cannot
    // call rand(), so little hash functions like this are common building blocks.
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 74.7);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p)
{
    // Smooth grid noise. We use it to make cloud openings and ray masks feel
    // organic instead of perfectly mathematical.
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float CloudLobe(vec2 uv, vec2 center, vec2 radius, float seed)
{
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
    // Hand-placed cloud blobs that roughly match the reference composition.
    // They are screen-space shapes: stable, art-directed, and cheap to evaluate.
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

float CloudRayOpen(vec2 uv)
{
    // Returns how open the sky is at this screen position. 1 means light can pass
    // through freely; lower values mean a cloud is blocking the ray.
    vec2 lowerUV = vec2(uv.x * 1.28 + 0.12, uv.y * 2.55 + 0.18) * max(uCloudParams.z, 0.001);
    lowerUV.x += sin(uv.y * 5.0) * 0.07;
    float cloudShape = ValueNoise(lowerUV) * 0.50;
    cloudShape += ValueNoise(lowerUV * 2.07 + 3.4) * 0.25;
    cloudShape += ValueNoise(lowerUV * 4.28 + 8.1) * 0.125;
    float erosion = ValueNoise(lowerUV * 4.20 + vec2(11.4, 5.7)) * 0.55;
    erosion += ValueNoise(lowerUV * 8.38 + vec2(12.9, 7.1)) * 0.25;
    cloudShape -= (erosion - 0.42) * 0.12;
    float threshold = mix(0.76, 0.34, clamp(uCloudParams.x, 0.0, 1.0));
    float cloud = smoothstep(threshold, threshold + max(uCloudParams.y * 1.55, 0.001), cloudShape);
    cloud *= smoothstep(0.18, 0.82, 1.0 - erosion * 0.34);
    cloud *= smoothstep(0.34, 0.50, uv.y) * (1.0 - smoothstep(0.76, 0.92, uv.y)) * 0.0;
    cloud = max(cloud, HeroCloudMask(uv) * 1.0);
    cloud = clamp(cloud * clamp(uCloudParams.w, 0.0, 1.5), 0.0, 1.0);
    return clamp(0.20 + (1.0 - cloud) * 0.80, 0.0, 1.0);
}

float SampleSkyTransmittance(vec2 uv)
{
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
    {
        return 0.0;
    }

    // A depth value near 1 means "nothing was drawn here", so the pixel is sky.
    // Rays are strongest when the sample is sky and the scene color is bright.
    float sampleDepth = texture(uDepthTex, uv).r;
    vec3 sampleColor = SampleScene(uv);
    float skyMask = sampleDepth >= 0.9999 ? 1.0 : 0.0;
    float brightness = max(max(sampleColor.r, sampleColor.g), sampleColor.b);
    float brightSky = smoothstep(0.28, 1.8, brightness);
    return skyMask * (0.35 + brightSky * 0.65) * CloudRayOpen(uv);
}

float RadialGodRays(vec2 uv)
{
    // March from the current pixel toward the sun in screen space. If many of
    // those samples are bright sky, the pixel receives a visible ray.
    vec2 sunUV = uSunShaftParams.xy;
    if (sunUV.x < -0.15 || sunUV.y < -0.15 || sunUV.x > 1.15 || sunUV.y > 1.15)
    {
        return 0.0;
    }

    const int sampleCount = 36;
    vec2 delta = (sunUV - uv) / float(sampleCount);
    vec2 sampleUV = uv;
    float illuminationDecay = 1.0;
    float accum = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        sampleUV += delta;
        float transmittance = SampleSkyTransmittance(sampleUV);
        accum += transmittance * illuminationDecay;
        illuminationDecay *= 0.95;
    }

    float sunDistance = length(uv - sunUV);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001));
    float belowSun = smoothstep(0.0, 0.45, sunUV.y - uv.y);
    return accum / float(sampleCount) * radialFalloff * belowSun;
}

void main()
{
    // Start with the fully rendered HDR world color and its depth.
    float rawDepth = texture(uDepthTex, vTexCoord).r;
    vec3 hdrColor = SampleScene(vTexCoord);

    // Depth fog: terrain and objects fade into warm air with distance. Sky pixels
    // are excluded so the fog does not wash out the procedural sky background.
    float linearDepth = LinearizeDepth(rawDepth);
    float fogRange = max(uFogParams.y - uFogParams.x, 0.0001);
    float rangeFog = clamp((linearDepth - uFogParams.x) / fogRange, 0.0, 1.0);
    float densityFog = 1.0 - exp(-max(linearDepth, 0.0) * max(uFogParams.z, 0.0));
    float geometryMask = rawDepth < 0.9999 ? 1.0 : 0.0;
    float fogAmount = min(max(uFogParams.w, 0.0), rangeFog * densityFog) * geometryMask;
    hdrColor = mix(hdrColor, uFogColor, fogAmount);

    // Extra low haze near the horizon sells the basin scale and backlit dust.
    float horizonBand = exp(-abs(vTexCoord.y - 0.52) * 7.0);
    float basinHaze = horizonBand * rangeFog * geometryMask * max(uFogParams.w, 0.0) * 0.18;
    hdrColor = mix(hdrColor, uFogColor * 1.08, basinHaze);

    // Screen-space god rays. The radial samples create broken shafts, while the
    // vertical column adds a stronger beam under the sun like the reference.
    float rayMask = RadialGodRays(vTexCoord);
    vec2 toSun = uSunShaftParams.xy - vTexCoord;
    float sunDistance = length(toSun);
    float belowSun = smoothstep(0.0, 0.35, uSunShaftParams.y - vTexCoord.y);
    float verticalColumn = pow(max(0.0, 1.0 - abs(vTexCoord.x - uSunShaftParams.x) * 4.0), 2.0);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001));
    float occlusionSoftening = mix(1.0, 0.35, geometryMask);
    float shaftAmount = radialFalloff * belowSun * (0.30 + verticalColumn * 0.70) * uSunShaftParams.z * 0.20 * occlusionSoftening;
    shaftAmount += rayMask * uSunShaftParams.z * 0.42 * (0.85 - geometryMask * 0.35);
    hdrColor += uSunColor * shaftAmount;
    // Add the cheaper half-resolution volumetric texture. It was generated in a
    // separate pass so we can keep this final pass simpler and faster.
    hdrColor += texture(uVolumetricTex, ClampScreenUV(vTexCoord)).rgb * max(uVolumetricCompositeStrength, 0.0);
    hdrColor += SampleBloom(vTexCoord);

    // Convert HDR to monitor color, then apply display gamma. The small contrast,
    // saturation, and vignette pushes are deliberately done last.
    vec3 mapped = TonemapACES(hdrColor * max(uExposure, 0.0));
    float safeGamma = max(uGamma, 0.001);
    mapped = pow(mapped, vec3(1.0 / safeGamma));
    float luminance = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(luminance), mapped, max(uStyleGrade.x, 0.0));
    mapped = clamp((mapped - 0.5) * max(uStyleGrade.y, 0.0) + 0.5, 0.0, 1.0);
    float vignette = 1.0 - smoothstep(0.28, 0.86, distance(vTexCoord, vec2(0.52, 0.48)));
    mapped *= mix(clamp(uStyleGrade.z, 0.0, 1.0), 1.0, vignette);
    FragColor = vec4(mapped, 1.0);
}
