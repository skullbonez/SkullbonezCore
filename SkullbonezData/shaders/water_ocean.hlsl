/*
File: SkullbonezData/shaders/water_ocean.hlsl
Purpose:
  Runs the water_ocean HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.

Related:
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// OCEAN WATER SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Animate the OUTER water zone with sine waves and shimmering reflection.
// This is the canonical DX12 ocean-water shader.
//
// --- Wave + Shimmer Architecture ---
//
//  Vertex Shader:
//  - Pushes vertices up/down with layered sine waves (ocean swell)
//  - Passes original (un-displaced) position for reflection coordinates
//
//  Pixel Shader:
//  - Computes reflection UV via projective texture mapping
//  - Applies DX Y-flip (top-left texture origin)
//  - Perturbs UV based on the same wave formula (shimmer tracks geometry)
//  - Blends reflection with base water tint
//
// --- uFlatWater / uNoReflect Debug Toggles ---
//
//  uFlatWater=1: Suppress wave displacement (flat surface for debugging)
//  uNoReflect=1: Skip reflection sampling (used during the reflection render pass
//                to avoid infinite recursion — water reflecting water reflecting...)
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;               // Water quad world transform
    float4x4 uView;                // Camera view
    float4x4 uProjection;          // Perspective projection
    float4x4 uReflectVP;           // Reflected camera View×Projection
    float4   uColorTint;           // Base water color (dark ocean blue)
    float    uTime;                // Animation time in seconds
    float    uWaveHeight;          // Wave amplitude (from engine.cfg)
    float    uReflectionStrength;  // Reflection blend factor (0-1)
    float    uWaterFresnelF0;      // Base reflectance for ordinary water
    float3   uCameraWorld;         // Camera position for view-angle reflection
    float    uPerturbStrength;     // UV offset multiplier (controls shimmer intensity)
    int      uFlatWater;           // 1=no waves (debug)
    int      uNoReflect;           // 1=flat color output (reflection pass)
    float    uCinematicMode;       // 1 = warm sunset response
    float    uSunGlintStrength;
    float3   uSunColor;
    float    _pad0;
};

Texture2D    uReflectionTex : register(t1);
SamplerState sSampler1      : register(s1);

struct VS_IN
{
    float3 position : POSITION;  // Water grid vertex (flat XZ plane)
};

struct VS_OUT
{
    float4 position       : SV_POSITION;
    float4 reflectClipPos : TEXCOORD0;  // Original pos in reflection camera space
    float2 worldXZ        : TEXCOORD1;  // XZ for fragment-shader wave calc
    float3 worldPos       : TEXCOORD2;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    float3 pos = input.position;
    // Animate Y with layered sine waves for rolling ocean effect.
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(pos.z * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    }

    float4 worldPos       = mul(uModel, float4(pos, 1.0));
    output.position       = mul(uProjection, mul(uView, worldPos));
    // Reflection uses un-displaced position (shimmer added in pixel shader).
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ        = input.position.xz;
    output.worldPos       = worldPos.xyz;

    return output;
}

float OrdinaryWaterReflectance(float3 worldPos, float3 normal)
{
    float3 V = normalize(uCameraWorld - worldPos);
    float cosTheta = saturate(dot(normalize(normal), V));
    float x = 1.0f - cosTheta;
    float fresnel = uWaterFresnelF0 + (1.0f - uWaterFresnelF0) * x * x * x * x * x;
    return saturate(uReflectionStrength * (0.22f + fresnel * 1.78f));
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // During reflection pass, output flat color (prevents infinite recursion).
    if (uNoReflect != 0)
    {
        return uColorTint;
    }

    // Projective texture mapping: clip space → UV [0,1].
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y;  // DX Y-flip (texture origin is top-left)

    // UV perturbation: wave function offsets the UV for a shimmering effect.
    float wave = sin(input.worldXZ.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(input.worldXZ.y * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    reflUV += float2(wave * uPerturbStrength, wave * uPerturbStrength);

    // Sample perturbed reflection and blend with base tint.
    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    float3 dx = ddx(input.worldPos);
    float3 dy = ddy(input.worldPos);
    float3 waterN = normalize(cross(dy, dx));
    if (waterN.y < 0.0f)
    {
        waterN = -waterN;
    }
    float reflectBlend = (uCinematicMode > 0.5f)
                           ? uReflectionStrength
                           : OrdinaryWaterReflectance(input.worldPos, waterN);
    float3 waterColor = lerp(uColorTint.rgb, reflection.rgb, reflectBlend);
    if (uCinematicMode > 0.5f)
    {
        // Cinematic ocean is currently skipped by the C++ path, but keeping this
        // branch documented makes the shader ready if we later re-enable distant
        // water behind the basin.
        float sunColumn = pow(max(0.0f, 1.0f - abs(reflUV.x - 0.28f) * 4.2f), 3.0f);
        float shimmer = 0.55f + 0.45f * sin((input.worldXZ.x + input.worldXZ.y) * 0.15f + uTime * 2.0f);
        float glint = sunColumn * shimmer * uSunGlintStrength;
        waterColor = lerp(waterColor * float3(0.70f, 0.54f, 0.40f), float3(0.52f, 0.20f, 0.06f), 0.12f);
        waterColor += uSunColor * glint;
    }
    return float4(waterColor, uColorTint.a);
}
