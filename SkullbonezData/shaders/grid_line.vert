#version 330 core

// =============================================================================
// GRID LINE VERTEX SHADER (grid_line.vert)
// =============================================================================
//
// PURPOSE: Draw per-vertex colored line segments in 3D world space.
// Used by the broadphase spatial grid visualizer to render cell boundaries
// with colors indicating occupancy and collision state.
//
// --- Vertex Layout ---
//
//  Each vertex has 6 floats: [x, y, z, r, g, b]
//  - Position (vec3): world-space endpoint of a line segment
//  - Color (vec3): RGB color for this vertex (interpolated along the line)
//
// --- How the Grid Overlay Works ---
//
//  The spatial grid divides 3D space into uniform cubic cells. Each cell
//  boundary is drawn as a wireframe cube (12 edges = 24 vertices in GL_LINES).
//
//  Cell colors encode state:
//  - White:  empty (no objects in this cell)
//  - Yellow: ball just entered (fading to blue over 0.5s)
//  - Blue:   occupied (steady state)
//  - Red:    active collision (deepens toward black with collision count)
//
//     +-----+-----+-----+
//     |white|blue |white|
//     +-----+-----+-----+
//     |blue | RED |blue |      RED = collision happening in this cell
//     +-----+-----+-----+
//     |white|blue |white|
//     +-----+-----+-----+
//
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProj;

out vec3 vColor;

void main()
{
    // Transform world-space position to clip space.
    gl_Position = uViewProj * vec4(aPosition, 1.0);

    // Pass per-vertex color to fragment shader for interpolation.
    vColor = aColor;
}
