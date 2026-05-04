#version 330 core

// Phong lighting + texture: fragment shader (shared with non-instanced version)

uniform vec4 uLightPosition;
uniform vec4 uLightAmbient;
uniform vec4 uLightDiffuse;
uniform vec4 uMaterialAmbient;
uniform vec4 uMaterialDiffuse;
uniform sampler2D uTexture;

in vec3 vViewPos;
in vec3 vNormal;
in vec2 vTexCoord;

out vec4 FragColor;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(-vViewPos);

    vec3 L;
    if (uLightPosition.w == 0.0)
        L = normalize(uLightPosition.xyz);
    else
        L = normalize(uLightPosition.xyz - vViewPos);

    vec3 ambient = uLightAmbient.rgb * uMaterialAmbient.rgb;

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = uLightDiffuse.rgb * uMaterialDiffuse.rgb * diff;

    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), 64.0);
    vec3 specular = uLightDiffuse.rgb * spec * 0.1;

    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = vec4((ambient + diffuse) * texColor.rgb + specular, 1.0);
}
