/*
File: SkullbonezData/shaders/unlit_textured.hlsl
Purpose:
  Runs the unlit_textured HLSL shader program used by the renderer.

Summary:
  unlit_textured.hlsl is shader source for the renderer's unlit_textured pass.
  Keep edits anchored on shader inputs, bindings, and render-output contracts
  and on the glossary/invariants below.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must
  match this shader exactly.
*/
// =============================================================================
// UNLIT TEXTURED SHADER — Shader Model 6.6 (Combined VS+PS)
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

// Invariant: b1 carries stable indices into the directly indexed shader-visible
// heap; the pipeline owner writes all six root constants before every draw.
cbuffer BindlessTextureIndices : register(b1)
{
    uint4 _textureDescriptorIndices0;
    uint2 _textureDescriptorIndices1;
};

uint BindlessTextureIndex(uint slot)
{
    return slot < 4u ? _textureDescriptorIndices0[slot] : _textureDescriptorIndices1[slot - 4u];
}

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
    Texture2D<float4> textureSource = ResourceDescriptorHeap[BindlessTextureIndex(0u)];
    return textureSource.Sample(sSampler0, input.texCoord) * uColorTint;
}
