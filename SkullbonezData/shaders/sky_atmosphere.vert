#version 330 core

// Full-screen sky vertex shader.
//
// The sky is procedural, so there is no skybox mesh here. We draw one rectangle
// over the screen and let sky_atmosphere.frag decide the color for each pixel.

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
