#version 330 core

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uTexelSize;

out vec4 FragColor;

void main()
{
    const float weights[5] = float[5](0.0625, 0.25, 0.375, 0.25, 0.0625);
    vec3 color = vec3(0.0);

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            vec2 uv = clamp(vTexCoord + vec2(float(x), float(y)) * uTexelSize.xy * 1.65, vec2(0.0), vec2(1.0));
            color += texture(uTexture, uv).rgb * weights[x + 2] * weights[y + 2];
        }
    }

    color = clamp(color * vec3(1.18, 1.22, 1.28) + vec3(0.07, 0.08, 0.10), vec3(0.0), vec3(1.0));
    FragColor = vec4(color, 0.94);
}
