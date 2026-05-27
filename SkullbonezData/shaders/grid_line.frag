#version 330 core

// =============================================================================
// GRID LINE FRAGMENT SHADER (grid_line.frag)
// =============================================================================
//
// PURPOSE: Output interpolated per-vertex color for grid lines.
// No lighting or textures — the color is determined entirely by the CPU based
// on the cell's occupancy/collision state and passed through from the vertex shader.
//
// Color meanings:
//  - White (1,1,1):  cell is empty
//  - Yellow (1,1,0): ball just entered this cell (fading to blue)
//  - Blue (0,0,1):   cell is occupied (steady state)
//  - Red (1,0,0):    active collision (darkens toward black with intensity)
//
// =============================================================================

in vec3 vColor;

out vec4 fragColor;

void main()
{
    // Output the interpolated vertex color at full opacity.
    fragColor = vec4(vColor, 1.0);
}
