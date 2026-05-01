// Unlit textured shader (combined HLSL)
// MVP transform only, no lighting. Used for skybox, water.

cbuffer TransformBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
};

Texture2D mainTexture : register(t0);
SamplerState mainSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;
    float4 worldPos = mul(float4(input.position, 1.0f), model);
    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, projection);
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    return mainTexture.Sample(mainSampler, input.texCoord);
}
