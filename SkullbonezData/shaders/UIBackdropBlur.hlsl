/*
File: SkullbonezData/shaders/UIBackdropBlur.hlsl
Purpose:
  Runs the UIBackdropBlur HLSL shader program used by the renderer.

Summary:
  UIBackdropBlur.hlsl is shader source for the renderer's UIBackdropBlur pass.
  Keep edits anchored on shader inputs, bindings, and render-output contracts
  and on the glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;
    float4   uTexelSize;
};

// Invariant: b1 carries stable indices into the directly indexed shader-visible
// heap; the pipeline owner writes all six root constants before every draw.
cbuffer BindlessTextureIndices : register(b1)
{
    uint4 _textureDescriptorIndices0;
    uint2 _textureDescriptorIndices1;
};

uint BindlessTextureIndex(uint slot)
{
    return slot < 4u ? _textureDescriptorIndices0[slot] : _textureDescriptorIndices1[slot - 4u];
}

SamplerState sSampler0 : register(s0);

struct VS_IN
{
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
    float2 texelSize : TEXCOORD1;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    output.texelSize = uTexelSize.xy;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    Texture2D<float4> textureSource = ResourceDescriptorHeap[BindlessTextureIndex(0u)];
    static const float weights[5] = { 0.0625, 0.25, 0.375, 0.25, 0.0625 };
    float3 color = float3(0.0, 0.0, 0.0);

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 uv = saturate(input.texCoord + float2((float)x, (float)y) * input.texelSize * 2.25);
            color += textureSource.Sample(sSampler0, uv).rgb * weights[x + 2] * weights[y + 2];
        }
    }

    color = saturate(color * float3(1.18, 1.22, 1.28) + float3(0.07, 0.08, 0.10));
    return float4(color, 0.94);
}
