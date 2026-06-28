/*
File: SkullbonezData/shaders/launcher_laser.hlsl
Purpose:
  Draws launcher-mode laser ribbon triangles.

Mental model:
  CPU code builds short-lived world-space ribbon quads. The shader only
  transforms them and forwards per-vertex color/alpha.

Glossary:
  HLSL (High-Level Shader Language): Shader language used by this file; the
    CPU-side renderer compiles it and binds resources by the declared registers.
  Root signature: CPU-defined binding contract that must match the shader's
    constant buffers, textures, samplers, and UAV/SRV register declarations.
  Ribbon: Camera-facing strip geometry emitted by CPU launcher diagnostics.
  Vertex color: Per-vertex RGBA payload supplied in TEXCOORD0.

Invariants:
  - Input layout is float3 position followed by float4 color.
  - The shader does not compute laser geometry; CPU-side ribbon construction
    owns width, length, and alpha policy.

Related:
  - SkullbonezSource/SkullbonezLauncherLaser.cpp
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
