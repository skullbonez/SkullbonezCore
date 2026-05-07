// =============================================================================
// INSTANCED SHADOW DISC SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render circular shadow decals beneath spheres using instanced rendering.
// HLSL equivalent of shadow.vert + shadow.frag.
//
// --- How Shadow Instancing Works in DX ---
//
//  Same concept as GL instancing:
//  - Shared geometry: a disc made of triangles (radius 1, Y=0)
//  - Per-instance data: model matrix (position/scale) + alpha (shadow opacity)
//  - The input assembler advances per-instance data every disc-worth of vertices
//
// --- Alpha Calculation ---
//
//  shadow_alpha = instance_alpha × (1 - distance_from_center)
//
//  This creates a soft circular falloff:
//  - Center of disc: full opacity (dark shadow)
//  - Edge of disc: zero opacity (invisible fade-out)
//  - instance_alpha accounts for sphere height (higher = fainter shadow)
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uView;        // Camera view matrix
    float4x4 uProjection;  // Perspective projection matrix
};

struct VS_IN
{
    float3 position : POSITION;   // Per-vertex: disc geometry (unit radius, XZ plane)
    float4 model0   : TEXCOORD1;  // Per-instance: model matrix column 0
    float4 model1   : TEXCOORD2;  // Per-instance: model matrix column 1
    float4 model2   : TEXCOORD3;  // Per-instance: model matrix column 2
    float4 model3   : TEXCOORD4;  // Per-instance: model matrix column 3
    float  alpha    : TEXCOORD5;  // Per-instance: base shadow opacity (0-1)
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Screen position for rasterizer
    float  alpha    : TEXCOORD0;    // Interpolated alpha for pixel shader
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Reconstruct model matrix (same transpose trick as instanced sphere shader).
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(uProjection, mul(uView, worldPos));

    // Radial fade: center=full alpha, edge=zero alpha.
    float distFromCenter = length(input.position.xz);
    output.alpha = input.alpha * (1.0 - distFromCenter);

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Pure black with variable alpha = darkens whatever is underneath via alpha blending.
    return float4(0.0, 0.0, 0.0, input.alpha);
}

