// Ocean (outer) water shader (HLSL 5.0, combined VS+PS)
// Layered sine-wave Y displacement for open-ocean surface animation.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;
    float4x4 uView;
    float4x4 uProjection;
    float4x4 uReflectVP;
    float4   uColorTint;
    float    uTime;
    float    uWaveHeight;
    float    uReflectionStrength;
    float    uPerturbStrength;
    int      uFlatWater;
    int      uNoReflect;
    float2   _pad0;
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

    float3 pos = input.position;
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(pos.z * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    }

    float4 worldPos       = mul(uModel, float4(pos, 1.0));
    output.position       = mul(uProjection, mul(uView, worldPos));
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ        = input.position.xz;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    if (uNoReflect != 0)
    {
        return uColorTint;
    }

    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y; // DX top-down texture origin

    float wave = sin(input.worldXZ.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(input.worldXZ.y * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    reflUV += float2(wave * uPerturbStrength, wave * uPerturbStrength);

    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    return lerp(uColorTint, reflection, uReflectionStrength);
}

