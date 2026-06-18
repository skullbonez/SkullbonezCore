/*
File: SkullbonezData/shaders/solid_color.hlsl
Purpose:
  Runs the solid_color HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
  HLSL (High Level Shader Language): Shader language compiled for Direct3D
  render, compute, and raytracing stages.
  DX12 (DirectX 12): Production renderer API that owns this shader's root
  signature, input layout, and descriptor bindings.
  HUD (Heads-Up Display): On-screen diagnostics and control overlay.
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
// SOLID COLOR SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render flat-colored 2D quads for HUD backgrounds.
// This is the canonical DX12 solid-color shader.
//
// Used to draw semi-transparent rectangles behind text panels so the text
// is always readable regardless of the 3D scene behind it.
//
// Typical uColor: (0, 0, 0, 0.7) = black at 70% opacity.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;  // Orthographic projection (pixel coords → clip space)
    float4   uColor;       // Flat RGBA color for the entire quad
};

struct VS_IN
{
    float2 position : POSITION;  // 2D screen position in pixel coordinates
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Clip-space output
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Orthographic projection: pixel coordinates → clip space, Z=0, W=1.
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Every pixel in the quad gets the exact same color (alpha blending does the rest).
    return uColor;
}
