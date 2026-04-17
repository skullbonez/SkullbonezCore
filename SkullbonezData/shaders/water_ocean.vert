#version 330 core

// Ocean (outer) water: vertex shader
// Layered sine-wave Y displacement for open-ocean surface animation.

layout(location = 0) in vec3 aPosition;

uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform mat4  uReflectVP;
uniform float uTime;
uniform float uWaveHeight;  // wave amplitude from engine.cfg ocean_wave_height
uniform int   uFlatWater;   // 1 = suppress displacement (debug key 3)

out vec4 vReflectClipPos;
out vec2 vWorldXZ;

void main()
{
    vec3 pos = aPosition;
    if (uFlatWater == 0)
    {
        pos.y += sin(pos.x * 0.04 + uTime * 1.2) * uWaveHeight
               + sin(pos.z * 0.06 + uTime * 0.8) * (uWaveHeight * 0.667);
    }

    gl_Position     = uProjection * uView * uModel * vec4(pos, 1.0);
    vReflectClipPos = uReflectVP  * uModel * vec4(aPosition, 1.0);
    vWorldXZ        = aPosition.xz;
}
