// Text rendering shader (HLSL 5.0, combined VS+PS)
// 2D orthographic projection for bitmap font quads.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;
    float3   uTextColor;
    float    _pad0;
};

Texture2D    uFontTexture : register(t0);
SamplerState sSampler0    : register(s0);

struct VS_IN
{
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float alpha = uFontTexture.Sample(sSampler0, input.texCoord).r;
    return float4(uTextColor, alpha);
}
