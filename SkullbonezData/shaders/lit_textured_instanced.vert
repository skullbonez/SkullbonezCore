#version 330 core

// Phong lighting + texture: instanced vertex shader
// Per-instance model matrix arrives via vertex attributes (divisor=1).
// View, projection, lighting, and clip plane are shared via uniforms.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in mat4 aModel;          // per-instance model matrix (locations 3-6)

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;
uniform vec4 uLightPosition;

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    mat4 modelView = uView * aModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    gl_ClipDistance[0] = dot(aModel * vec4(aPosition, 1.0), uClipPlane);

    vViewPos  = viewPos.xyz;
    vNormal   = transpose(inverse(mat3(modelView))) * aNormal;
    vTexCoord = aTexCoord;
}
