// Shared world-space turf colour keeps the ground and rooted blades continuous.
// No bitmap samples: broad mowing bands and restrained seed variation survive LOD.
float TurfHash(float2 p)
{
    float3 q = frac(float3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float TurfNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(TurfHash(cell), TurfHash(cell + float2(1,0)), f.x),
                lerp(TurfHash(cell + float2(0,1)), TurfHash(cell + 1), f.x), f.y);
}
float TurfMowBand(float2 p)
{
    return smoothstep(-0.08, 0.08, sin((p.x + p.y * 0.15) * 0.22439948));
}
float3 TurfColor(float2 p)
{
    float variation = TurfNoise(p * 0.65);
    float3 green = lerp(float3(0.255,0.34,0.175), float3(0.29,0.375,0.205), variation);
    return green * lerp(0.96, 1.04, TurfMowBand(p));
}
float3 TurfLightTint(float3 light)
{
    // Broad diffuse fill softens the legacy orange sun without losing its intensity.
    float luminance = dot(light, float3(0.2126,0.7152,0.0722));
    return lerp(light, luminance.xxx, 0.8);
}
