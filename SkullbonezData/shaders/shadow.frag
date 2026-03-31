#version 330 core

// Shadow disc: fragment shader
// Flat black with alpha for shadow transparency.

in float vAlpha;

out vec4 FragColor;

void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, vAlpha);
}
