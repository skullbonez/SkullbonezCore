#version 330 core

// Water: vertex shader
// Generates subtle ripple animation via layered sine-wave Y displacement.
// vReflectClipPos is the water vertex position in the reflection camera's clip
// space — used in the fragment shader for correct projective UV sampling of the
// reflection FBO. vWorldXZ drives wave-phase perturbation in the fragment shader.

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
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * 1.5
               + sin(pos.z * 0.06 + uTime * 0.8) * 1.0;
    }

    gl_Position    = uProjection * uView * uModel * vec4(pos, 1.0);
    // Use undisplaced aPosition for the reflection UV projection — the reflection FBO was
    // captured for the flat water plane, so projecting the wave-displaced pos would shift
    // the UV and distort the reflection as the surface animates.  The fragment shader's
    // small UV perturbation (wave * 0.002) handles the shimmer effect separately.
    vReflectClipPos = uReflectVP * uModel * vec4(aPosition, 1.0);
    vWorldXZ       = aPosition.xz;  // undisplaced so wave phase matches vertex displacement
}
