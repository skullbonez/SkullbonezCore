#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 3) in mat4 aModel;
layout(location = 7) in vec4 aColor;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;

out vec3 vViewPos;
out vec3 vNormal;
out vec4 vColor;

void main()
{
    mat4 modelView = uView * aModel;
    vec4 viewPos = modelView * vec4(aPosition, 1.0);
    gl_Position = uProjection * viewPos;
    gl_ClipDistance[0] = dot(aModel * vec4(aPosition, 1.0), uClipPlane);

    vViewPos = viewPos.xyz;
    vNormal = transpose(inverse(mat3(modelView))) * aNormal;
    vColor = aColor;
}
