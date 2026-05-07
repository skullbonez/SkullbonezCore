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
// --- _pad0 Field ---
//
//  HLSL cbuffers must be 16-byte aligned. float3 uTextColor is 12 bytes,
//  so we add a 4-byte padding float to fill the 16-byte boundary.
//  This is a common DX pattern — GLSL handles alignment automatically.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;  // Orthographic projection (pixel coords → clip space)
    float3   uTextColor;   // RGB text color (e.g., white, green for FPS)
    float    _pad0;        // Padding to meet 16-byte cbuffer alignment requirement
};

Texture2D    uFontTexture : register(t0);  // Font atlas (single-channel RED = glyph alpha)
SamplerState sSampler0    : register(s0);  // Linear filtering for smooth text edges

struct VS_IN
{
    float2 position : POSITION;   // 2D screen position (pixel coordinates)
    float2 texCoord : TEXCOORD0;  // UV into font atlas (points at the character cell)
};

struct VS_OUT
{
    float4 position : SV_POSITION;  // Screen-space output for rasterizer
    float2 texCoord : TEXCOORD0;    // UV for pixel shader to sample
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    // Orthographic projection: maps pixel coordinates directly to clip space.
    // Z=0 (flat on screen), W=1 (no perspective division needed).
    output.position = mul(uProjection, float4(input.position, 0.0, 1.0));
    output.texCoord = input.texCoord;
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    // Sample RED channel — this is the glyph's alpha mask (1=ink, 0=empty).
    float alpha = uFontTexture.Sample(sSampler0, input.texCoord).r;
    // Output user-chosen text color with glyph-shaped transparency.
    return float4(uTextColor, alpha);
}

