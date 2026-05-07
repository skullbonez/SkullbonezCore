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
in vec2 vTexCoord;

uniform sampler2D uTexture;

// Light properties (view space, w=0 directional, w=1 positional)
uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;

// Material properties
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;

out vec4 FragColor;

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

    // Combine: multiply lighting by texture color, then add specular on top.
    // Specular is added separately because highlights should be the light's color, not tinted by texture.
    FragColor = vec4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}
