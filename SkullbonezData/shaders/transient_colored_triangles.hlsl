/*
File: SkullbonezData/shaders/transient_colored_triangles.hlsl
Purpose:
  Render dynamic per-frame transient colored triangles for tornado shells and visual passes.

Summary:
  Draws unindexed dynamic triangle buffers allocated per-frame for fluid
  surfaces, vortex funnels, and transient geometric effects without retained GPU buffers.

Invariants:
  - Storage is allocated from transient frame dynamic heaps and valid for one frame only.
  - Alpha blending respects authored visual pass sorting order.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Gameplay/TornadoVisualPass.cpp
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

float CloudHash(float2 p)
{
    float3 q = frac(float3(p.xyx) * .1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float CloudNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(CloudHash(cell), CloudHash(cell+float2(1,0)), f.x),
                lerp(CloudHash(cell+float2(0,1)), CloudHash(cell+1), f.x), f.y);
}
float CloudDensity(float2 p)
{
    float value = CloudNoise(p) * .57;
    p = float2(p.x*.8-p.y*.6, p.x*.6+p.y*.8) * 2.07 + 7.3;
    value += CloudNoise(p) * .29;
    return value + CloudNoise(p * 2.11 + 19.7) * .14;
}
float4 main_ps(VS_OUT input) : SV_TARGET
{
    float4 color = input.color;
    if (input.fx.z > .5)
    {
        // Local coherent noise moves with each cloud sample. World-position
        // white noise would crawl across its surface as the camera or cloud moves.
        float2 p = input.fx.xy * 2.0 - 1.0;
        clip(1.0 - dot(p,p));
        float seed = frac(input.fx.z) * 137.0;
        float noise = CloudDensity(p * 2.8 + float2(seed, seed * .73));
        float radius = length(p);
        float edge = 1.0 - smoothstep(.35, 1.0, radius + (noise-.5) * .30);
        // The outer envelope reaches zero inside the quad, including when the
        // noise expands a lobe; no rectangular billboard edge can survive.
        edge *= 1.0 - smoothstep(.88, 1.0, radius);
        float density = edge * smoothstep(.13, .80, noise + edge * .26);
        float groundFade = smoothstep(-.2, 1.5, input.world.y-input.fx.w);
        float3 normal = normalize(float3(p * .65, sqrt(saturate(1.0-dot(p,p))) + .25));
        float light = .65 + .35 * saturate(dot(normal, normalize(float3(-.45,.65,.75))));
        color.rgb *= light * lerp(.78, 1.20, noise);
        color.a *= density * groundFade;
        clip(color.a - .002);
    }
    return color;
}
