// =============================================================================
// SOLID COLOR BATCH SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render 2D quads where each quad carries its own RGBA colour, batched
// into a single draw call for the entire profiler bar overlay.
//
// --- Motivation ---
//
//  solid_color.hlsl uses a per-draw-call cbuffer colour, so one draw call is
//  required per distinct colour.  For the profiler bar overlay this results in
//  30+ draw calls per frame.  This shader bakes RGBA into the vertex stream so
//  one upload + one draw covers every bar segment, background, and legend swatch.
//
// --- Vertex layout ---
//
//  The dynamic VB layer maps vertex attributes to HLSL semantics sequentially:
//    attrib[0] → POSITION     (vec2 — 2D screen position)
//    attrib[1] → TEXCOORD0    (vec4 — RGBA, reusing TEXCOORD0 slot for float4)
//
// --- Conventions ---
//
//  Positions are in the same frustum-unit space as text.hlsl and solid_color.hlsl.
//  uProjection is the same orthographic matrix shared across all 2D overlays.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;  // Orthographic projection (pixel coords → clip space)
};

struct VS_IN
{
    float2 position : POSITION;   // 2D screen position in frustum-unit space
    float4 color    : TEXCOORD0;  // Per-vertex RGBA baked at batch-build time
                                  // (DX dynamic VB maps attrib[1] → TEXCOORD0)
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Clip-space output for rasteriser
    float4 color    : TEXCOORD0;    // RGBA forwarded to pixel shader
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Orthographic projection: Z = 0 (flat on screen), W = 1 (no perspective).
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    output.color    = input.color;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Output the interpolated per-vertex colour.  Because all four corners of
    // each axis-aligned quad carry the same colour, every pixel in the quad is
    // the same flat colour — alpha blending handles transparency.
    return input.color;
}
