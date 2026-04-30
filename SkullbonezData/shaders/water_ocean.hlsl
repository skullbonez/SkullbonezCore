// Ocean water shader (combined HLSL)
// Layered sine-wave Y displacement for open-ocean surface animation

cbuffer TransformBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 reflectVP;
    float time;
    float waveHeight;
    int flatWater;
    float padding0;
};

cbuffer WaterSettings : register(b1)
{
    float4 colorTint;
    float reflectionStrength;
    float perturbStrength;
    int noReflect;
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
        pos.y += sin(pos.x * 0.04f + time * 1.2f) * waveHeight
               + sin(pos.z * 0.06f + time * 0.8f) * (waveHeight * 0.667f);
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

    float wave = sin(input.worldXZ.x * 0.04f + time * 1.2f) * waveHeight
               + sin(input.worldXZ.y * 0.06f + time * 0.8f) * (waveHeight * 0.667f);
    reflUV += float2(wave * perturbStrength, wave * perturbStrength);

    float4 reflection = reflectionTexture.Sample(reflectionSampler, reflUV);
    return lerp(colorTint, reflection, reflectionStrength);
}
