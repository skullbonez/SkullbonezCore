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
uniform int       uWaterMode;          // 1 = basin pool, 2 = full calm plane, 4 = stylized basin
uniform vec3      uSunColor;
uniform float     uSunGlintStrength;
uniform vec4      uBasinMask;          // center xz, radius xz for cinematic pool mask
uniform float     uBasinMaskFeather;

out vec4 FragColor;

void main()
{
    float basinMask = 1.0;
    float basinDistance = 0.0;
    vec2 basinOffset = vec2(0.0);
    if (uCinematicMode > 0.5 && (uWaterMode == 1 || uWaterMode == 4))
    {
        // Cinematic mode turns the calm water into an oval pool in the basin.
        // Outside the oval we discard pixels so the old broad water plane does
        // not cover the shot.
        basinOffset = (vWorldXZ - uBasinMask.xy) / max(uBasinMask.zw, vec2(1.0));
        basinDistance = length(basinOffset);
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
    if (uCinematicMode > 0.5 && uWaterMode == 4)
    {
        // Low-poly water: clean turquoise depth bands with a bright shoreline.
        // It keeps a tiny reflection contribution but avoids the mirror-like
        // grey sheet that fights the stylized terrain.
        float shore = smoothstep(0.54, 0.94, basinDistance);
        float angle = atan(basinOffset.y, basinOffset.x);
        float shard = floor(fract(angle * 1.90986 + basinDistance * 2.4 + 0.35) * 4.0) / 4.0;
        float depthBand = floor(clamp(1.0 - basinDistance, 0.0, 1.0) * 5.0 + shard * 0.45) / 5.0;
        vec3 deep = vec3(0.035, 0.22, 0.34);
        vec3 mid = vec3(0.08, 0.46, 0.56);
        vec3 shallow = vec3(0.42, 0.76, 0.66);
        waterColor = mix(deep, mid, 0.24 + depthBand * 0.54);
        waterColor = mix(waterColor, shallow, shore * 0.44);
        waterColor *= 0.90 + shard * 0.15;
        float wedge = fract(angle * 2.86479 + basinDistance * 0.30 + 0.5);
        float panelEdge = 1.0 - smoothstep(0.0, 0.040, min(wedge, 1.0 - wedge));
        waterColor = mix(waterColor, waterColor * vec3(0.58, 0.78, 0.90), panelEdge * 0.24);
        waterColor = mix(waterColor, reflection.rgb, min(uReflectionStrength, 0.16));
        float rimLine = smoothstep(0.70, 0.91, basinDistance) * (1.0 - smoothstep(0.94, 1.0, basinDistance));
        float innerRim = smoothstep(0.52, 0.72, basinDistance) * (1.0 - smoothstep(0.76, 0.88, basinDistance));
        waterColor += vec3(0.38, 0.42, 0.18) * rimLine;
        waterColor += vec3(0.08, 0.22, 0.18) * innerRim;
        float sunShard = pow(max(0.0, 1.0 - abs(reflUV.x - 0.64) * 6.0), 3.0) *
                         pow(max(0.0, 1.0 - abs(reflUV.y - 0.48) * 8.0), 2.0);
        waterColor += uSunColor * sunShard * 0.052;
    }
    if (uCinematicMode > 0.5)
    {
        // Add a fake sunset glint where the reflected sun column would land.
        // This is deliberately screen/reflection-space, so it is stable and easy
        // to tune without building a full physical water lighting model.
        float sunColumn = pow(max(0.0, 1.0 - abs(reflUV.x - 0.28) * 4.6), 3.0);
        float horizonLine = pow(max(0.0, 1.0 - abs(reflUV.y - 0.54) * 9.0), 2.0);
        float glint = sunColumn * horizonLine * uSunGlintStrength;
        if (uWaterMode != 4)
        {
            waterColor = mix(waterColor * vec3(0.72, 0.58, 0.42), vec3(0.58, 0.24, 0.065), 0.14);
        }
        waterColor += uSunColor * glint;
    }
    FragColor = vec4(waterColor, uColorTint.a * basinMask);
}
