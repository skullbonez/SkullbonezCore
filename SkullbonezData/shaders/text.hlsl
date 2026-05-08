// =============================================================================
// TEXT RENDERING SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render 2D text quads with a font atlas texture.
// HLSL equivalent of text.vert + text.frag.
//
// --- How It Works ---
//
//  Same concept as the GLSL version:
//  1. CPU generates quads (4 vertices each) for each character
//  2. Each quad's UV coordinates point at the character's cell in the font atlas
//  3. Orthographic projection positions quads at exact pixel coordinates
//  4. Font atlas RED channel = alpha mask (glyph shape)
//
// --- Per-vertex color ---
//
//  Previously uTextColor was a per-draw-call cbuffer uniform, requiring one
//  draw call per distinct string color.  Color is now baked into the vertex
//  stream so the entire frame's text (all colors) is uploaded and drawn in a
//  single call.  The cbuffer therefore only needs the projection matrix.
//
//  The dynamic VB layer maps vertex attributes to HLSL semantics as:
//    attrib[0] → POSITION    attrib[1] → TEXCOORD0    attrib[2] → TEXCOORD1
//  so the color float3 uses TEXCOORD1 (not COLOR) to match that convention.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;  // Orthographic projection (pixel coords → clip space)
};

Texture2D    uFontTexture : register(t0);  // Font atlas (single-channel RED = glyph alpha)
SamplerState sSampler0    : register(s0);  // Linear filtering for smooth text edges

struct VS_IN
{
    float2 position : POSITION;   // 2D screen position
    float2 texCoord : TEXCOORD0;  // UV into font atlas
    float3 color    : TEXCOORD1;  // Per-vertex RGB — baked at batch-build time
                                  // (DX dynamic VB maps attrib[2] to TEXCOORD1)
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Screen-space output for rasterizer
    float2 texCoord : TEXCOORD0;    // UV for pixel shader to sample
    float3 color    : TEXCOORD1;    // Forwarded to pixel shader
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Orthographic projection: maps pixel coordinates directly to clip space.
    // Z=0 (flat on screen), W=1 (no perspective division needed).
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    output.color    = input.color;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Sample RED channel — this is the glyph's alpha mask (1=ink, 0=empty).
    float alpha = uFontTexture.Sample(sSampler0, input.texCoord).r;
    // Output per-vertex text color with glyph-shaped transparency.
    return float4(input.color, alpha);
}

