#version 330 core

// =============================================================================
// LIT TEXTURED FRAGMENT SHADER (lit_textured.frag)
// =============================================================================
//
// PURPOSE: Calculate the final pixel color using Phong lighting + texture.
// This shader runs ONCE PER PIXEL (potentially millions of times per frame).
//
// --- Phong Lighting Model ---
//
// Phong lighting simulates how real light interacts with surfaces using 3 components:
//
//  1. AMBIENT  = constant base illumination (simulates indirect/bounced light)
//               Even surfaces facing away from the light aren't pitch black.
//
//  2. DIFFUSE  = light hitting the surface directly. Brighter when the surface
//               faces the light head-on, dimmer at glancing angles.
//               Uses Lambert's cosine law: brightness = dot(Normal, LightDir)
//
//  3. SPECULAR = shiny highlight (the "glint" on a billiard ball).
//               Brightest when your eye lines up with the reflection of the light.
//               Uses Phong reflection: brightness = dot(ViewDir, ReflectDir)^shininess
//
//       Light
//        \
//         \  Normal (N)
//          \ |
//           \|
//    --------*-------- Surface
//           /|
//          / |
//         /  Reflected (R)
//        /
//      Eye (V)
//
//  Final color = (ambient + diffuse) × texture_color + specular
//
// =============================================================================

in vec3 vViewPos;
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform sampler2D uShadowMap;
uniform mat4 uShadowViewProj;
uniform vec4 uShadowParams; // strength, depth bias, slope bias, texel step
uniform vec4 uShadowFlags;  // enabled, receive, pcf radius, zero-to-one depth
uniform vec4 uCinematicTerrain; // enable, relief, basin depth, rim lift
uniform vec4 uCinematicBasin;   // center x/z, radius x/z
uniform vec4 uStyleModes;       // sky, terrain, object, water
uniform vec4 uTerrainTint;      // rgb tint
uniform vec4 uTerrainAccent;    // rgb accent
uniform vec4 uTerrainGrid;      // scale, strength, unused, unused

// Light properties (view space, w=0 directional, w=1 positional)
uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;

// Material properties
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;

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

float BasinDistance(vec2 xz)
{
    // Same oval basin measurement as the vertex shader. The fragment shader uses
    // it only for color grading, so the visual basin can shade darker in the
    // middle and warmer on the rim.
    vec2 radius = max(uCinematicBasin.zw, vec2(1.0));
    return length((xz - uCinematicBasin.xy) / radius);
}

float GridLine(vec2 xz, float scale)
{
    vec2 g = abs(fract(xz / max(scale, 0.001)) - 0.5);
    float lineDistance = min(g.x, g.y);
    return 1.0 - smoothstep(0.470, 0.498, lineDistance);
}

vec3 FacetNormalFromDerivatives(vec3 viewPos, vec3 fallbackNormal)
{
    vec3 dx = dFdx(viewPos);
    vec3 dy = dFdy(viewPos);
    vec3 faceN = normalize(cross(dy, dx));
    return dot(faceN, fallbackNormal) < 0.0 ? -faceN : faceN;
}

vec3 TerrainModeColor(int mode, vec3 texColor, vec3 N, vec3 worldPos)
{
    vec3 base = texColor * max(uTerrainTint.rgb, vec3(0.001));
    if (mode == 1)
    {
        float wear = sin(worldPos.x * 0.035) * sin(worldPos.z * 0.041) * 0.08;
        base = vec3(0.34, 0.35, 0.34) + wear + texColor * vec3(0.14);
    }
    else if (mode == 2)
    {
        base = vec3(0.58, 0.60, 0.61) + texColor * vec3(0.08);
    }
    else if (mode == 3)
    {
        float grid = GridLine(worldPos.xz, max(uTerrainGrid.x, 8.0));
        base = vec3(0.006, 0.012, 0.020) + uTerrainAccent.rgb * grid * max(uTerrainGrid.y, 0.0);
    }
    else if (mode == 4)
    {
        float veins = sin(worldPos.x * 0.026 + sin(worldPos.z * 0.021) * 2.0) * 0.5 + 0.5;
        base = mix(vec3(0.18, 0.08, 0.24), uTerrainTint.rgb, veins * 0.55) + uTerrainAccent.rgb * pow(veins, 5.0) * 0.45;
    }
    else if (mode == 5)
    {
        base = texColor * uTerrainTint.rgb + vec3(0.20, 0.10, 0.02) * (1.0 - max(N.y, 0.0));
    }
    else if (mode == 6)
    {
        base = floor((texColor * uTerrainTint.rgb + uTerrainAccent.rgb * 0.08) * 5.0) / 5.0;
    }
    else if (mode == 7)
    {
        float heightT = clamp((worldPos.y - 28.0) / 115.0, 0.0, 1.0);
        float terrace = floor(heightT * 5.0) / 5.0;
        vec3 lowColor = mix(vec3(0.15, 0.32, 0.10), uTerrainAccent.rgb, 0.18);
        vec3 midColor = mix(vec3(0.34, 0.54, 0.18), uTerrainTint.rgb, 0.36);
        vec3 highColor = vec3(0.58, 0.62, 0.30);
        base = mix(lowColor, midColor, smoothstep(0.08, 0.58, heightT));
        base = mix(base, highColor, smoothstep(0.58, 1.0, heightT));
        float slope = clamp(1.0 - max(N.y, 0.0), 0.0, 1.0);
        vec3 slopeColor = mix(vec3(0.28, 0.25, 0.12), uTerrainAccent.rgb, 0.30);
        base = mix(base, slopeColor, slope * 0.42);
        vec2 patchCell = floor(worldPos.xz / 64.0);
        float patchSeed = fract(sin(dot(patchCell, vec2(12.9898, 78.233))) * 43758.5453);
        float patchBand = floor(patchSeed * 4.0) / 3.0;
        vec3 coolPatch = mix(vec3(0.14, 0.30, 0.08), uTerrainAccent.rgb, 0.12);
        vec3 warmPatch = vec3(0.43, 0.48, 0.18);
        base = mix(base, mix(coolPatch, warmPatch, patchBand), 0.13);
        float basinT = clamp(1.0 - BasinDistance(worldPos.xz), 0.0, 1.0);
        base *= 1.0 - basinT * 0.055;
        float facet = floor(max(N.y, 0.0) * 4.0) / 4.0;
        base *= 0.72 + facet * 0.34 + terrace * 0.08;
    }
    else if (mode == 8)
    {
        base = mix(vec3(0.08, 0.09, 0.10), uTerrainTint.rgb, 0.55) + texColor * 0.05;
    }
    else if (mode == 9)
    {
        base = mix(vec3(0.80, 0.84, 0.88), vec3(0.35, 0.42, 0.48), clamp(1.0 - N.y, 0.0, 1.0)) + texColor * 0.06;
    }
    else if (mode == 10)
    {
        float grid = GridLine(worldPos.xz, max(uTerrainGrid.x, 18.0));
        base = vec3(0.055, 0.060, 0.066) + grid * uTerrainAccent.rgb * max(uTerrainGrid.y, 0.0);
    }
    else if (mode == 11)
    {
        base = mix(vec3(0.76, 0.82, 0.88), vec3(0.96, 0.98, 1.0), max(N.y, 0.0)) + uTerrainAccent.rgb * 0.05;
    }
    else if (mode == 12)
    {
        base = texColor * uTerrainTint.rgb * vec3(0.92, 1.02, 0.88);
    }
    else if (mode == 13)
    {
        vec3 bands = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + worldPos.x * 0.018 + worldPos.z * 0.023);
        base = mix(uTerrainTint.rgb, uTerrainAccent.rgb, bands);
    }
    else if (mode == 14)
    {
        base = mix(uTerrainTint.rgb, vec3(0.90, 0.92, 0.84), 0.35 + max(N.y, 0.0) * 0.25);
    }
    float authoredGrid = GridLine(worldPos.xz, max(uTerrainGrid.x, 8.0)) * max(uTerrainGrid.y, 0.0);
    base += uTerrainAccent.rgb * authoredGrid;
    return base;
}

void main()
{
    // Normalize the interpolated normal (interpolation between vertices can un-normalize it).
    vec3 N = normalize(vNormal);

    // View direction: vector from this pixel toward the camera (camera is at origin in view space).
    vec3 V = normalize(-vViewPos);

    // Light direction — depends on whether this is a directional or point light:
    //   w=0: directional (like the sun, infinitely far away — direction is constant)
    //   w=1: positional (like a lamp — direction depends on pixel position)
    vec3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - vViewPos);

    // AMBIENT: constant illumination regardless of surface orientation.
    vec3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    // DIFFUSE: dot(N, L) gives the cosine of the angle between surface normal and light.
    // When the surface faces the light directly (angle=0), dot=1 (full brightness).
    // When the surface is perpendicular to light (angle=90°), dot=0 (no light).
    // max() clamps negative values (surface facing away from light) to zero.
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    // SPECULAR (Phong reflection): simulate a shiny highlight.
    // reflect(-L, N) gives the mirror reflection of the light direction around the normal.
    // dot(V, R) measures how closely your eye aligns with that reflection.
    // pow(..., 64.0) makes the highlight sharp (higher exponent = smaller, tighter highlight).
    // × 0.1 keeps it subtle.
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);
    vec3 specular = uLightDiffuse.rgb * spec * 0.1;

    // Sample the texture at this pixel's UV coordinate.
    vec4 texColor = texture(uTexture, vTexCoord);

    if (uLightPosition.w == 0.0)
    {
        // w=0 is how the C++ render path tells this shader "cinematic sun mode".
        // The terrain then gets a warmer, more photographic grade instead of the
        // neutral gameplay Phong lighting.
        int terrainMode = int(floor(uStyleModes.y + 0.5));
        vec3 terrainN = N;
        if (terrainMode == 7)
        {
            terrainN = normalize(mix(N, FacetNormalFromDerivatives(vViewPos, N), 0.86));
        }
        float terrainDot = dot(terrainN, L);
        float terrainDiff = max(terrainDot, 0.0);
        float shadowFactor = ShadowVisibility(vWorldPos, terrainN, L);
        float warmWrap = clamp(terrainDot * 0.5 + 0.5, 0.0, 1.0);
        float grazing = pow(clamp(1.0 - abs(terrainDot), 0.0, 1.0), 1.5);
        float rim = pow(1.0 - clamp(dot(terrainN, V), 0.0, 1.0), 3.0) * (0.25 + warmWrap * 0.75);
        vec3 earthBase = TerrainModeColor(terrainMode, texColor.rgb, terrainN, vWorldPos);
        if (uCinematicTerrain.x > 0.5)
        {
            // If visual terrain relief is enabled, darken the basin center and
            // add a subtle warm lift near the rim. This helps the bowl read even
            // before the height exaggeration slider is turned up high.
            float relief = clamp(uCinematicTerrain.y, 0.0, 1.5);
            float d = BasinDistance(vWorldPos.xz);
            float bowlShade = (1.0 - smoothstep(0.16, 0.88, d)) * relief;
            float rimShade = exp(-pow((d - 1.03) * 3.2, 2.0)) * relief;
            earthBase = mix(earthBase, earthBase * vec3(0.62, 0.47, 0.34), bowlShade * 0.55);
            earthBase += vec3(0.20, 0.09, 0.02) * rimShade * 0.10;
        }
        if (terrainMode == 7)
        {
            // Low-poly art mode: no texture dependency, just height-colored
            // terrain, hemisphere ambient, warm sun bands, and readable facets.
            float hemiT = clamp(terrainN.y * 0.5 + 0.5, 0.0, 1.0);
            vec3 skyAmbient = vec3(0.38, 0.52, 0.76);
            vec3 groundAmbient = vec3(0.14, 0.22, 0.08);
            vec3 hemiAmbient = mix(groundAmbient, skyAmbient, hemiT);
            float sunBand = terrainDiff > 0.72 ? 1.0 : (terrainDiff > 0.34 ? 0.62 : 0.30);
            float softFill = warmWrap * 0.10;
            vec3 warmSun = uLightDiffuse.rgb * vec3(0.96, 0.78, 0.50);
            vec3 color = earthBase * (hemiAmbient * 0.64 + warmSun * (sunBand * 0.26 + softFill * 0.78) * shadowFactor);
            color += warmSun * (rim * 0.042 + grazing * 0.022) * shadowFactor;
            FragColor = vec4(color, 1.0);
            return;
        }

        // Grade the texture into a sunset palette: brown shadow tone, orange lit
        // tone, and a tiny ridge highlight where the view/light angle catches.
        vec3 shadowTone = earthBase * vec3(0.42, 0.28, 0.18);
        vec3 litTone = earthBase * vec3(1.28, 0.72, 0.34);
        vec3 gradedBase = mix(shadowTone, litTone, clamp(terrainDiff * 0.85 + warmWrap * 0.12, 0.0, 1.0));
        vec3 warmAmbient = gradedBase * uLightAmbient.rgb * (0.95 + max(terrainN.y, 0.0) * 0.35);
        vec3 directSun = gradedBase * uLightDiffuse.rgb * (terrainDiff * 0.42 + grazing * 0.08) * shadowFactor;
        vec3 ridgeLight = uLightDiffuse.rgb * (rim * 0.045 + spec * 0.035) * shadowFactor;
        FragColor = vec4(warmAmbient + directSun + ridgeLight, 1.0);
        return;
    }

    float shadowFactor = ShadowVisibility(vWorldPos, N, L);

    // Combine: multiply lighting by texture color, then add specular on top.
    // Specular is added separately because highlights should be the light's color, not tinted by texture.
    FragColor = vec4((ambient + diffuse * shadowFactor) * texColor.rgb + specular * shadowFactor, 1.0);
}
