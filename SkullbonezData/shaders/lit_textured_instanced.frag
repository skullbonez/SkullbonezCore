#version 330 core

// =============================================================================
// INSTANCED LIT TEXTURED FRAGMENT SHADER (lit_textured_instanced.frag)
// =============================================================================
//
// PURPOSE: Apply Phong lighting and texture to instanced geometry (spheres).
// This shader is IDENTICAL to lit_textured.frag — the instancing difference
// is only in the vertex shader (which reads per-instance model matrices).
//
// See lit_textured.frag for the full Phong lighting explanation.
// The key steps are repeated briefly here:
//
//  1. Compute light direction (directional or point light)
//  2. Ambient = constant minimum illumination
//  3. Diffuse = brightness based on angle between surface and light
//  4. Specular = shiny highlight where light reflects toward camera
//  5. Multiply by texture color for final result
//
// =============================================================================

uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;
uniform sampler2D uTexture;
uniform sampler2D uShadowMap;
uniform mat4 uShadowViewProj;
uniform vec4 uShadowParams;
uniform vec4 uShadowFlags;
uniform int uObjectStyle;

in vec3 vViewPos;
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vTexCoord;
in vec4 vTint;

out vec4 FragColor;

float ShadowVisibility(vec3 worldPos, vec3 normalView, vec3 lightView)
{
    if (uShadowFlags.x < 0.5 || uShadowFlags.y < 0.5 || uShadowParams.x <= 0.0)
    {
        return 1.0;
    }

    vec4 shadowClip = uShadowViewProj * vec4(worldPos, 1.0);
    if (shadowClip.w <= 0.0)
    {
        return 1.0;
    }

    vec3 ndc = shadowClip.xyz / shadowClip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uShadowFlags.w > 0.5)
    {
        uv.y = 1.0 - uv.y;
    }
    float receiverDepth = uShadowFlags.w > 0.5 ? ndc.z : ndc.z * 0.5 + 0.5;

    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || receiverDepth <= 0.0 || receiverDepth >= 1.0)
    {
        return 1.0;
    }

    float ndotl = max(dot(normalize(normalView), normalize(lightView)), 0.0);
    float bias = uShadowParams.y + uShadowParams.z * (1.0 - ndotl);
    int radius = int(floor(uShadowFlags.z + 0.5));
    float texel = max(uShadowParams.w, 0.00001);
    float visible = 0.0;
    float samples = 0.0;
    for (int y = -3; y <= 3; ++y)
    {
        if (abs(y) > radius)
        {
            continue;
        }
        for (int x = -3; x <= 3; ++x)
        {
            if (abs(x) > radius)
            {
                continue;
            }
            float shadowDepth = textureLod(uShadowMap, uv + vec2(x, y) * texel, 0.0).r;
            visible += receiverDepth - bias <= shadowDepth ? 1.0 : 0.0;
            samples += 1.0;
        }
    }

    float visibility = samples > 0.0 ? visible / samples : 1.0;
    return mix(1.0 - uShadowParams.x, 1.0, visibility);
}

vec3 ProceduralBeachBallColor(vec2 uv)
{
    // The original source texture is intentionally low-res, which becomes blurry
    // when a ball is close to the camera. The beachball material now uses mesh
    // UVs plus shader math for the red/yellow panels in every render mode, so the
    // edges stay razor crisp at any resolution.
    uv = fract(uv);

    // uv.x is the wrap around the object. Multiplying by 2 creates two vertical
    // panels around the circumference. uv.y chooses the top/bottom half, and the
    // hemisphere offset flips the color order so the total object reads as two
    // red panels and two yellow panels, not a repeated 4-and-4 pattern.
    float longitudePanel = floor(uv.x * 2.0);
    float hemisphere = step(0.5, uv.y);
    float redPanel = mod(longitudePanel + hemisphere, 2.0);
    vec3 yellow = vec3(1.0, 0.78, 0.0);
    vec3 red = vec3(0.96, 0.06, 0.0);
    vec3 color = mix(yellow, red, redPanel);

    // Add a narrow warm seam on panel borders. It is still generated from UVs,
    // so it stays sharp and does not depend on the texture image resolution.
    float panelCoord = fract(uv.x * 2.0);
    float seamU = min(panelCoord, 1.0 - panelCoord);
    float seamV = abs(uv.y - 0.5);
    float seam = 1.0 - smoothstep(0.0, 0.014, min(seamU, seamV));
    return mix(color, vec3(1.0, 0.62, 0.02), seam * 0.28);
}

vec3 QuantizedLowPolyNormal(vec3 N)
{
    vec3 qN = floor(N * 2.5 + 0.5) / 2.5;
    return normalize(mix(N, qN, 0.88));
}

float LowPolySunBand(float lightAmount)
{
    return lightAmount > 0.72 ? 1.0 : (lightAmount > 0.34 ? 0.62 : 0.30);
}

vec3 ApplyMaterialMode(int mode, vec3 materialColor, vec3 N, vec3 V, vec3 L, vec3 lightColor, float diff, float spec)
{
    vec3 R = reflect(-L, N);
    if (mode == 2)
    {
        float metalSpec = pow(max(dot(V, R), 0.0), 140.0);
        vec3 reflected = mix(vec3(0.10, 0.12, 0.14), lightColor * 1.4, metalSpec);
        return materialColor * (0.12 + diff * 0.32) + reflected * (0.35 + metalSpec * 0.75);
    }
    if (mode == 3)
    {
        float pulse = 0.70 + 0.30 * sin(vTexCoord.x * 24.0 + vTexCoord.y * 18.0);
        return materialColor * (1.5 + pulse * 1.2) + lightColor * spec * 0.22;
    }
    if (mode == 4)
    {
        float fresnel = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.6);
        return materialColor * (0.18 + diff * 0.22) + lightColor * (fresnel * 0.85 + spec * 0.48);
    }
    if (mode == 5)
    {
        float bands = diff > 0.72 ? 1.0 : (diff > 0.34 ? 0.58 : 0.24);
        float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0);
        return materialColor * (0.18 + bands * 0.95) + lightColor * rim * 0.16;
    }
    if (mode == 6)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 skyAmbient = vec3(0.34, 0.46, 0.72);
        vec3 groundAmbient = vec3(0.34, 0.24, 0.13);
        vec3 hemiAmbient = mix(groundAmbient, skyAmbient, hemiT);
        vec3 poster = mix(materialColor, floor(materialColor * 4.0 + 0.5) / 4.0, 0.35);
        float rim = pow(1.0 - clamp(dot(qN, V), 0.0, 1.0), 2.2);
        vec3 warmSun = lightColor * vec3(1.03, 0.92, 0.68);
        return poster * (hemiAmbient * 0.50 + warmSun * (0.22 + sunBand * 0.42)) + warmSun * rim * 0.055;
    }
    if (mode == 8)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        float leafTier = floor(clamp(vTexCoord.y + qN.y * 0.22, 0.0, 1.0) * 4.0) / 4.0;
        vec3 topLeaf = mix(materialColor, vec3(0.50, 0.66, 0.18), 0.42);
        vec3 underside = mix(materialColor * vec3(0.40, 0.58, 0.34), vec3(0.06, 0.22, 0.06), 0.36);
        vec3 leaf = mix(underside, topLeaf, hemiT);
        leaf *= 0.78 + leafTier * 0.20 + sunBand * 0.10;
        vec3 skyAmbient = vec3(0.26, 0.38, 0.52);
        vec3 groundAmbient = vec3(0.14, 0.24, 0.07);
        vec3 hemiAmbient = mix(groundAmbient, skyAmbient, hemiT);
        vec3 warmSun = lightColor * vec3(1.02, 0.88, 0.54);
        float rim = pow(1.0 - clamp(dot(qN, V), 0.0, 1.0), 2.1);
        return leaf * (hemiAmbient * 0.52 + warmSun * (0.18 + sunBand * 0.38)) + warmSun * rim * 0.034;
    }
    if (mode == 13)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float qDiff = max(dot(qN, L), 0.0);
        float sunBand = LowPolySunBand(qDiff);
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        float heightBand = floor(clamp(vTexCoord.y, 0.0, 1.0) * 3.0) / 3.0;
        vec3 litNeedle = mix(materialColor, vec3(0.48, 0.64, 0.18), 0.38);
        vec3 shadowNeedle = mix(materialColor * vec3(0.36, 0.54, 0.34), vec3(0.040, 0.16, 0.045), 0.42);
        vec3 leaf = mix(shadowNeedle, litNeedle, hemiT);
        leaf *= 0.74 + heightBand * 0.16 + sunBand * 0.18;
        vec3 skyAmbient = vec3(0.22, 0.33, 0.48);
        vec3 groundAmbient = vec3(0.10, 0.18, 0.07);
        vec3 hemiAmbient = mix(groundAmbient, skyAmbient, hemiT);
        vec3 warmSun = lightColor * vec3(1.02, 0.86, 0.50);
        float rim = pow(1.0 - clamp(dot(qN, V), 0.0, 1.0), 2.0);
        return leaf * (hemiAmbient * 0.58 + warmSun * (0.12 + sunBand * 0.36)) + warmSun * rim * 0.024;
    }
    if (mode == 9)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0));
        float grain = 0.5 + 0.5 * sin(vTexCoord.x * 24.0 + vTexCoord.y * 7.0);
        vec3 bark = mix(materialColor * vec3(0.72, 0.58, 0.42), materialColor * vec3(1.22, 0.96, 0.62) + vec3(0.05, 0.025, 0.0), grain * 0.28);
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 hemiAmbient = mix(vec3(0.30, 0.20, 0.11), vec3(0.44, 0.44, 0.32), hemiT);
        vec3 warmSun = lightColor * vec3(1.02, 0.86, 0.55);
        return bark * (hemiAmbient * 0.46 + warmSun * (0.14 + sunBand * 0.34));
    }
    if (mode == 10)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0));
        float facet = floor(max(qN.y, 0.0) * 4.0) / 4.0;
        vec3 stone = mix(materialColor * vec3(0.90, 0.94, 1.04), vec3(0.44, 0.48, 0.56), 0.42);
        stone *= 0.68 + facet * 0.26 + sunBand * 0.05;
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 hemiAmbient = mix(vec3(0.22, 0.22, 0.26), vec3(0.46, 0.50, 0.56), hemiT);
        vec3 warmSun = lightColor * vec3(0.86, 0.78, 0.58);
        float rim = pow(1.0 - clamp(dot(qN, V), 0.0, 1.0), 2.4);
        return stone * (hemiAmbient * 0.54 + warmSun * (0.12 + sunBand * 0.24)) + warmSun * rim * 0.014;
    }
    if (mode == 11)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0));
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 ridge = mix(materialColor, vec3(0.70, 0.76, 0.48), 0.38);
        ridge *= 0.82 + floor(max(qN.y, 0.0) * 3.0) * 0.08;
        vec3 hazeAmbient = mix(vec3(0.40, 0.44, 0.30), vec3(0.62, 0.70, 0.66), hemiT);
        vec3 warmSun = lightColor * vec3(0.88, 0.80, 0.58);
        return ridge * (hazeAmbient * 0.58 + warmSun * (0.10 + sunBand * 0.20));
    }
    if (mode == 12)
    {
        vec3 qN = QuantizedLowPolyNormal(N);
        float sunBand = LowPolySunBand(max(dot(qN, L), 0.0));
        float band = floor(clamp(vTexCoord.y, 0.0, 1.0) * 3.0) / 3.0;
        vec3 sand = mix(materialColor, vec3(0.84, 0.76, 0.48), 0.44);
        sand *= 0.86 + band * 0.12;
        float hemiT = clamp(qN.y * 0.5 + 0.5, 0.0, 1.0);
        vec3 hemiAmbient = mix(vec3(0.42, 0.34, 0.20), vec3(0.62, 0.66, 0.52), hemiT);
        vec3 warmSun = lightColor * vec3(1.06, 0.94, 0.64);
        return sand * (hemiAmbient * 0.50 + warmSun * (0.18 + sunBand * 0.34));
    }
    if (mode == 7)
    {
        float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 1.8);
        return vec3(0.006, 0.010, 0.018) + materialColor * rim * 0.16 + lightColor * spec * 0.05;
    }
    return materialColor * (0.16 + diff * 0.92) + lightColor * spec * 0.12;
}

void main()
{
    // Re-normalize the interpolated normal (interpolation can denormalize it).
    vec3 N = normalize(vNormal);
    // View direction: from fragment toward camera (camera is at origin in view space).
    vec3 V = normalize(-vViewPos);

    // Light direction depends on whether light is directional (w=0) or point (w=1).
    vec3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);           // Directional: same direction everywhere
    else
        L = normalize(uLightPosition.xyz - vViewPos); // Point: direction from fragment to light

    // Ambient: constant light that illuminates everything equally (prevents pure black shadows).
    vec3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    // Diffuse: Lambert's cosine law — surfaces facing the light are brighter.
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    // Specular: mirror-like highlight — bright spot where light bounces toward camera.
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);  // 64 = shininess (higher = tighter highlight)
    vec3 specular = uLightDiffuse.rgb * spec * 0.1; // 0.1 = subtle specular intensity

    // Combine lighting with procedural beachball color or explicit instance color;
    // specular is added on top.
    bool cinematicMode = uLightPosition.w == 0.0;
    int materialMode = int(floor(vTint.a + 0.5));
    if (vTint.a < -0.5)
    {
        materialMode = 0;
    }
    else if (vTint.a > 1.25)
    {
        materialMode = int(floor(vTint.a + 0.5));
    }
    else if (vTint.a > 0.5)
    {
        materialMode = 1;
    }
    else
    {
        materialMode = uObjectStyle;
    }

    vec3 materialColor = vTint.rgb;
    if (materialMode == 0)
    {
        materialColor = ProceduralBeachBallColor(vTexCoord) * vTint.rgb;
    }

    if (cinematicMode)
    {
        // The cinematic ball lighting is warmer and softer than the normal Phong
        // path: wrap light fills the shadow side, rim light outlines the silhouette,
        // and glint gives glossy sunset highlights.
        float warmWrap = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
        float shadowFactor = ShadowVisibility(vWorldPos, N, L);
        float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.25) * (0.35 + warmWrap * 0.65);
        float glint = pow(max(dot(V, R), 0.0), 96.0);
        vec3 warmAmbient = materialColor * uLightAmbient.rgb * 1.15;
        vec3 directSun = materialColor * uLightDiffuse.rgb * (diff * 0.62 + warmWrap * 0.18) * shadowFactor;
        vec3 rimLight = uLightDiffuse.rgb * rim * 0.18;
        vec3 specularSun = uLightDiffuse.rgb * glint * 0.24 * shadowFactor;
        vec3 styled = ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, glint);
        styled *= shadowFactor;
        vec3 beachBall = warmAmbient + directSun + rimLight + specularSun;
        FragColor = vec4(materialMode == 0 ? beachBall : styled + rimLight * 0.35, 1.0);
        return;
    }

    float shadowFactor = ShadowVisibility(vWorldPos, N, L);
    vec3 litColor = materialMode == 0 ? (ambient + diffuse) * materialColor + specular
                                      : ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, spec);
    litColor *= shadowFactor;
    FragColor = vec4(litColor, 1.0);
}
