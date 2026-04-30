// Water shader (combined HLSL)
// Generates subtle ripple animation via layered sine-wave Y displacement
// Blends deep ocean blue with planar reflection texture

cbuffer TransformBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 reflectVP;
    float time;
    int flatWater;
    float2 padding0;
};

cbuffer WaterSettings : register(b1)
{
    float4 colorTint;
    float reflectionStrength;
    int noReflect;
    int noPerturb;
    float padding1;
};

Texture2D reflectionTexture : register(t0);
SamplerState reflectionSampler : register(s0);

struct VS_INPUT
{
    float3 position : POSITION;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 reflectClipPos : TEXCOORD1;
    float2 worldXZ : TEXCOORD0;
};

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;

    float3 pos = input.position;
    if (flatWater == 0)
    {
        pos.y += sin(pos.x * 0.04f + time * 1.2f) * 1.5f
               + sin(pos.z * 0.06f + time * 0.8f) * 1.0f;
    }

    float4 worldPos = mul(float4(pos, 1.0f), model);
    output.position = mul(mul(worldPos, view), projection);
    output.reflectClipPos = mul(mul(float4(input.position, 1.0f), model), reflectVP);
    output.worldXZ = input.position.xz;

    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    if (noReflect != 0)
    {
        return colorTint;
    }

    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5f + 0.5f;

    if (noPerturb == 0)
    {
        float wave = sin(input.worldXZ.x * 0.04f + time * 1.2f) * 1.5f
                   + sin(input.worldXZ.y * 0.06f + time * 0.8f) * 1.0f;
        reflUV += float2(wave * 0.002f, wave * 0.002f);
    }

    float4 reflection = reflectionTexture.Sample(reflectionSampler, reflUV);
    return lerp(colorTint, reflection, reflectionStrength);
}
