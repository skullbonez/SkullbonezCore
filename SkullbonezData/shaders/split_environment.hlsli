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
    // Integer lattice hashing gives adjacent cells identical shared corners.
    // Floating frac hashes can disagree after compiler reassociation and make
    // seams across the smooth noise interpolation visible in sky/reflections.
    uint2 cell = asuint(int2(p));
    uint h = cell.x * 1597334677u ^ cell.y * 3812015801u;
    h = (h ^ (h >> 16u)) * 2246822519u;
    h = (h ^ (h >> 13u)) * 3266489917u;
    h ^= h >> 16u;
    return float(h & 0x00ffffffu) / 16777215.0f;
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
    float warm = 1.0f - smoothstep(-0.55f, 0.35f, d.x);
    float horizon = exp(-abs(d.y - 0.015f) * 12.0f);
    float3 zenith = lerp(float3(0.012f, 0.035f, 0.095f), float3(0.12f, 0.016f, 0.025f), warm);
    float3 horizonColor = lerp(float3(0.055f, 0.24f, 0.47f), float3(1.4f, 0.20f, 0.025f), warm);
    float3 color = lerp(zenith, horizonColor, horizon);
    // World-direction clouds remain stable when the camera or a reflected ray moves.
    float2 cloudUv = d.xz / (abs(d.y) + 0.16f) * 3.1f;
    cloudUv += float2(SplitCloud(cloudUv * 0.8f), SplitCloud(cloudUv * 0.8f + 17.0f)) * 0.4f;
    float cloudField = SplitCloud(cloudUv * float2(1.0f, 2.6f));
    float cloud = smoothstep(0.28f, 0.82f, cloudField);
    float3 cloudLight = lerp(float3(0.065f, 0.16f, 0.29f), float3(0.95f, 0.095f, 0.025f), warm);
    color = lerp(color, cloudLight * (0.09f + horizon), cloud * smoothstep(0.01f, 0.18f, d.y) * 0.68f);
    // Broad off-camera illumination gives the coating a readable soft highlight.
    color += float3(8.0f, 7.2f, 6.3f) * pow(saturate(dot(d, normalize(float3(-0.45f, 0.55f, 0.75f)))), 18.0f);
    color += float3(0.12f, 0.45f, 0.85f) * pow(saturate(dot(d, normalize(float3(0.85f, 0.3f, 0.35f)))), 12.0f);
    float ground = smoothstep(-0.015f, -0.16f, d.y);
    return lerp(color, float3(0.022f, 0.026f, 0.034f), ground);
}

float3 SplitDiffuseLight(float3 N)
{
    // Low-frequency diffuse approximation of the horizon and broad light lobes.
    // Avoid sparse quadrature: tiny cloud samples produced visible bands as the
    // integration basis rotated over a smooth sphere.
    float sky = saturate(N.y * 0.5f + 0.5f);
    float3 irradiance = lerp(float3(0.035f, 0.03f, 0.025f), float3(0.13f, 0.16f, 0.21f), sky);
    irradiance += float3(0.40f, 0.12f, 0.035f) * saturate(dot(N, normalize(float3(-0.8f, 0.3f, -0.3f))));
    irradiance += float3(0.75f, 0.68f, 0.57f) * saturate(dot(N, normalize(float3(-0.45f, 0.55f, 0.75f))));
    irradiance += float3(0.035f, 0.08f, 0.15f) * saturate(dot(N, normalize(float3(0.85f, 0.3f, 0.35f))));
    return irradiance;
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
