#version 330 core

// =============================================================================
// SOLID COLOR FRAGMENT SHADER (solid_color.frag)
// =============================================================================
//
// PURPOSE: Output a single uniform color for every pixel.
// No texture, no lighting — just flat color. Used for HUD background quads
// (e.g., semi-transparent black panels behind text).
//
// uColor is typically something like (0.0, 0.0, 0.0, 0.7) meaning
// "black at 70% opacity" — the alpha blending makes the 3D scene
// partially visible through the panel.
//
// =============================================================================

uniform vec4 uColor;

out vec4 FragColor;

void main()
{
    // Every pixel in this quad gets the exact same color.
    FragColor = uColor;
}
