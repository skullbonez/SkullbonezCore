#version 330 core

// Gouraud lighting + texture: vertex shader
// Computes per-vertex lighting in view space, passes color + texcoord to fragment

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// Light properties (position in view space, w=0 directional, w=1 positional)
uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;

// Material properties
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    mat4 modelView = uView * uModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    // Transform normal to view space
    mat3 normalMatrix = transpose(inverse(mat3(modelView)));
    vec3 N = normalize(normalMatrix * aNormal);

    // Light direction in view space
    vec3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - viewPos.xyz);

    // Ambient
    vec4 ambient = uLightAmbient * uMaterialAmbient;

    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec4 diffuse = uLightDiffuse * uMaterialDiffuse * diff;

    vColor    = ambient + diffuse;
    vTexCoord = aTexCoord;
}
