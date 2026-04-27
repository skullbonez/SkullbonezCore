#version 330 core

// Solid color: fragment shader
// Outputs a flat RGBA color (used for HUD background quads).

uniform vec4 uColor;

out vec4 FragColor;

void main()
{
    FragColor = uColor;
}
