// Text rendering shader (combined HLSL)
// 2D orthographic projection for bitmap font quads

cbuffer ProjectionBuffer : register(b0)
{
    float4x4 projection;
};

cbuffer TextColorBuffer : register(b1)
{
    float3 textColor;
    float padding;
};

Texture2D fontTexture : register(t0);
SamplerState fontSampler : register(s0);

struct VS_INPUT
{
    float2 position : POSITION;
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
    output.position = mul(float4(input.position, 0.0f, 1.0f), projection);
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    float alpha = fontTexture.Sample(fontSampler, input.texCoord).r;
    return float4(textColor, alpha);
}
