#version 330 core

// =============================================================================
// SHADOW DISC VERTEX SHADER (shadow.vert)
// =============================================================================
//
// PURPOSE: Render circular shadow "decals" beneath each sphere using instancing.
//
// --- Shadow Technique ---
//
//  Instead of complex shadow mapping (rendering from the light's perspective),
//  this engine uses simple shadow DISCS — flat dark circles projected onto the ground
//  beneath each object. Cheap, effective, and physically plausible for overhead lighting.
//
// --- Quad-Based Decal Geometry ---
//
//  Each shadow is a single quad (-1..+1 in XZ), not a triangle fan.
//  The disc shape is cut out entirely in the FRAGMENT shader by testing
//  whether the pixel's UV coordinate falls inside a unit circle:
//
//      if (length(vUV) > 1.0) discard;
//
//  This means:
//   - Only 2 triangles (6 vertices) per shadow regardless of disc quality
//   - Perfectly smooth circular fade (per-pixel, not per-vertex interpolation)
//   - No need for a segment count config option
//
//  Previously this was a 16-segment triangle fan = 16 triangles per shadow.
//  At 300 balls: 16 → 2 triangles saves 4,200 triangles per frame.
//
// --- Instancing ---
//
//  - Location 0: quad vertex position (shared static 2-triangle geometry)
//  - Locations 3-6: per-instance model matrix (positions/scales each shadow on terrain)
//  - Location 7: per-instance alpha (shadow opacity — fades with height above ground)
//
//  The vertex's XZ position is passed through as vUV so the fragment shader
//  can compute per-pixel radial distance from the disc centre.
//
// =============================================================================

layout(location = 0) in vec3 aPosition;      // quad vertex in [-1,1] XZ plane, Y=0
layout(location = 3) in mat4 aModel;          // per-instance model matrix (locations 3-6)
layout(location = 7) in float aAlpha;         // per-instance base shadow opacity

uniform mat4 uView;
uniform mat4 uProjection;

out vec2  vUV;       // XZ coords in [-1,1] — used by fragment shader for disc test
out float vAlpha;    // base alpha passed through unchanged; fragment applies radial fade

void main()
{
    vec4 worldPos = aModel * vec4( aPosition, 1.0 );
    gl_Position = uProjection * uView * worldPos;

    // Pass XZ through so the fragment shader can compute distance from centre per-pixel.
    vUV    = aPosition.xz;
    vAlpha = aAlpha;
}
