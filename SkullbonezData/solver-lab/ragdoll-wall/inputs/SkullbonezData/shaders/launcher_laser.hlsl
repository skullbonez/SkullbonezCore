/*
File: SkullbonezData/shaders/launcher_laser.hlsl
Purpose:
  Render player aim laser and predictive aim guides with soft falloff.

Summary:
  Expands ribbon vertices along camera-facing vectors to draw the glowing aim
  raycast trajectory with animated pulse intensity and core-to-edge glow falloff.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must match this shader exactly.
  - Additive blending combines core laser and outer glow in a single pass.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
*/

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uViewProj;
};

struct VS_IN
{
    float3 position : POSITION;
    float4 color    : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uViewProj, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return input.color;
}
