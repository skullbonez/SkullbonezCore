#version 330 core

// Text rendering: fragment shader
// Samples font texture atlas, applies text color.

in vec2 vTexCoord;

uniform sampler2D uFontTexture;
uniform vec3 uTextColor;

out vec4 FragColor;

void main()
{
    float alpha = texture(uFontTexture, vTexCoord).r;
    FragColor = vec4(uTextColor, alpha);
}
