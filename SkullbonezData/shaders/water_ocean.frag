#version 330 core

// Ocean (outer) water: fragment shader
// Reflection with UV perturbation — shimmer phase-locked to vertex wave displacement.

in vec4 vReflectClipPos;
in vec2 vWorldXZ;

uniform vec4      uColorTint;
uniform sampler2D uReflectionTex;
uniform float     uReflectionStrength;
uniform float     uTime;
uniform float     uWaveHeight;
uniform float     uPerturbStrength;
uniform int       uNoReflect;

out vec4 FragColor;

void main()
{
    if (uNoReflect != 0)
    {
        FragColor = uColorTint;
        return;
    }

    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;

    float wave = sin(vWorldXZ.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(vWorldXZ.y * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    reflUV += vec2(wave * uPerturbStrength, wave * uPerturbStrength);

    vec4 reflection = texture(uReflectionTex, reflUV);
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
