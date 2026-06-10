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
    if (uLightPosition.w == 0.0)
    {
        // Directional light means cinematic sun mode, so replace the sampled
        // texture with the procedural red/yellow panel color described above.
        texColor.rgb = ProceduralBeachBallColor(vTexCoord);
    }
    vec3 materialColor = mix(texColor.rgb * vTint.rgb, vTint.rgb, clamp(vTint.a, 0.0, 1.0));

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
        FragColor = vec4(warmAmbient + directSun + rimLight + specularSun, 1.0);
        return;
    }

    vec3 litColor = (ambient + diffuse) * materialColor + specular;
    FragColor = vec4(litColor, 1.0);
}
