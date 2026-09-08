/*
File: SkullbonezData/shaders/shadow_depth.hlsl
Purpose:
  Render scene depth from the directional light's perspective into shadow maps.

Summary:
  Transforms non-instanced geometry through the light space view-projection
  matrix, writing linear depth values into the cascading shadow map atlas.

Invariants:
  - Pixel shader is null or minimal; depth write is hardware-accelerated.
  - Slope-scaled depth bias is bound on the pipeline state to prevent shadow acne.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Rendering/Shadow.h
*/

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;
    float4x4 uView;
    float4x4 uProjection;
    float4   uClipPlane;
    float4   uCinematicTerrain;
    float4   uCinematicBasin;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float  clipDist : SV_ClipDistance0;
};

float BasinDistance(float2 xz)
{
    float2 radius = max(uCinematicBasin.zw, float2(1.0f, 1.0f));
    return length((xz - uCinematicBasin.xy) / radius);
}

float CinematicTerrainOffset(float2 xz)
{
    if (uCinematicTerrain.x < 0.5f || uCinematicTerrain.y <= 0.0f)
    {
        return 0.0f;
    }

    float d = BasinDistance(xz);
    float bowl = 1.0f - smoothstep(0.10f, 0.94f, d);
    float rim = exp(-pow((d - 1.04f) * 3.1f, 2.0f));
    float slopeTexture = smoothstep(0.32f, 0.92f, d) * (1.0f - smoothstep(1.02f, 1.55f, d));
    float rough = (sin(xz.x * 0.045f + xz.y * 0.011f) + sin(xz.y * 0.052f - xz.x * 0.017f)) * 0.5f;
    return uCinematicTerrain.y * (-uCinematicTerrain.z * bowl + uCinematicTerrain.w * rim + rough * 1.6f * slopeTexture);
}

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    float4 worldPos = mul(uModel, float4(input.position, 1.0f));
    worldPos.y += CinematicTerrainOffset(worldPos.xz);
    output.position = mul(uProjection, mul(uView, worldPos));
    output.clipDist = dot(worldPos, uClipPlane);
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
