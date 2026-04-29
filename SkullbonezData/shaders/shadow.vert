#version 330 core

// Instanced shadow decal: vertex shader
// Static disc geometry drawn once per instance via glDrawArraysInstanced.
// Per-instance model matrix and alpha arrive via vertex attributes (divisor=1).

layout(location = 0) in vec3 aPosition;      // disc vertex (unit radius, XZ plane)
layout(location = 3) in mat4 aModel;          // per-instance model matrix (locations 3-6)
layout(location = 7) in float aAlpha;         // per-instance shadow opacity

uniform mat4 uView;
uniform mat4 uProjection;

out float vAlpha;

void main()
{
    gl_Position = uProjection * uView * aModel * vec4(aPosition, 1.0);

    // Center vertex (0,0,0) gets full alpha; edge vertices fade to zero
    float distFromCenter = length(aPosition.xz);
    vAlpha = aAlpha * (1.0 - distFromCenter);
}
