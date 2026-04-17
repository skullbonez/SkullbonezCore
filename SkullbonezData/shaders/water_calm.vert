#version 330 core

// Calm (inner) water: vertex shader
// No Y displacement — surface stays perfectly flat for clean reflection.

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform mat4  uReflectVP;

out vec4 vReflectClipPos;
out vec2 vWorldXZ;

void main()
{
    gl_Position    = uProjection * uView * uModel * vec4(aPosition, 1.0);
    vReflectClipPos = uReflectVP * uModel * vec4(aPosition, 1.0);
    vWorldXZ       = aPosition.xz;
}
