#version 330 core

// Unlit textured: vertex shader
// MVP transform only, no lighting. Used for skybox, water.

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec2 vTexCoord;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);
    vTexCoord   = aTexCoord;
}
