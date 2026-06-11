#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in mat4 aModel;
layout(location = 7) in vec4 aTint;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;

void main()
{
    vec4 worldPos = aModel * vec4(aPosition, 1.0);
    gl_Position = uProjection * uView * worldPos;
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
}
