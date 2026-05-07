#version 330 core

// =============================================================================
// UNLIT TEXTURED VERTEX SHADER (unlit_textured.vert)
// =============================================================================
//
// PURPOSE: Simple MVP transform with texture coordinates, NO lighting.
// Used for the skybox (which should appear uniformly bright regardless of
// light direction — the sky texture already has lighting "baked in").
//
// --- Why No Lighting for Skybox? ---
//
//  The skybox is a giant box around the entire scene with sky/cloud textures.
//  It represents infinitely distant scenery, so:
//  - No lighting needed (the photo already has correct brightness)
//  - No normals needed (flat surfaces, no shading variation)
//  - Just sample the texture and display it as-is
//
// =============================================================================

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 vTexCoord;

void main()
{
    // Standard Model-View-Projection transform.
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
    // Pass texture coordinates to fragment shader unchanged.
    vTexCoord   = aTexCoord;
}
