/*
File: SkullbonezData/shaders/unlit_textured.hlsl
Purpose:
  Runs the unlit_textured HLSL shader program used by the renderer.

Mental model:
  Shaders are GPU programs. Constant buffers provide per-frame data, shader
  stages transform or shade inputs, and CPU-side renderer bindings must match
  the declarations in this file.

Glossary:
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
// UNLIT TEXTURED SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Simple MVP transform + texture sample, no lighting. Used for skybox.
// This is the canonical DX12 unlit textured shader.
//
// The skybox texture already contains "baked" lighting (it's a photograph of
// the sky), so we don't apply any Phong lighting calculations.
//
// --- uColorTint ---
//
//  Multiplied with the texture color to allow runtime color adjustment.
//  Default (1,1,1,1) = unmodified texture. The engine could use this for
//  day/night cycles, fog tinting, etc.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;       // Object-to-world transform (skybox cube positioning)
    float4x4 uView;        // Camera view matrix
    float4x4 uProjection;  // Perspective projection matrix
    float4   uColorTint;   // Color multiplier (default 1,1,1,1 = no tint)
};

Texture2D    uTexture  : register(t0);  // The skybox face texture
SamplerState sSampler0 : register(s0);  // Texture filtering settings

struct VS_IN
{
    float3 position : POSITION;   // Vertex position on skybox cube
    float2 texCoord : TEXCOORD0;  // UV coordinate into the face texture
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Screen-space position for rasterizer
    float2 texCoord : TEXCOORD0;    // UV passed to pixel shader
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Standard Model → View → Projection pipeline.
    float4 worldPos = mul(uModel, float4(input.position, 1.0));
    float4 viewPos  = mul(uView, worldPos);
    output.position = mul(uProjection, viewPos);
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Sample texture and apply color tint. No lighting calculations.
    return uTexture.Sample(sSampler0, input.texCoord) * uColorTint;
}
