// Unlit textured shader (HLSL 5.0, combined VS+PS)
// MVP transform only, no lighting. Used for skybox.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;
    float4x4 uView;
    float4x4 uProjection;
    float4   uColorTint;
};

Texture2D    uTexture  : register(t0);
SamplerState sSampler0 : register(s0);

struct VS_IN
{
    float3 position : POSITION;
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
    float4 worldPos = mul(uModel, float4(input.position, 1.0));
    float4 viewPos  = mul(uView, worldPos);
    output.position = mul(uProjection, viewPos);
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return uTexture.Sample(sSampler0, input.texCoord) * uColorTint;
}
