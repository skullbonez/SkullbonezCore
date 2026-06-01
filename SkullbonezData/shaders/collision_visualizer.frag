#version 330 core

uniform vec4 uLightPosition;

in vec3 vViewPos;
in vec3 vNormal;
in vec4 vColor;

out vec4 FragColor;

void main()
{
    vec3 n = normalize(vNormal);
    vec3 v = normalize(-vViewPos);

    vec3 l;
    if (uLightPosition.w == 0.0)
        l = normalize(uLightPosition.xyz);
    else
        l = normalize(uLightPosition.xyz - vViewPos);

    float diff = max(dot(n, l), 0.0);
    vec3 r = reflect(-l, n);
    float spec = pow(max(dot(v, r), 0.0), 96.0);

    vec3 base = vColor.rgb;
    vec3 ambient = base * 0.22;
    vec3 diffuse = base * diff * 0.72;
    vec3 metallicSheen = vec3(0.85, 0.92, 1.0) * spec * 0.38;

    FragColor = vec4(ambient + diffuse + metallicSheen, vColor.a);
}
