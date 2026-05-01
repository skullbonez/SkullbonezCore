// Solid color shader (combined HLSL)
// Outputs a flat RGBA color (used for HUD background quads)

cbuffer ProjectionBuffer : register(b0)
{
    float4x4 projection;
};

cbuffer ColorBuffer : register(b1)
{
    float4 color;
};

struct VS_INPUT
{
    float2 position : POSITION;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

PS_INPUT main_vs(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 0.0f, 1.0f), projection);
    return output;
}

float4 main_ps(PS_INPUT input) : SV_TARGET
{
    return color;
}
