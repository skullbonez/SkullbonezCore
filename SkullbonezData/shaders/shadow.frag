#version 330 core

// Shadow quad: fragment shader
// Samples soft Gaussian shadow texture for smooth falloff.

in vec2 vTexCoord;
in float vAlpha;

uniform sampler2D uShadowTex;

out vec4 FragColor;

void main()
{
    float a = texture(uShadowTex, vTexCoord).r * vAlpha;
    FragColor = vec4(0.0, 0.0, 0.0, a);
}
