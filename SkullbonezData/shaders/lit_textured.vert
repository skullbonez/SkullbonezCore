#version 330 core

// Gouraud lighting + texture: vertex shader
// Computes per-vertex lighting in view space, passes color + texcoord to fragment

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// Light properties (directional light in world space)
uniform vec3 uLightDirection;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;
uniform vec4 uLightSpecular;

// Material properties
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;
uniform vec4 uMaterialSpecular;
uniform float uMaterialShininess;

out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    mat4 modelView = uView * uModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    // Transform normal to view space (using inverse transpose of modelView upper 3x3)
    mat3 normalMatrix = transpose(inverse(mat3(modelView)));
    vec3 N = normalize(normalMatrix * aNormal);

    // Light direction in view space
    vec3 L = normalize(mat3(uView) * (-uLightDirection));

    // View direction (camera is at origin in view space)
    vec3 V = normalize(-viewPos.xyz);

    // Ambient
    vec4 ambient = uLightAmbient * uMaterialAmbient;

    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec4 diffuse = uLightDiffuse * uMaterialDiffuse * diff;

    // Specular (Blinn-Phong)
    vec3 H = normalize(L + V);
    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), uMaterialShininess) : 0.0;
    vec4 specular = uLightSpecular * uMaterialSpecular * spec;

    vColor    = ambient + diffuse + specular;
    vTexCoord = aTexCoord;
}
