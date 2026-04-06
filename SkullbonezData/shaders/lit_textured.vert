#version 330 core

// Phong lighting + texture: vertex shader
// Passes view-space position, normal, and texcoord to fragment for per-pixel lighting.
// uClipPlane is a world-space clip plane (Ax+By+Cz+D >= 0 keeps the fragment).
// Default (0,1,0,1e9) always passes — enable GL_CLIP_DISTANCE0 only when needed.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;    // world-space clip plane; default (0,1,0,1e9) = always pass

out vec3 vViewPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main()
{
    mat4 modelView = uView * uModel;
    vec4 viewPos   = modelView * vec4(aPosition, 1.0);
    gl_Position    = uProjection * viewPos;

    // Clip distance: positive = keep, negative = discard (when GL_CLIP_DISTANCE0 enabled)
    gl_ClipDistance[0] = dot(uModel * vec4(aPosition, 1.0), uClipPlane);

    vViewPos  = viewPos.xyz;
    vNormal   = transpose(inverse(mat3(modelView))) * aNormal;
    vTexCoord = aTexCoord;
}
