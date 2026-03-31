#version 330 core

// Phong lighting + texture: vertex shader
// Passes view-space position, normal, and texcoord to fragment for per-pixel lighting

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    mat4 modelView = uView * uModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    vViewPos  = viewPos.xyz;
    vNormal   = transpose(inverse(mat3(modelView))) * aNormal;
    vTexCoord = aTexCoord;
}
