#version 330 core

// Phong lighting + texture: fragment shader
// Per-pixel ambient + diffuse + specular lighting with atmospheric haze

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

// Atmospheric haze (exponential distance fog)
uniform vec3  uFogColor;      // horizon/haze color
uniform float uFogDensity;    // 0 = no fog, higher = thicker

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(-vViewPos);

    // Light direction
    vec3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - vViewPos);

    // Ambient
    vec3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    // Specular (Phong reflection) — subtle, view-dependent highlight
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);
    vec3 specular = uLightDiffuse.rgb * spec * 0.1;

    vec4 texColor = texture(uTexture, vTexCoord);
    vec3 litColor = (ambient + diffuse) * texColor.rgb + specular;

    // Atmospheric haze — exponential falloff blends toward horizon color
    float dist = length(vViewPos);
    float fogFactor = exp(-uFogDensity * dist);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    vec3 finalColor = mix(uFogColor, litColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}
