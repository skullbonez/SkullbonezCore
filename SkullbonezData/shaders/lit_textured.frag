#version 330 core

// Phong lighting + texture + shader-based shadows: fragment shader
// Per-pixel ambient + diffuse + specular lighting with shadow computation.

in vec3 vViewPos;
in vec3 vWorldPos;
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

// Shadow parameters
#define MAX_SHADOW_BALLS 512
uniform int uNumShadowBalls;
uniform vec4 uShadowBalls[MAX_SHADOW_BALLS];  // xy=world pos, z=radius, w=height above ground
uniform float uShadowMaxHeight;
uniform float uShadowMaxAlpha;
uniform float uShadowScale;

out vec4 FragColor;

float ComputeShadowFactor()
{
    float totalShadow = 0.0;

    for (int i = 0; i < uNumShadowBalls; ++i)
    {
        vec2 ballPos = uShadowBalls[i].xy;
        float ballRadius = uShadowBalls[i].z;
        float ballHeight = uShadowBalls[i].w;

        // Only compute shadow if ball is not too high
        if (ballHeight >= uShadowMaxHeight)
            continue;

        // Distance in XZ plane from pixel to ball center
        float distXZ = distance(vWorldPos.xz, ballPos);

        // Shadow radius scales with ball radius
        float shadowRadius = ballRadius * uShadowScale;

        // Only contribute if within shadow radius
        if (distXZ > shadowRadius)
            continue;

        // Fade shadow with distance: full at center, zero at edge
        float distanceFade = 1.0 - (distXZ / shadowRadius);

        // Fade shadow with height: full when on ground, zero at max height
        float heightFade = 1.0 - (ballHeight / uShadowMaxHeight);

        // Combine fades and cap total shadow
        float shadowContrib = uShadowMaxAlpha * distanceFade * heightFade;
        totalShadow = min(totalShadow + shadowContrib, 1.0);
    }

    return totalShadow;
}

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

    // Compute shadow and apply darkening
    float shadowFactor = ComputeShadowFactor();
    vec3 shadowed = mix(ambient + diffuse, ambient * 0.5, shadowFactor);

    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = vec4(shadowed * texColor.rgb + specular, 1.0);
}
