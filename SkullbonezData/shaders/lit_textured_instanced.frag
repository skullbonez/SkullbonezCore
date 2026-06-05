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

    // Combine lighting with texture color; specular is added on top (not modulated by texture).
    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 litColor = (ambient + diffuse) * (texColor.rgb * vTint.rgb) + specular;
    FragColor = vec4(mix(litColor, vTint.rgb, clamp(vTint.a, 0.0, 1.0)), 1.0);
}
