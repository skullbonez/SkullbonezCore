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
in vec2 vWorldXZ;

uniform vec4      uColorTint;          // Base water color (dark blue-green)
uniform sampler2D uReflectionTex;      // Texture containing the reflected scene
uniform float     uReflectionStrength; // 0=pure tint, 1=pure reflection
uniform int       uNoReflect;          // 1 = skip reflection, output flat tint
uniform float     uCinematicMode;      // 1 = warm sunset response
uniform vec3      uSunColor;
uniform float     uSunGlintStrength;
uniform vec4      uBasinMask;          // center xz, radius xz for cinematic pool mask
uniform float     uBasinMaskFeather;

out vec4 FragColor;

void main()
{
    float basinMask = 1.0;
    if (uCinematicMode > 0.5)
    {
        // Cinematic mode turns the calm water into an oval pool in the basin.
        // Outside the oval we discard pixels so the old broad water plane does
        // not cover the shot.
        vec2 basinOffset = (vWorldXZ - uBasinMask.xy) / max(uBasinMask.zw, vec2(1.0));
        float basinDistance = length(basinOffset);
        basinMask = 1.0 - smoothstep(max(0.0, 1.0 - uBasinMaskFeather), 1.0, basinDistance);
        if (basinMask <= 0.01)
        {
            discard;
        }
    }

    // When reflection is disabled, output flat tint to match ocean behaviour.
    if (uNoReflect != 0)
    {
        FragColor = vec4(uColorTint.rgb, uColorTint.a * basinMask);
        return;
    }
    // Projective texture lookup: convert clip coords → UV coords [0,1].
    vec2 reflUV = (vReflectClipPos.xy / vReflectClipPos.w) * 0.5 + 0.5;
    // Sample the reflection texture at the computed UV.
    vec4 reflection = texture(uReflectionTex, reflUV);
    // Blend between base water color and reflection based on strength.
    vec3 waterColor = mix(uColorTint.rgb, reflection.rgb, uReflectionStrength);
    if (uCinematicMode > 0.5)
    {
        // Add a fake sunset glint where the reflected sun column would land.
        // This is deliberately screen/reflection-space, so it is stable and easy
        // to tune without building a full physical water lighting model.
        float sunColumn = pow(max(0.0, 1.0 - abs(reflUV.x - 0.28) * 4.6), 3.0);
        float horizonLine = pow(max(0.0, 1.0 - abs(reflUV.y - 0.54) * 9.0), 2.0);
        float glint = sunColumn * horizonLine * uSunGlintStrength;
        waterColor = mix(waterColor * vec3(0.72, 0.58, 0.42), vec3(0.58, 0.24, 0.065), 0.14);
        waterColor += uSunColor * glint;
    }
    FragColor = vec4(waterColor, uColorTint.a * basinMask);
}
