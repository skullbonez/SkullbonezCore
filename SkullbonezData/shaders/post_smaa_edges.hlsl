#include "smaa_common.hlsli"
VS_OUT main_vs(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;
    output.position = float4(input.position, 0, 1);
    output.texCoord = float2(input.texCoord.x, 1-input.texCoord.y);
    SMAAEdgeDetectionVS(output.texCoord, output.offset);
    return output;
}
float4 main_ps(VS_OUT input) : SV_TARGET
{
    Texture2D color = ResourceDescriptorHeap[_textureDescriptorIndices0.x];
    return float4(SMAALumaEdgeDetectionPS(input.texCoord, input.offset, color), 0, 0);
}
