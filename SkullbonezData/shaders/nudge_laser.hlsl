/*
File: SkullbonezData/shaders/nudge_laser.hlsl
Purpose:
  Draws nudge-mode laser ribbon triangles.

Mental model:
  CPU code builds short-lived world-space ribbon quads. The shader only
  transforms them and forwards per-vertex color/alpha.

Related:
  - SkullbonezSource/SkullbonezNudgeLaser.cpp
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
