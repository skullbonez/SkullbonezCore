#version 330 core

// Solid color: vertex shader
// 2D orthographic projection for HUD background quads.

layout(location = 0) in vec2 aPosition;

uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
