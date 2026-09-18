// Colored debug lines, world gizmos and retained line overlays. Two existing
// xyz/rgb endpoints form one instance, expanded into an antialiased capsule.
#pragma pack_matrix(column_major)
#include "line_coverage.hlsli"
cbuffer Uniforms : register(b0)
{
    float4x4 uViewProj;
    float4 uViewportPixels;
};
struct Vertex
{
    float3 start : POSITION;
    float3 startColor : TEXCOORD0;
    float3 end : TEXCOORD1;
    float3 endColor : TEXCOORD2;
};
struct Pixel
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
    noperspective float2 coordinate : TEXCOORD0;
    nointerpolation float segmentLength : TEXCOORD1;
};
Pixel main_vs(Vertex input, uint vertexId : SV_VertexID)
{
    uint cornerIndex = vertexId % 6;
    float endpoint = cornerIndex == 1 || cornerIndex == 2 || cornerIndex == 4 ? 1.0 : 0.0;
    float side = cornerIndex == 2 || cornerIndex == 4 || cornerIndex == 5 ? 1.0 : -1.0;
    LineQuad quad = ExpandLineQuad(mul(uViewProj, float4(input.start, 1)),
                                  mul(uViewProj, float4(input.end, 1)),
                                  float2(endpoint, side), uViewportPixels.xy, uViewportPixels.w > 0.5 ? 2.0 : 0.7);
    // Invariant: only explicit world-space point records become lights. A
    // regular edge viewed end-on must not acquire a spurious corner light.
    if (uViewportPixels.w > 0.5 && all(input.start == input.end))
    {
        float4 center = mul(uViewProj, float4(input.start, 1));
        if (center.w > 0.0001 && center.z >= 0)
        {
            quad.coordinate = float2(endpoint * 2 - 1, side) * 4.0;
            quad.position = center;
            quad.position.xy += quad.coordinate * float2(2, -2) / max(uViewportPixels.xy, 1) * center.w;
            quad.segmentLength = 0;
        }
    }
    Pixel output;
    output.position = quad.position;
    output.coordinate = quad.coordinate;
    output.segmentLength = quad.segmentLength;
    output.color = lerp(input.startColor, input.endColor, endpoint);
    return output;
}
float4 main_ps(Pixel input) : SV_TARGET
{
    if (uViewportPixels.w < 0.5)
        return float4(input.color, LineCoverage(input.coordinate, input.segmentLength, 0.7) * saturate(uViewportPixels.z));

    // Semantic colours: blue identifies contact, orange identifies rest.
    // Both retain subpixel cores; halo energy is concentrated at corner points.
    float resting = (all(abs(input.color - float3(0.58, 0.58, 0.62)) < 0.015) ||
                     all(abs(input.color - float3(0.18, 0.62, 0.78)) < 0.015)) ? 1.0 : 0.0;
    float horizon = all(abs(input.color - float3(0.45, 0.92, 1.0)) < 0.015) ? 1.0 : 0.0;
    float hint = uViewportPixels.w > 1.5 ? 0.10 : 1.0;
    float cool = saturate((input.color.b - input.color.r) * 3.0);
    float3 color = lerp(input.color, float3(0.015, 0.55, 1.0), cool);
    color = lerp(color, float3(1.0, 0.22, 0.008), max(resting, horizon));
    float poseOpacity = lerp(1.0, 0.68, resting) * lerp(1.0, 0.58, horizon);
    float2 delta = float2(input.coordinate.x - clamp(input.coordinate.x, 0, input.segmentLength), input.coordinate.y);
    float distanceSquared = dot(delta, delta);
    bool isPoint = input.segmentLength == 0;
    float core = LineCoverage(input.coordinate, input.segmentLength, isPoint ? 1.0 : 0.32);
    float halo = exp2(-distanceSquared * (isPoint ? 0.38 : 1.15));
    float alpha = (core * 0.65 + halo * (isPoint ? 0.22 : 0.025)) * poseOpacity * hint * saturate(uViewportPixels.z);
    float3 emission = color * (isPoint ? 2.8 : 1.65);
    if (isPoint) emission += core * float3(0.3, 0.3, 0.3);
    return float4(emission, saturate(alpha));
}
