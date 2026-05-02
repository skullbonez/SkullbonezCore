// Solid color shader (HLSL 5.0, combined VS+PS)
// Flat RGBA color for HUD background quads.

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;
    float4   uColor;
};

struct VS_IN
{
    float2 position : POSITION;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return uColor;
}
