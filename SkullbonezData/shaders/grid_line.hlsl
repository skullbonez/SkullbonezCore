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
                                  float2(endpoint, side), uViewportPixels.xy, 0.7);
    Pixel output;
    output.position = quad.position;
    output.coordinate = quad.coordinate;
    output.segmentLength = quad.segmentLength;
    output.color = lerp(input.startColor, input.endColor, endpoint);
    return output;
}
float4 main_ps(Pixel input) : SV_TARGET
{
    return float4(input.color, LineCoverage(input.coordinate, input.segmentLength, 0.7));
}
