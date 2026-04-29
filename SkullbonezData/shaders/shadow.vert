#version 330 core

// Shadow quad: vertex shader
// Instanced — per-instance model matrix (locations 3-6) and alpha (location 7).
// Passes UV to fragment for soft shadow texture sampling.

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aTexCoord;

// Per-instance attributes
layout(location = 3) in vec4 aModelCol0;
layout(location = 4) in vec4 aModelCol1;
layout(location = 5) in vec4 aModelCol2;
layout(location = 6) in vec4 aModelCol3;
layout(location = 7) in float aAlpha;

uniform mat4 uView;
uniform mat4 uProjection;

out vec2 vTexCoord;
out float vAlpha;

void main()
{
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    gl_Position = uProjection * uView * model * vec4(aPosition, 1.0);
    vTexCoord = aTexCoord;
    vAlpha = aAlpha;
}
