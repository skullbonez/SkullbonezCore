#version 330 core

// =============================================================================
// CALM WATER FRAGMENT SHADER (water_calm.frag)
// =============================================================================
//
// PURPOSE: Sample the reflection texture and blend it with a base water tint.
// No UV perturbation — the reflection is a perfect mirror for the inner zone.
//
// --- Projective Texture Mapping ---
//
//  To find which pixel of the reflection texture to use for this fragment:
//  1. Take vReflectClipPos (position in reflection camera's clip space)
//  2. Perspective divide: xy / w → normalized device coordinates [-1, +1]
//  3. Remap to UV space: * 0.5 + 0.5 → texture coordinates [0, 1]
//
//  This maps world geometry to the correct reflection texture pixel.
//
// --- mix() Blending ---
//
//  mix(a, b, t) = a * (1-t) + b * t
//  So: mix(tint, reflection, 0.8) = 80% reflection + 20% water color.
//
// =============================================================================

in vec4 vReflectClipPos;

uniform vec4      uColorTint;          // Base water color (dark blue-green)
uniform sampler2D uReflectionTex;      // Texture containing the reflected scene
uniform float     uReflectionStrength; // 0=pure tint, 1=pure reflection
uniform int       uNoReflect;          // 1 = skip reflection, output flat tint

out vec4 FragColor;

void main()
{
    // When reflection is disabled, output flat tint to match ocean behaviour.
    if (uNoReflect != 0)
    {
        FragColor = uColorTint;
        return;
    }
    // Projective texture lookup: convert clip coords → UV coords [0,1].
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;
    // Sample the reflection texture at the computed UV.
    vec4 reflection = texture(uReflectionTex, reflUV);
    // Blend between base water color and reflection based on strength.
    FragColor = mix(uColorTint, reflection, uReflectionStrength);
}
