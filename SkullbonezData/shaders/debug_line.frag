#version 330 core

// Debug line shader — flat colour output.

uniform vec4 uColor;

out vec4 fragColor;

void main()
{
    fragColor = uColor;
}
