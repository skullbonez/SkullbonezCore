// =============================================================================
// INSTANCED SHADOW DISC SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render circular shadow decals beneath spheres using instanced rendering.
// HLSL equivalent of shadow.vert + shadow.frag.
//
// --- Quad-Based Decal Geometry ---
//
//  Each shadow is a single quad (-1..+1 in XZ), not a triangle fan.
//  The disc is cut out in the PIXEL SHADER by testing distance from centre:
//
//      if (length(input.uv) > 1.0) discard;
//
//  Benefits:
//   - Only 2 triangles (6 vertices) per shadow — previously 16 triangles
//   - Perfectly smooth circular fade (per-pixel, not per-vertex)
//   - No shadow_segments config option needed
//   - At 300 balls: saves 4,200 triangles per frame
//
// --- Instancing ---
//
//  - POSITION: quad vertex in [-1,1] XZ, Y=0 (shared 2-tri geometry)
//  - TEXCOORD1–4: per-instance model matrix (4 columns)
//  - TEXCOORD5: per-instance base alpha (height-based opacity)
//
//  The vertex XZ is passed through as uv so the pixel shader can
//  compute per-pixel radial distance from the disc centre.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/discard--sm4---asm-
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
    float3 position : POSITION;   // Per-vertex: quad geometry in [-1,1] XZ, Y=0
    float4 model0   : TEXCOORD1;  // Per-instance: model matrix column 0
    float4 model1   : TEXCOORD2;  // Per-instance: model matrix column 1
    float4 model2   : TEXCOORD3;  // Per-instance: model matrix column 2
    float4 model3   : TEXCOORD4;  // Per-instance: model matrix column 3
    float  alpha    : TEXCOORD5;  // Per-instance: base shadow opacity (0-1)
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Screen position for rasterizer
    float2 uv       : TEXCOORD0;    // XZ disc coords [-1,1] for per-pixel disc test
    float  alpha    : TEXCOORD1;    // Base alpha passed through unchanged
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // Reconstruct model matrix (same transpose trick as instanced sphere shader).
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(uProjection, mul(uView, worldPos));

    // Pass XZ through so the pixel shader can compute per-pixel distance from centre.
    output.uv    = input.position.xz;
    output.alpha = input.alpha;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float dist = length(input.uv);

    // Discard fragments outside the unit circle — turns the quad into a disc.
    if (dist > 1.0)
    {
        discard;
    }

    // Linear fade from centre (full opacity) to edge (transparent).
    float alpha = input.alpha * (1.0 - dist);

    // Pure black with variable alpha = darkens whatever is underneath via alpha blending.
    return float4(0.0, 0.0, 0.0, alpha);
}

