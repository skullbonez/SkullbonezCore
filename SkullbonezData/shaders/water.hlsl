// =============================================================================
// WATER SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render the main water surface with sine-wave animation and planar
// reflection. HLSL equivalent of water.vert + water.frag.
//
// --- Water Rendering Architecture (DX11/DX12) ---
//
//  Same multi-pass approach as the GL version:
//
//  Pass 1: Render scene from REFLECTED camera (flipped below water)
//          → captured into uReflectionTex (a render-target texture)
//
//  Pass 2: Render water surface using THIS shader:
//          - Vertex shader animates Y with sine waves
//          - Pixel shader samples reflection texture with projective mapping
//          - UV perturbation creates shimmering distortion
//
// --- DX vs GL Texture Coordinate Difference ---
//
//  CRITICAL: DX textures have origin at TOP-LEFT (V=0 at top)
//            GL textures have origin at BOTTOM-LEFT (V=0 at bottom)
//
//  This means we must flip V: reflUV.y = 1.0 - reflUV.y
//  Without this flip, reflections would appear upside-down on DX.
//
// --- lerp() vs mix() ---
//
//  HLSL uses lerp(a, b, t) where GLSL uses mix(a, b, t) — same function,
//  different name. Both compute: a * (1-t) + b * t.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-intrinsic-functions
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;               // Water quad world transform
    float4x4 uView;                // Camera view matrix
    float4x4 uProjection;          // Perspective projection
    float4x4 uReflectVP;           // Reflected camera's View×Projection (for texture mapping)
    float4   uColorTint;           // Base water color (blended with reflection)
    float    uTime;                // Elapsed time (drives wave animation)
    float    uReflectionStrength;  // 0=pure tint, 1=pure reflection
    int      uFlatWater;           // 1=suppress waves (debug toggle)
    int      uNoReflect;           // 1=skip reflection (used during reflection pass itself)
    int      uNoPerturb;           // 1=skip UV perturbation (debug toggle)
    float3   _pad0;                // Padding for 16-byte cbuffer alignment
};

Texture2D    uReflectionTex : register(t1);  // Reflection FBO texture
SamplerState sSampler1      : register(s1);  // Sampler for reflection texture

struct VS_IN
{
    float3 position : POSITION;  // Water mesh vertex (flat grid in XZ plane)
};

struct VS_OUT
{
    float4 position        : SV_POSITION;  // Screen position
    float4 reflectClipPos  : TEXCOORD0;    // Position in reflection camera clip space
    float2 worldXZ         : TEXCOORD1;    // World XZ for fragment UV perturbation
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    float3 pos = input.position;
    // Animate Y position with layered sine waves (if not debug-flattened).
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * 1.5
               + sin(pos.z * 0.06 + uTime * 0.8) * 1.0;
    }

    // Transform displaced position through MVP for screen placement.
    float4 worldPos    = mul(uModel, float4(pos, 1.0));
    output.position    = mul(uProjection, mul(uView, worldPos));
    // Project ORIGINAL position through reflection camera (shimmer comes from frag).
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ     = input.position.xz;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // During reflection pass, return flat color to prevent infinite recursion.
    if (uNoReflect != 0)
    {
        return uColorTint;
    }

    // Projective texture mapping: clip coords → UV [0,1].
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    // FLIP V for DX texture coordinate convention (origin at top-left, not bottom-left).
    reflUV.y = 1.0 - reflUV.y;

    // UV perturbation: offset reflection lookup based on wave function for shimmer.
    if (uNoPerturb == 0)
    {
        float wave = sin(input.worldXZ.x * 0.04 + uTime * 1.2) * 1.5
                   + sin(input.worldXZ.y * 0.06 + uTime * 0.8) * 1.0;
        reflUV += float2(wave * 0.002, wave * 0.002);
    }

    // Sample reflection texture and blend with base water tint.
    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    return lerp(uColorTint, reflection, uReflectionStrength);
}

