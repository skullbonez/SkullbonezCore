// A scene-authored studio horizon shared by sky, material lighting and wet ground.
// These previously unused integer styles are opt-in; existing modes never enter it.
#ifndef SPLIT_ENVIRONMENT_INCLUDED
#define SPLIT_ENVIRONMENT_INCLUDED
static const int SPLIT_OBJECT_STYLE = 14;
static const int SPLIT_SKY_STYLE = 22;
static const int SPLIT_TERRAIN_STYLE = 16;
static const int SPLIT_WATER_STYLE = 5;

float SplitHash(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float SplitNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(SplitHash(cell), SplitHash(cell + float2(1, 0)), f.x),
                lerp(SplitHash(cell + float2(0, 1)), SplitHash(cell + 1.0f), f.x), f.y);
}

float SplitCloud(float2 p)
{
    return SplitNoise(p) * 0.55f + SplitNoise(p * 2.07f + 5.3f) * 0.28f
         + SplitNoise(p * 4.21f + 12.7f) * 0.12f + SplitNoise(p * 8.43f) * 0.05f;
}

float3 SplitEnvironment(float3 direction)
{
    float3 d = normalize(direction);
    float warm = smoothstep(0.55f, -0.65f, d.x);
    float horizon = exp(-abs(d.y - 0.04f) * 4.8f);
    float3 zenith = lerp(float3(0.012f, 0.035f, 0.095f), float3(0.12f, 0.016f, 0.025f), warm);
    float3 horizonColor = lerp(float3(0.13f, 0.42f, 0.72f), float3(1.8f, 0.29f, 0.042f), warm);
    float3 color = lerp(zenith, horizonColor, horizon);
    // World-direction clouds remain stable when the camera or a reflected ray moves.
    float2 cloudUv = d.xz / (abs(d.y) + 0.22f) * 1.7f;
    float cloud = smoothstep(0.43f, 0.76f, SplitCloud(cloudUv * float2(1.0f, 2.6f)));
    float3 cloudLight = lerp(float3(0.065f, 0.16f, 0.29f), float3(0.95f, 0.095f, 0.025f), warm);
    color = lerp(color, cloudLight * (0.45f + horizon), cloud * smoothstep(0.01f, 0.18f, d.y) * 0.78f);
    float ground = smoothstep(-0.015f, -0.16f, d.y);
    return lerp(color, float3(0.022f, 0.026f, 0.034f), ground);
}

float3 SplitDiffuseLight(float3 N)
{
    // Fixed cosine-weighted hemisphere quadrature. This integrates the same
    // radiance used by the sky without a texture upload or per-frame allocation.
    float3 up = abs(N.y) < 0.95f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    float3 irradiance = 0.0f;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float radius = sqrt((i + 0.5f) / 8.0f);
        float angle = i * 2.39996323f;
        float3 sampleDir = T * (cos(angle) * radius) + B * (sin(angle) * radius)
                         + N * sqrt(1.0f - radius * radius);
        irradiance += SplitEnvironment(sampleDir);
    }
    return irradiance * 0.125f;
}

float3 SplitSpecularLight(float3 R, float roughness)
{
    float3 up = abs(R.y) < 0.95f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, R));
    float3 B = cross(R, T);
    float3 radiance = 0.0f;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float radius = sqrt((i + 0.5f) / 8.0f) * roughness * roughness * 1.8f;
        float angle = i * 2.39996323f;
        radiance += SplitEnvironment(normalize(R + radius * (T * cos(angle) + B * sin(angle))));
    }
    return radiance * 0.125f;
}
#endif
