#version 330 core

// Text rendering: vertex shader
// 2D orthographic projection for bitmap font quads.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uProjection;

out vec2 vTexCoord;

void main()
{
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord   = aTexCoord;
}
