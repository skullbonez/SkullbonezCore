#version 330 core

// Full-screen volumetric-light vertex shader.
//
// The interesting work happens in post_volumetric_light.frag. This tiny vertex
// shader simply draws the screen-sized rectangle that lets the fragment shader
// run once for each pixel of the half-resolution light texture.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
