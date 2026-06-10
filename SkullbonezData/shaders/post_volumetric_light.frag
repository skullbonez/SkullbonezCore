#version 330 core

// =============================================================================
// CINEMATIC VOLUMETRIC LIGHT SHADER (OpenGL)
// =============================================================================
//
// This shader builds a separate soft light texture at half resolution. It does
// not draw real 3D fog volumes. Instead, each screen pixel samples repeatedly
// toward the sun position and asks: "how much bright sky can I see along this
// path?" If the answer is high, the pixel gets warm shaft light.
//
// The final tonemap shader later adds this texture over the main scene.
// =============================================================================

in vec2 vTexCoord;

uniform sampler2D uSceneTex;
uniform sampler2D uDepthTex;
uniform vec4 uDepthParams;       // near, far, unused, unused
uniform vec4 uSunShaftParams;    // x/y screen position, strength, falloff
uniform vec3 uSunColor;
uniform vec4 uVolumetricParams;  // strength, density, decay, fog density
uniform vec4 uCloudParams;       // coverage, softness, scale, intensity

out vec4 FragColor;

float Hash21(vec2 p)
{
    // Cheap repeatable pseudo-random value from a 2D point. Used to build soft
    // cloud breakup without needing a cloud texture.
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 74.7);
    return fract(p.x * p.y);
}

float ValueNoise(vec2 p)
{
    // Smooth value noise. Neighboring positions get similar values, so the cloud
    // gaps feel cloudy instead of speckled.
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash21(i);
    float b = Hash21(i + vec2(1.0, 0.0));
    float c = Hash21(i + vec2(0.0, 1.0));
    float d = Hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float CloudBreakup(vec2 uv)
{
    // Adds smaller holes and unevenness to the ray mask so beams do not look like
    // perfectly smooth computer cones.
    vec2 p = uv * vec2(5.2, 2.2) + vec2(0.17, 1.31);
    float v = ValueNoise(p) * 0.55;
    v += ValueNoise(p * 2.03 + 4.0) * 0.30;
    v += ValueNoise(p * 4.11 + 9.0) * 0.15;
    return smoothstep(0.28, 0.86, v);
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
    // Hand-placed cloud forms that line up with the cinematic sky composition.
    // These shapes block parts of the rays like the reference image clouds.
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

float CloudLayerMask(vec2 uv)
{
    // Combines broad noise, fine detail, erosion, and hand-placed cloud shapes
    // into a single 0..1 "cloud is here" mask.
    vec2 lowerUV = vec2(uv.x * 1.28 + 0.12, uv.y * 2.55 + 0.18) * max(uCloudParams.z, 0.001);
    lowerUV.x += sin(uv.y * 5.0) * 0.07;
    float broad = ValueNoise(lowerUV) * 0.50;
    broad += ValueNoise(lowerUV * 2.07 + 3.4) * 0.25;
    broad += ValueNoise(lowerUV * 4.28 + 8.1) * 0.125;
    float detail = ValueNoise(lowerUV * 2.45 + vec2(6.8, 1.7)) * 0.50;
    detail += ValueNoise(lowerUV * 5.04 + vec2(9.4, 4.2)) * 0.25;
    float erosion = ValueNoise(lowerUV * 4.20 + vec2(11.4, 5.7)) * 0.55;
    erosion += ValueNoise(lowerUV * 8.38 + vec2(12.9, 7.1)) * 0.25;
    float cloudShape = broad * 0.80 + detail * 0.20 - (erosion - 0.42) * 0.12;

    float threshold = mix(0.76, 0.34, clamp(uCloudParams.x, 0.0, 1.0));
    float lowerMask = smoothstep(threshold, threshold + max(uCloudParams.y * 1.55, 0.001), cloudShape);
    lowerMask *= smoothstep(0.18, 0.82, 1.0 - erosion * 0.34);
    float lowerBand = smoothstep(0.34, 0.50, uv.y) * (1.0 - smoothstep(0.76, 0.92, uv.y));
    return clamp(max(lowerMask * lowerBand * 0.0, HeroCloudMask(uv) * 1.0) * clamp(uCloudParams.w, 0.0, 1.5), 0.0, 1.0);
}

float CloudRayOpen(vec2 uv)
{
    // Converts the cloud mask into "how open is this part of the sky?" 1 means
    // open sky, 0 means cloud is blocking most of the light.
    float cloud = CloudLayerMask(uv);
    float breakup = CloudBreakup(uv + uSunShaftParams.xy * 0.17);
    return clamp(0.18 + (1.0 - cloud) * 0.62 + breakup * 0.20, 0.0, 1.0);
}

float LinearizeDepth(float depth)
{
    // Convert depth-buffer values back into approximate scene distance so far
    // hills and near objects can influence the amount of haze differently.
    float nearPlane = max(uDepthParams.x, 0.0001);
    float farPlane = max(uDepthParams.y, nearPlane + 0.0001);
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

vec2 ClampScreenUV(vec2 uv)
{
    return clamp(uv, vec2(0.0), vec2(1.0));
}

float SampleLightTransmittance(vec2 uv)
{
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
    {
        return 0.0;
    }

    // Sky pixels have depth near 1. Solid pixels can still contribute a little
    // if they are distant and bright, which makes far haze glow near the horizon.
    float rawDepth = texture(uDepthTex, uv).r;
    vec3 sceneColor = texture(uSceneTex, ClampScreenUV(uv)).rgb;
    float skyMask = rawDepth >= 0.9999 ? 1.0 : 0.0;
    float linearDepth = LinearizeDepth(rawDepth);
    float distantGeometry = smoothstep(90.0, 1250.0, linearDepth) * (1.0 - skyMask);
    float brightness = max(max(sceneColor.r, sceneColor.g), sceneColor.b);
    float brightPath = smoothstep(0.25, 2.2, brightness);
    float cloudOpen = CloudRayOpen(uv);
    return (skyMask * (0.38 + brightPath * 0.62) + distantGeometry * 0.22) * cloudOpen;
}

void main()
{
    vec2 sunUV = uSunShaftParams.xy;
    if (sunUV.x < -0.15 || sunUV.y < -0.15 || sunUV.x > 1.15 || sunUV.y > 1.15)
    {
        FragColor = vec4(0.0);
        return;
    }

    // The receiver term fades shafts differently over sky and solid geometry.
    // Geometry gets a fog-based amount so the light feels suspended in air.
    float rawDepth = texture(uDepthTex, vTexCoord).r;
    float linearDepth = LinearizeDepth(rawDepth);
    float geometryMask = rawDepth < 0.9999 ? 1.0 : 0.0;
    float distanceFog = 1.0 - exp(-max(linearDepth, 0.0) * max(uVolumetricParams.w, 0.0) * 0.70);
    float receiver = mix(1.0, clamp(distanceFog * 1.4, 0.20, 0.72), geometryMask);

    // Step from this pixel toward the sun. Each step asks whether that point is
    // open sky or blocked by cloud/geometry. The accumulated answer becomes a
    // warm light shaft.
    const int sampleCount = 48;
    vec2 delta = (sunUV - vTexCoord) * max(uVolumetricParams.y, 0.05) / float(sampleCount);
    vec2 sampleUV = vTexCoord;
    float decay = clamp(uVolumetricParams.z, 0.80, 0.995);
    float illuminationDecay = 1.0;
    float accum = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        sampleUV += delta;
        accum += SampleLightTransmittance(sampleUV) * illuminationDecay;
        illuminationDecay *= decay;
    }

    // Shape the shaft so it is strongest below/near the sun and fades outward.
    float sunDistance = length(vTexCoord - sunUV);
    float radialFalloff = exp(-sunDistance * max(uSunShaftParams.w, 0.001));
    float belowSun = smoothstep(0.0, 0.44, sunUV.y - vTexCoord.y);
    float verticalColumn = pow(max(0.0, 1.0 - abs(vTexCoord.x - sunUV.x) * 3.8), 2.0);
    float shaft = accum / float(sampleCount);
    shaft *= radialFalloff * belowSun * (0.55 + verticalColumn * 0.45) * receiver;
    shaft *= max(uVolumetricParams.x, 0.0) * max(uSunShaftParams.z, 0.0);

    FragColor = vec4(uSunColor * shaft, 1.0);
}
