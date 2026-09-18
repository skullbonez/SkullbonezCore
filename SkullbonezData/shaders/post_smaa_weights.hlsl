#include "smaa_common.hlsli"
VS_OUT main_vs(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;
    output.position = float4(input.position, 0, 1);
    output.texCoord = float2(input.texCoord.x, 1-input.texCoord.y);
    SMAABlendingWeightCalculationVS(output.texCoord, output.pixcoord, output.offset);
    return output;
}
float4 main_ps(VS_OUT input) : SV_TARGET
{
    Texture2D edges = ResourceDescriptorHeap[_textureDescriptorIndices0.x];
    Texture2D area = ResourceDescriptorHeap[_textureDescriptorIndices0.y];
    Texture2D search = ResourceDescriptorHeap[_textureDescriptorIndices0.z];
    return SMAABlendingWeightCalculationPS(input.texCoord, input.pixcoord, input.offset, edges, area, search, 0);
}
