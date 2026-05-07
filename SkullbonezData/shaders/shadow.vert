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
//  Each shadow disc is a unit-radius circle in the XZ plane (Y=0), scaled and
//  positioned by its per-instance model matrix to sit just above the terrain.
//
// --- Instancing ---
//
//  Like the sphere shader, this uses instanced rendering:
//  - Location 0: disc vertex position (shared static geometry)
//  - Locations 3-6: per-instance model matrix (positions each shadow on terrain)
//  - Location 7: per-instance alpha (shadow opacity — fades with height above ground)
//
// --- Edge Fade ---
//
//  The shadow fades out toward the edges for a soft, natural look:
//  center (0,0) = full opacity; edge (radius=1) = fully transparent.
//
// =============================================================================

layout(location = 0) in vec3 aPosition;      // disc vertex (unit radius, XZ plane)
layout(location = 3) in mat4 aModel;          // per-instance model matrix (locations 3-6)
layout(location = 7) in float aAlpha;         // per-instance shadow opacity

uniform mat4 uView;
uniform mat4 uProjection;

out float vAlpha;

void main()
{
    gl_Position = uProjection * uView * aModel * vec4(aPosition, 1.0);

    // Radial fade: vertices at the center (distance=0) get full alpha,
    // vertices at the edge (distance=1) get zero alpha.
    float distFromCenter = length(aPosition.xz);
    vAlpha = aAlpha * (1.0 - distFromCenter);
}
