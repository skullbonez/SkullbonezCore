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
uniform vec4 uCinematicTerrain; // enable, relief, basin depth, rim lift
uniform vec4 uCinematicBasin;   // center x/z, radius x/z

// Light properties (view space, w=0 directional, w=1 positional)
uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;

// Material properties
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;

out vec4 FragColor;

float BasinDistance(vec2 xz)
{
    // Same oval basin measurement as the vertex shader. The fragment shader uses
    // it only for color grading, so the visual basin can shade darker in the
    // middle and warmer on the rim.
    vec2 radius = max(uCinematicBasin.zw, vec2(1.0));
    return length((xz - uCinematicBasin.xy) / radius);
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
        float warmWrap = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
        float grazing = pow(clamp(1.0 - abs(dot(N, L)), 0.0, 1.0), 1.5);
        float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 3.0) * (0.25 + warmWrap * 0.75);
        vec3 earthBase = texColor.rgb * vec3(0.78, 0.60, 0.38);
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
        // Grade the texture into a sunset palette: brown shadow tone, orange lit
        // tone, and a tiny ridge highlight where the view/light angle catches.
        vec3 shadowTone = earthBase * vec3(0.42, 0.28, 0.18);
        vec3 litTone = earthBase * vec3(1.28, 0.72, 0.34);
        vec3 gradedBase = mix(shadowTone, litTone, clamp(diff * 0.85 + warmWrap * 0.12, 0.0, 1.0));
        vec3 warmAmbient = gradedBase * uLightAmbient.rgb * (0.95 + max(N.y, 0.0) * 0.35);
        vec3 directSun = gradedBase * uLightDiffuse.rgb * (diff * 0.42 + grazing * 0.08);
        vec3 ridgeLight = uLightDiffuse.rgb * (rim * 0.045 + spec * 0.035);
        FragColor = vec4(warmAmbient + directSun + ridgeLight, 1.0);
        return;
    }

    // Combine: multiply lighting by texture color, then add specular on top.
    // Specular is added separately because highlights should be the light's color, not tinted by texture.
    FragColor = vec4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}
