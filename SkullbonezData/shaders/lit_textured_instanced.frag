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
uniform int uObjectStyle;

in vec3 vViewPos;
in vec3 vNormal;
in vec2 vTexCoord;
in vec4 vTint;

out vec4 FragColor;

vec3 ProceduralBeachBallColor(vec2 uv)
{
    // The original source texture is intentionally low-res, which becomes blurry
    // when a ball is close to the camera. In cinematic mode we still use the mesh
    // UVs, but we choose the red/yellow color in shader math so the edges stay
    // razor crisp at any resolution.
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
        float bands = floor(clamp(diff, 0.0, 1.0) * 3.0) / 2.0;
        vec3 poster = floor(materialColor * 4.0) / 4.0;
        return poster * (0.25 + bands * 0.95);
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

    // Combine lighting with texture or explicit instance color; specular is added on top.
    vec4 texColor = texture(uTexture, vTexCoord);
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

    if (cinematicMode && materialMode == 0)
    {
        // Directional light means cinematic sun mode, so replace the sampled
        // texture with the procedural red/yellow panel color described above.
        texColor.rgb = ProceduralBeachBallColor(vTexCoord);
    }
    vec3 materialColor = materialMode == 0 ? texColor.rgb * vTint.rgb : vTint.rgb;

    if (cinematicMode)
    {
        // The cinematic ball lighting is warmer and softer than the normal Phong
        // path: wrap light fills the shadow side, rim light outlines the silhouette,
        // and glint gives glossy sunset highlights.
        float warmWrap = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
        float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 2.25) * (0.35 + warmWrap * 0.65);
        float glint = pow(max(dot(V, R), 0.0), 96.0);
        vec3 warmAmbient = materialColor * uLightAmbient.rgb * 1.15;
        vec3 directSun = materialColor * uLightDiffuse.rgb * (diff * 0.62 + warmWrap * 0.18);
        vec3 rimLight = uLightDiffuse.rgb * rim * 0.18;
        vec3 specularSun = uLightDiffuse.rgb * glint * 0.24;
        vec3 styled = ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, glint);
        vec3 beachBall = warmAmbient + directSun + rimLight + specularSun;
        FragColor = vec4(materialMode == 0 ? beachBall : styled + rimLight * 0.35, 1.0);
        return;
    }

    vec3 litColor = materialMode == 0 ? (ambient + diffuse) * materialColor + specular
                                      : ApplyMaterialMode(materialMode, materialColor, N, V, L, uLightDiffuse.rgb, diff, spec);
    FragColor = vec4(litColor, 1.0);
}
