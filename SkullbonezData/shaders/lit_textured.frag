#version 330 core

// Gouraud lighting + texture: fragment shader
// Samples texture and multiplies by per-vertex lighting color

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 FragColor;

void main()
{
    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = texColor * vColor;
}
