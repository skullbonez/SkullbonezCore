#version 330 core

// =============================================================================
// DEBUG LINE VERTEX SHADER (debug_line.vert)
// =============================================================================
//
// PURPOSE: Draw debug visualization lines in 3D world space.
// Used in development for rendering bounding sphere wireframes, velocity vectors,
// collision normals, and other diagnostic visualizations.
//
// --- How Debug Lines Work ---
//
//  The CPU builds a list of line segment endpoints (pairs of 3D positions)
//  and uploads them to the GPU each frame. This shader transforms them from
//  world space to screen space using the combined View-Projection matrix.
//
//  Example — bounding sphere wireframe:
//
//     .---.        Each circle is made of many short line segments.
//    /     \       The CPU generates pairs of points along the circle
//   |   o   |     and streams them to the GPU.
//    \     /
//     '---'
//
// --- Why uViewProj instead of separate uView + uProjection? ---
//
//  Since debug lines have no model transform (they're already in world space),
//  we can pre-multiply View × Projection on the CPU and send one matrix.
//  Saves one matrix multiply per vertex on the GPU (minor optimization).
//
// =============================================================================

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;

void main()
{
    // Transform world-space position directly to clip space (no model transform needed).
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
