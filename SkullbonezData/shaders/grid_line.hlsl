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
                                  float2(endpoint, side), uViewportPixels.xy, uViewportPixels.w > 0.5 ? 3.25 : 0.7);
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

    // Narrow cores remain legible through tonemapping; the low-energy halo
    // fades over three pixels rather than widening the apparent wireframe.
    float core = LineCoverage(input.coordinate, input.segmentLength, 0.48);
    float2 delta = float2(input.coordinate.x - clamp(input.coordinate.x, 0, input.segmentLength), input.coordinate.y);
    float halo = exp2(-dot(delta, delta) * 0.65);
    float chroma = max(input.color.r, max(input.color.g, input.color.b)) - min(input.color.r, min(input.color.g, input.color.b));
    float resting = 1.0 - smoothstep(0.05, 0.18, chroma);
    // The pale-cyan ending pose is deliberately quieter than a contact.
    float horizon = all(abs(input.color - float3(0.45, 0.92, 1.0)) < 0.015) ? 1.0 : 0.0;
    float hint = uViewportPixels.w > 1.5 ? 0.10 : 1.0;
    float3 color = lerp(input.color, float3(0.025, 0.48, 1.0), saturate((input.color.b - input.color.r) * 3.0));
    float alpha = (core * lerp(0.70, 0.24, resting) + halo * 0.08 * (1.0 - resting)) * lerp(1.0, 0.38, horizon) * hint * saturate(uViewportPixels.z);
    return float4(color * lerp(1.9, 0.85, resting), alpha);
}
