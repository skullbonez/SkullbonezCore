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

uniform sampler2D uFontTexture;
uniform vec3 uTextColor;

out vec4 FragColor;

void main()
{
    // Sample the RED channel of the font atlas — this gives us the glyph's alpha mask.
    float alpha = texture(uFontTexture, vTexCoord).r;

    // Output: user-chosen text color with glyph-shaped alpha (transparent between letters).
    FragColor = vec4(uTextColor, alpha);
}
