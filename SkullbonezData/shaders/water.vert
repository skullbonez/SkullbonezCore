#version 330 core

// Water: vertex shader
// Generates subtle ripple animation via layered sine-wave Y displacement.

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform float uTime;

void main()
{
    vec3 pos = aPosition;
    pos.y += sin(pos.x * 0.04 + uTime * 1.2) * 1.5
           + sin(pos.z * 0.06 + uTime * 0.8) * 1.0;

    gl_Position = uProjection * uView * uModel * vec4(pos, 1.0);
}
