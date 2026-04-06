#version 330 core

// Water: fragment shader
// Deep ocean blue, semi-transparent.

uniform vec4 uColorTint;

out vec4 FragColor;

void main()
{
    FragColor = uColorTint;
}
