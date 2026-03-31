#version 330 core

// Shadow disc: vertex shader
// MVP transform with per-vertex alpha for shadow fade.

layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uAlpha;  // shadow opacity (0-1, fades with height)

out float vAlpha;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0);

    // Center vertex (first in fan) gets full alpha, edge vertices fade
    // Use vertex position to determine: center is at (0,0,0) in model space
    float distFromCenter = length(aPosition.xz);
    vAlpha = uAlpha * (1.0 - distFromCenter);
}
