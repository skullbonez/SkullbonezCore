// Calm water shader (combined HLSL)
// No Y displacement — surface stays perfectly flat for clean reflection

cbuffer TransformBuffer : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4x4 reflectVP;
};

cbuffer WaterSettings : register(b1)
{
    float4 colorTint;
    float reflectionStrength;
    float3 padding;
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

    float4 worldPos = mul(float4(input.position, 1.0f), model);
    output.position = mul(mul(worldPos, view), projection);
    output.reflectClipPos = mul(mul(float4(input.position, 1.0f), model), reflectVP);
    output.worldXZ = input.position.xz;

    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5f + 0.5f;
    float4 reflection = reflectionTexture.Sample(reflectionSampler, reflUV);
    return lerp(colorTint, reflection, reflectionStrength);
}
