#version 330 core

// Debug line shader — draws world-space line segments.
// Each vertex is a vec3 world position.

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;

void main()
{
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
