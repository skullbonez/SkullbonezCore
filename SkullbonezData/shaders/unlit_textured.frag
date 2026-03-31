#version 330 core

// Unlit textured: fragment shader
// Samples texture only, supports alpha blending and optional color tint.

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uColorTint;  // default (1,1,1,1) = no tint

out vec4 FragColor;

void main()
{
    FragColor = texture(uTexture, vTexCoord) * uColorTint;
}
