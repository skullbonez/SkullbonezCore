#version 330 core

// =============================================================================
// TEXT FRAGMENT SHADER (text.frag)
// =============================================================================
//
// PURPOSE: Sample the font atlas texture and output colored text with alpha.
//
// The font atlas is a single-channel (RED only) texture where:
//   - White (1.0) = the glyph shape (where ink goes)
//   - Black (0.0) = empty space (transparent)
//
// We use the RED channel as the alpha value, so the glyph shape determines
// opacity while uTextColor sets the RGB color of the text.
//
// =============================================================================

in vec2 vTexCoord;
in vec3 vColor;  // Per-vertex color passed through from the vertex shader.

uniform sampler2D uFontTexture;

out vec4 FragColor;

void main()
{
    // The font atlas stores a Signed Distance Field (SDF):
    //   0.0  = far outside the glyph
    //   0.5  = on the glyph edge
    //   1.0  = deep inside the glyph
    //
    // fwidth() returns the screen-space rate-of-change of the SDF value.
    // This gives automatic adaptive anti-aliasing:
    //   - large text → small fwidth → narrow AA band → sharp edges
    //   - small text → large fwidth → wide AA band  → smooth, no aliasing
    //
    // Reference: https://www.khronos.org/opengl/wiki/Fragment_Shader#Special_operations
    float sdf   = texture(uFontTexture, vTexCoord).r;
    float fw    = fwidth(sdf) * 0.7;
    float alpha = smoothstep(0.5 - fw, 0.5 + fw, sdf);

    FragColor = vec4(vColor, alpha);
}
