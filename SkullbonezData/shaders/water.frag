#version 330 core

// =============================================================================
// WATER FRAGMENT SHADER (water.frag)
// =============================================================================
//
// PURPOSE: Blend a deep-ocean base color with the reflection texture to create
// a realistic water surface appearance.
//
// --- Projective Texturing (Reflection Sampling) ---
//
//  The reflection was rendered into an FBO from a camera BELOW the water looking UP.
//  To sample the correct texel for each water pixel, we use "projective texturing":
//
//  1. The vertex shader gives us vReflectClipPos (this pixel's position in the
//     reflection camera's clip space: range [-w, +w])
//  2. We divide by W (perspective divide) to get NDC: range [-1, +1]
//  3. We remap to UV: range [0, 1] with  uv = ndc * 0.5 + 0.5
//
//  Clip Space (-w to +w) → NDC (-1 to +1) → Texture UV (0 to 1)
//       ÷ w                    × 0.5 + 0.5
//
// --- UV Perturbation (Wave Shimmer) ---
//
//  To make the reflection "shimmer" with the waves, we perturb the UV coordinates
//  using the same sine functions as the vertex displacement. This makes the
//  reflection distort in sync with the water surface geometry.
//
//  Without perturbation: reflection looks like a perfect mirror (unrealistic)
//  With perturbation:    reflection wobbles like real water
//
// =============================================================================

in vec4 vReflectClipPos;
in vec2 vWorldXZ;

uniform vec4      uColorTint;
uniform sampler2D uReflectionTex;
uniform float     uReflectionStrength;
uniform float     uTime;
uniform int       uNoReflect;   // 1 = flat colour only, no reflection sample (debug key 2)
uniform int       uNoPerturb;   // 1 = disable UV wave perturbation (debug key 4)

out vec4 FragColor;

void main()
{
    // Debug mode: just output flat water color (no reflection texture sampling).
    if (uNoReflect != 0)
    {
        FragColor = uColorTint;
        return;
    }

    // Projective UV: convert clip-space position → texture coordinates (0 to 1).
    // The perspective divide (÷ w) and remap (× 0.5 + 0.5) convert from the reflection
    // camera's view into the FBO texture's coordinate space.
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;

    // Perturb UV with sine waves to simulate water surface distortion of the reflection.
    // The frequencies and phases match the vertex displacement in water.vert, so the
    // shimmer is phase-locked to the actual surface geometry.
    if (uNoPerturb == 0)
    {
        float waveX = sin(vWorldXZ.x * 0.04 + uTime * 1.2) * 0.006;
        float waveZ = sin(vWorldXZ.y * 0.06 + uTime * 0.8) * 0.004;
        reflUV += vec2(waveX, waveZ);
    }

    // Sample the reflection FBO texture at the perturbed UV.
    vec4 reflection = texture(uReflectionTex, reflUV);

    // mix(a, b, t) = a×(1-t) + b×t. Blends between the base water color (uColorTint)
    // and the reflection. uReflectionStrength controls how mirror-like the water is.
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
