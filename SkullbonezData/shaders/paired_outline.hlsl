// Geometric edge coverage for paired views. This program never shades objects
// or changes their materials; its output is a separate screen-space mask.
#pragma pack_matrix(column_major)
#include "line_coverage.hlsli"
cbuffer Uniforms : register(b0)
{
    float4x4 uModelViewProjection;
    float4 uOutlineViewport; // width, height, line radius in pixels, X-ray
};
cbuffer BindlessTextureIndices : register(b1)
{
    uint4 _textureDescriptorIndices0;
    uint2 _textureDescriptorIndices1;
};
struct Vertex
{
    float3 start : POSITION;
    float3 end : NORMAL;
    float2 corner : TEXCOORD0;
};
struct Pixel
{
    float4 position : SV_POSITION;
    noperspective float2 alongAcross : TEXCOORD0;
    nointerpolation float length : TEXCOORD1;
};
Pixel main_vs(Vertex input)
{
    LineQuad quad = ExpandLineQuad(mul(uModelViewProjection, float4(input.start, 1)),
                                  mul(uModelViewProjection, float4(input.end, 1)),
                                  input.corner, uOutlineViewport.xy, uOutlineViewport.z);
    Pixel output;
    output.position = quad.position;
    output.alongAcross = quad.coordinate;
    output.length = quad.segmentLength;
    return output;
}
float4 main_ps(Pixel input) : SV_TARGET
{
    Texture2D<float> depth = ResourceDescriptorHeap[_textureDescriptorIndices0.x];
    float nearestDepth = depth.Load(int3(int2(input.position.xy), 0));
    // The feather spans neighboring surface samples. Include their local slope
    // so depth quantization and a tilted face cannot punch holes in an edge.
    float tolerance = max(0.0000002, 2 * max(fwidth(input.position.z), fwidth(nearestDepth)));
    clip(nearestDepth + tolerance - input.position.z);
    if (uOutlineViewport.w < 0.5)
    {
        Texture2D<float> otherDepth = ResourceDescriptorHeap[_textureDescriptorIndices0.y];
        float other = otherDepth.Load(int3(int2(input.position.xy), 0));
        float otherTolerance = max(0.0000002, 2 * max(fwidth(input.position.z), fwidth(other)));
        clip(other + otherTolerance - input.position.z);
    }
    float coverage = LineCoverage(input.alongAcross, input.length, uOutlineViewport.z);
    // Alpha blending accumulates coverage at joins without a later faint edge
    // erasing an earlier solid edge. Rounded caps keep corners continuous.
    return float4(1, 1, 1, coverage);
}
