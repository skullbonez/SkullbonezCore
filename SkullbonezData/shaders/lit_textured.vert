#version 330 core

// =============================================================================
// LIT TEXTURED VERTEX SHADER (lit_textured.vert)
// =============================================================================
//
// PURPOSE: Transform each vertex from 3D model space → screen space, and prepare
// data for per-pixel lighting in the fragment shader.
//
// --- What this shader does in the rendering pipeline ---
//
//  3D Model Space       View (Camera) Space         Screen Space (2D)
//  (object local)       (camera-relative)           (final pixels)
//      |                       |                         |
//      |   uModel matrix       |   uView matrix          |  uProjection matrix
//      |   (position/rotate    |   (camera look-at)      |  (perspective/FOV)
//      |    the object)        |                         |
//      v                       v                         v
//  [aPosition] -----> [world pos] -----> [viewPos] -----> [gl_Position]
//
// The vertex shader runs ONCE PER VERTEX (e.g. 7500 times for a 25×25 sphere).
// Its job is to:
//   1. Transform position: model space → clip space (gl_Position)
//   2. Transform normal: rotate to match camera orientation (for lighting)
//   3. Pass texture coordinates through unchanged
//   4. Calculate clip distance (for water reflection clipping)
//
// --- Clip Distance (Water Reflection) ---
//
//  When rendering the reflection pass for water, we need to clip everything
//  BELOW the water surface. The clip plane (0, 1, 0, -waterHeight) means:
//  "keep vertices where dot(worldPos, plane) >= 0" (i.e. above the water).
//
//       Camera
//         |
//   ------+------------ water surface (y = waterHeight)
//         |
//    (CLIPPED - discarded by GPU when gl_ClipDistance[0] < 0)
//
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;    // world-space clip plane; default (0,1,0,1e9) = always pass

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    // Combine model and view matrices, then transform vertex position into view space.
    // View space = "as seen from the camera's perspective" (camera is at origin, looking down -Z).
    mat4 modelView = uView * uModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);

    // Apply the projection matrix to get clip space coordinates.
    // The GPU uses this to determine where on the 2D screen this vertex appears.
    // Perspective projection makes distant objects appear smaller (vanishing point effect).
    gl_Position    = uProjection * viewPos;

    // Clip distance: the GPU discards fragments where this is negative.
    // We transform the vertex to world space and dot it with the clip plane equation.
    // Positive = above the plane (keep), negative = below the plane (discard).
    gl_ClipDistance[0] = dot(uModel * vec4(aPosition, 1.0), uClipPlane);

    // Pass view-space position to fragment shader (used for light direction calculation).
    vViewPos  = viewPos.xyz;

    // Transform the normal vector. We use the "normal matrix" (transpose of inverse of
    // the model-view matrix's upper 3x3). This correctly handles non-uniform scaling —
    // if an object is squished, its normals must rotate differently than its vertices.
    vNormal   = transpose(inverse(mat3(modelView))) * aNormal;

    // Texture coordinates pass through unchanged — they're defined in the mesh data.
    vTexCoord = aTexCoord;
}
