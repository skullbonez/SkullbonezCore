#version 330 core

// =============================================================================
// CALM WATER VERTEX SHADER (water_calm.vert)
// =============================================================================
//
// PURPOSE: Render the INNER water surface (near the terrain center) as a
// perfectly flat plane — no wave displacement.
//
// --- Why Two Water Shaders? ---
//
//  The engine has two water zones:
//
//  Top-down view:
//  +--------------------------------------+
//  |  OCEAN (outer) — waves, shimmer      |
//  |  +----------------------------+      |
//  |  | CALM (inner) — flat mirror |      |
//  |  |  [terrain sits here]       |      |
//  |  +----------------------------+      |
//  +--------------------------------------+
//
//  The inner zone is calm to give a clean mirror reflection of the terrain
//  and spheres. The outer zone has waves so the distant ocean looks alive.
//
// --- Reflection Coordinates ---
//
//  vReflectClipPos is the fragment's position as seen from the "reflection camera"
//  (a camera flipped below the water surface). The fragment shader uses this to
//  sample the reflection texture at the correct spot.
//
// =============================================================================

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform mat4  uReflectVP;    // View-Projection matrix for the mirrored camera

out vec4 vReflectClipPos;
out vec2 vWorldXZ;

void main()
{
    // Standard MVP transform — positions the flat water quad on screen.
    gl_Position    = uProjection * uView * uModel * vec4(aPosition, 1.0);
    // Project this vertex through the REFLECTION camera for texture lookup.
    vReflectClipPos = uReflectVP * uModel * vec4(aPosition, 1.0);
    // Pass XZ world position (unused in calm shader but keeps interface consistent).
    vWorldXZ       = aPosition.xz;
}
