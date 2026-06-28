/*
File: SkullbonezData/shaders/tornado_fx.hlsl
Purpose:
  Draws sparse tornado visual-effect triangles.

Mental model:
  CPU code builds low-alpha world-space ribbons and dust billboards. This shader
  transforms positions, keeps ribbons soft, and breaks dust quads into mottled
  terrain-faded clumps.

Glossary:
  HLSL (High-Level Shader Language): Shader language used by this file; the
    CPU-side renderer compiles it and binds resources by the declared registers.
  Root signature: CPU-defined binding contract that must match the shader's
    constant buffers, textures, samplers, and UAV/SRV register declarations.
  Ribbon: World-space strip used for the tornado funnel shell.
  Dust billboard: Quad whose opacity is shaped in the pixel shader.
  Terrain fade: Alpha falloff based on height above the authored terrain plane.

Invariants:
  - Input layout is position, color, then fx payload; CPU generation must match
    those semantics exactly.
  - fx.z selects dust behavior; non-dust ribbon pixels return the CPU color.

Related:
  - SkullbonezSource/Runtime/RunPasses.cpp
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
    float4 fx       : TEXCOORD1; // uv.xy, kind, terrainY
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float3 world    : TEXCOORD0;
    float4 fx       : TEXCOORD1;
    float4 color    : COLOR0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uViewProj, float4(input.position, 1.0));
    output.world = input.position;
    output.fx = input.fx;
    output.color = input.color;
    return output;
}

float HashNoise(float2 value)
{
    return frac(sin(dot(value, float2(12.9898, 78.233))) * 43758.5453);
}

float DirtMask(float2 uv, float2 seed)
{
    const float2 centered = uv * 2.0 - 1.0;
    const float distanceSq = dot(centered, centered);
    const float oval = 1.0 - smoothstep(0.12, 1.0, distanceSq);
    const float edgeNoise = HashNoise(seed + uv * 13.7);
    const float clumpNoise = HashNoise(seed * 0.41 + floor(uv * 5.0));
    const float ragged = smoothstep(0.08, 0.95, oval + (edgeNoise - 0.5) * 0.32);
    const float flecks = smoothstep(0.34, 0.92, edgeNoise * 0.62 + clumpNoise * 0.38);
    const float mottled = saturate(0.50 + edgeNoise * 0.32 + clumpNoise * 0.24);
    return ragged * mottled * (0.35 + 0.65 * flecks);
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float4 color = input.color;
    if (input.fx.z > 0.5)
    {
        const float2 uv = saturate(input.fx.xy);
        const float2 seed = input.world.xz * 0.073 + input.fx.w * 0.019;
        const float dirt = DirtMask(uv, seed);
        const float groundFade = smoothstep(2.0, 12.0, input.world.y - input.fx.w);
        const float grain = HashNoise(seed + uv * 31.0);
        color.rgb *= lerp(0.72, 1.16, grain);
        color.a *= dirt * groundFade;
        clip(color.a - 0.002);
    }
    return color;
}
