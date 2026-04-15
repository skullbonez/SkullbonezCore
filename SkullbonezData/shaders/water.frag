#version 330 core

// Water: fragment shader
// Blends deep ocean blue with a planar reflection texture.
// Reflection UV is derived from clip-space position (projective texturing) and
// perturbed by the same wave functions used in water.vert, so the reflected
// image shimmers in phase with the surface geometry.

in vec4 vReflectClipPos;
in vec2 vWorldXZ;

uniform vec4      uColorTint;
uniform sampler2D uReflectionTex;
uniform float     uReflectionStrength;
uniform float     uTime;
uniform int       uFlatShading;    // 1 = flat colour only, skip reflection (debug key 2)

out vec4 FragColor;

void main()
{
    if (uFlatShading != 0)
    {
        FragColor = uColorTint;
        return;
    }

    // Projective UV: sample the reflection FBO using the reflection camera's clip space.
    // This ensures the UV tracks the reflection camera exactly, with no double-motion.
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;

    // Perturb UV with the same wave functions as water.vert — phase-locks
    // the reflected image shimmer to the surface ripple geometry
    float wave = sin(vWorldXZ.x * 0.04 + uTime * 1.2) * 1.5
               + sin(vWorldXZ.y * 0.06 + uTime * 0.8) * 1.0;
    reflUV += vec2(wave * 0.002, wave * 0.002);

    vec4 reflection = texture(uReflectionTex, reflUV);
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
