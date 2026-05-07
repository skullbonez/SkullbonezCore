#version 330 core

// =============================================================================
// UNLIT TEXTURED FRAGMENT SHADER (unlit_textured.frag)
// =============================================================================
//
// PURPOSE: Output the texture color directly with an optional tint.
// No lighting calculations — the texture appears exactly as stored.
//
// --- texture() Function ---
//
//  texture(sampler, uv) samples the 2D texture at the given UV coordinate.
//  UV coordinates range from (0,0) at bottom-left to (1,1) at top-right.
//  The GPU handles filtering (smooth interpolation between texels) automatically
//  based on the texture's filter settings (GL_LINEAR/GL_NEAREST).
//
// --- Color Tint ---
//
//  Multiplying by uColorTint allows runtime coloring without changing the texture.
//  Default (1,1,1,1) = no tint. Setting (0.5,0.5,1,1) would make everything blue-ish.
//
// =============================================================================

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uColorTint;  // default (1,1,1,1) = no tint

out vec4 FragColor;

void main()
{
    FragColor = texture(uTexture, vTexCoord) * uColorTint;
}
