// Instanced shadow decal shader (combined HLSL)
// Static disc geometry drawn once per instance via DrawInstanced
// Per-instance model matrix and alpha arrive via vertex attributes (instance buffer)

cbuffer ViewProjBuffer : register(b0)
{
    float4x4 view;
    float4x4 projection;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float4 model0 : TEXCOORD1;
    float4 model1 : TEXCOORD2;
    float4 model2 : TEXCOORD3;
    float4 model3 : TEXCOORD4;
    float alpha : TEXCOORD5;
    uint instanceID : SV_InstanceID;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float alpha : TEXCOORD0;
};

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;

    float4x4 model = float4x4(input.model0, input.model1, input.model2, input.model3);
    float4x4 viewProj = mul(view, projection);

    float4 worldPos = mul(float4(input.position, 1.0f), model);
    output.position = mul(worldPos, viewProj);

    float distFromCenter = length(input.position.xz);
    output.alpha = input.alpha * (1.0f - distFromCenter);

    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    return float4(0.0f, 0.0f, 0.0f, input.alpha);
}
