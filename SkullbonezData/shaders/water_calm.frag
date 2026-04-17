#version 330 core

// Calm (inner) water: fragment shader
// Flat reflection, no UV perturbation.

in vec4 vReflectClipPos;

uniform vec4      uColorTint;
uniform sampler2D uReflectionTex;
uniform float     uReflectionStrength;

out vec4 FragColor;

void main()
{
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;
    vec4 reflection = texture(uReflectionTex, reflUV);
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
