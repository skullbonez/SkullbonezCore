#version 330 core

// =============================================================================
// OCEAN WATER FRAGMENT SHADER (water_ocean.frag)
// =============================================================================
//
// PURPOSE: Sample the reflection texture with UV PERTURBATION to create a
// shimmering, distorted reflection effect on the ocean surface.
//
// --- UV Perturbation (Shimmer Effect) ---
//
//  After computing the base reflection UV (projective texture mapping, same as
//  calm water), we OFFSET the UV by an amount based on the wave function:
//
//  Without perturbation:     With perturbation:
//  +--reflection--+          +--reflection--+
//  |  clean image |          |  ~~~~wavy~~~~|
//  |  like mirror |          |  ~~distorted~|
//  +--------------+          +--------------+
//
//  The wave function uses the SAME sine formula as the vertex displacement,
//  meaning the shimmer aligns with the wave geometry (crests shimmer more).
//
// --- uNoReflect Fallback ---
//
//  When rendering the reflection pass itself (to avoid infinite recursion),
//  water renders as flat color with no reflection lookup.
//
// =============================================================================

in vec4 vReflectClipPos;
in vec2 vWorldXZ;

uniform vec4      uColorTint;          // Base water color (fallback + blend target)
uniform sampler2D uReflectionTex;      // FBO texture with reflected scene
uniform float     uReflectionStrength; // How much reflection vs tint (0-1)
uniform float     uTime;              // Animation time (syncs with vertex shader waves)
uniform float     uWaveHeight;        // Amplitude (controls perturbation magnitude)
uniform float     uPerturbStrength;   // Multiplier for UV offset intensity
uniform int       uNoReflect;         // 1 = skip reflection (used during reflection pass)

out vec4 FragColor;

void main()
{
    // During the reflection render pass, output flat color to avoid recursion.
    if (uNoReflect != 0)
    {
        FragColor = uColorTint;
        return;
    }

    // Projective texture mapping: clip coords → UV [0,1].
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;

    // Compute wave displacement at this XZ position (same formula as vertex shader).
    // This value is used to PERTURB the UV, creating shimmer that tracks the waves.
    float wave = sin(vWorldXZ.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(vWorldXZ.y * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    // Offset UV by wave-proportional amount (bigger waves = more distortion).
    reflUV += vec2(wave * uPerturbStrength, wave * uPerturbStrength);

    // Sample the reflection texture at the perturbed UV coordinate.
    vec4 reflection = texture(uReflectionTex, reflUV);
    // Blend water tint with reflection.
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
