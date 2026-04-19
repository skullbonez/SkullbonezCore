#version 330 core

layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec4 vertexColor;

void main()
{
  vertexColor = color;
  vec4 worldPos = model * vec4(position, 0.0, 1.0);
  vec4 viewPos = view * worldPos;
  gl_Position = projection * viewPos;
}
