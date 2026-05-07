#version 330 core

// =============================================================================
// DEBUG LINE FRAGMENT SHADER (debug_line.frag)
// =============================================================================
//
// PURPOSE: Output a flat color for debug lines. No lighting, no textures.
// The color is set per draw call (all lines in a batch share one color).
//
// Typical colors used:
//  - Green: bounding spheres
//  - Red: collision contacts
//  - Yellow: velocity vectors
//  - Cyan: terrain normals
//
// =============================================================================

uniform vec4 uColor;

out vec4 fragColor;

void main()
{
    // Every pixel of the line gets the same flat color.
    fragColor = uColor;
}
