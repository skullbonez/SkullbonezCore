// Calm (inner) water shader (HLSL 5.0, combined VS+PS)
// No Y displacement — surface stays perfectly flat for clean reflection.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;
    float4x4 uView;
    float4x4 uProjection;
    float4x4 uReflectVP;
    float4   uColorTint;
    float    uReflectionStrength;
    float3   _pad0;
};

Texture2D    uReflectionTex : register(t1);
SamplerState sSampler1      : register(s1);

struct VS_IN
{
    float3 position : POSITION;
};

struct VS_OUT
{
    float4 position       : SV_POSITION;
    float4 reflectClipPos : TEXCOORD0;
    float2 worldXZ        : TEXCOORD1;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    float4 worldPos       = mul(uModel, float4(input.position, 1.0));
    output.position       = mul(uProjection, mul(uView, worldPos));
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ        = input.position.xz;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y; // DX top-down texture origin
    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    return lerp(uColorTint, reflection, uReflectionStrength);
}
