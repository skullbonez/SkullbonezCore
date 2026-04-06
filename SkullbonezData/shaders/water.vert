#version 330 core

// Water: vertex shader
// Generates subtle ripple animation via layered sine-wave Y displacement.
// vClipPos and vWorldXZ are passed to the fragment shader for projective reflection UV
// and wave-phase perturbation, keeping the reflected shimmer phase-locked to the geometry.

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform float uTime;

out vec4 vClipPos;   // clip-space position for projective reflection UV
out vec2 vWorldXZ;   // undisplaced world XZ for wave-phase UV perturbation

void main()
{
    vec3 pos = aPosition;
    pos.y += sin(pos.x * 0.04 + uTime * 1.2) * 1.5
           + sin(pos.z * 0.06 + uTime * 0.8) * 1.0;

    gl_Position = uProjection * uView * uModel * vec4(pos, 1.0);

    vClipPos  = gl_Position;
    vWorldXZ  = aPosition.xz;  // undisplaced so wave phase matches vertex displacement
}
