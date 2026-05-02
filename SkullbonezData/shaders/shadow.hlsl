// Instanced shadow decal shader (HLSL 5.0, combined VS+PS)
// Static disc geometry drawn via DrawInstanced. Per-instance model matrix and alpha.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uView;
    float4x4 uProjection;
};

struct VS_IN
{
    float3 position : POSITION;
    float4 model0   : TEXCOORD1;
    float4 model1   : TEXCOORD2;
    float4 model2   : TEXCOORD3;
    float4 model3   : TEXCOORD4;
    float  alpha    : TEXCOORD5;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float  alpha    : TEXCOORD0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    float4x4 model = float4x4(input.model0, input.model1, input.model2, input.model3);
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(uProjection, mul(uView, worldPos));

    float distFromCenter = length(input.position.xz);
    output.alpha = input.alpha * (1.0 - distFromCenter);

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return float4(0.0, 0.0, 0.0, input.alpha);
}
