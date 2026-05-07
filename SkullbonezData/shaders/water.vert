#version 330 core

// =============================================================================
// WATER VERTEX SHADER (water.vert)
// =============================================================================
//
// PURPOSE: Animate the water surface with layered sine waves and prepare data
// for reflection texture sampling in the fragment shader.
//
// --- Water Rendering Architecture ---
//
//  Pass 1: Reflection                 Pass 2: Main Scene
//  (camera flipped below water)       (normal camera)
//  +---------------------------+      +---------------------------+
//  | Render scene upside-down  |      | Render terrain, spheres   |
//  | into FBO texture          |      | then draw water plane     |
//  |                           |      | sampling the FBO texture  |
//  +---------------------------+      +---------------------------+
//        |                                     ^
//        | FBO color texture                   |
//        +-----> water.frag samples this ------+
//
// --- Sine Wave Vertex Displacement ---
//
//  Flat water plane          After displacement
//  ___________________       ~~~~~/\~~~~~/\~~~~~
//  |_|_|_|_|_|_|_|_|        each vertex moves up/down based on sin(x + time)
//
//  Two sine waves with different frequencies create organic-looking ripples:
//    wave1: frequency 0.04 along X, speed 1.2, amplitude 1.5
//    wave2: frequency 0.06 along Z, speed 0.8, amplitude 1.0
//
// --- Reflection UV via Projective Texturing ---
//
//  The reflection FBO was rendered from a "mirror camera" below the water.
//  To correctly map the reflection texture onto the water surface, we project
//  each water vertex through the mirror camera's view-projection matrix.
//  This gives us clip-space coordinates which we convert to texture UVs (0-1).
//
// =============================================================================

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform mat4  uReflectVP;   // reflection camera view-projection
uniform float uTime;
uniform int   uFlatWater;   // 1 = fully flat mesh, no displacement (debug key 3)

out vec4 vReflectClipPos;   // clip-space position from reflection camera
out vec2 vWorldXZ;          // undisplaced world XZ for wave-phase UV perturbation

void main()
{
    vec3 pos = aPosition;

    // Animate vertex Y position with layered sine waves (unless flat water debug mode).
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * 1.5
               + sin(pos.z * 0.06 + uTime * 0.8) * 1.0;
    }

    // Standard MVP transform for the animated position.
    gl_Position    = uProjection * uView * uModel * vec4(pos, 1.0);

    // Project the UNDISPLACED position through the reflection camera.
    // Using undisplaced ensures the reflection UV stays anchored to the flat water plane.
    // The wave shimmer effect is applied later in the fragment shader via UV perturbation.
    vReflectClipPos = uReflectVP * uModel * vec4(aPosition, 1.0);
    vWorldXZ       = aPosition.xz;
}
