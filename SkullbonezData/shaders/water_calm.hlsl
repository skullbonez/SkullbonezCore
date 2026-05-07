// =============================================================================
// CALM WATER SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render the INNER water zone as a perfectly flat mirror surface.
// HLSL equivalent of water_calm.vert + water_calm.frag.
//
// --- Why a Separate Calm Shader? ---
//
//  The inner water zone (surrounding the terrain) uses a flat surface with
//  undistorted reflections. This gives a clean, mirror-like lake appearance
//  while the outer ocean zone has waves and shimmer.
//
//  No wave displacement, no UV perturbation — just clean projective texturing.
//
// --- DX Texture Flip ---
//
//  reflUV.y = 1.0 - reflUV.y is needed because DirectX texture coordinates
//  have their origin at the TOP-LEFT, while our reflection camera assumes
//  BOTTOM-LEFT origin. This Y-flip corrects the orientation.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-resources-coordinates
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;               // Water quad positioning
    float4x4 uView;                // Camera view
    float4x4 uProjection;          // Perspective projection
    float4x4 uReflectVP;           // Reflected camera View×Projection
    float4   uColorTint;           // Base water color (dark blue-green)
    float    uReflectionStrength;  // Reflection blend factor (0=tint, 1=mirror)
    float3   _pad0;                // Cbuffer alignment padding
};

Texture2D    uReflectionTex : register(t1);  // Scene rendered from reflected camera
SamplerState sSampler1      : register(s1);

struct VS_IN
{
    float3 position : POSITION;  // Flat water grid vertex
};

struct VS_OUT
{
    float4 position       : SV_POSITION;
    float4 reflectClipPos : TEXCOORD0;   // Vertex in reflection camera's clip space
    float2 worldXZ        : TEXCOORD1;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // No wave displacement — calm water stays perfectly flat.
    float4 worldPos       = mul(uModel, float4(input.position, 1.0));
    output.position       = mul(uProjection, mul(uView, worldPos));
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ        = input.position.xz;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Projective texture mapping: clip → NDC → UV.
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y;  // DX texture Y-flip (top-left origin)
    // Sample undistorted reflection — perfect mirror.
    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    return lerp(uColorTint, reflection, uReflectionStrength);
}

