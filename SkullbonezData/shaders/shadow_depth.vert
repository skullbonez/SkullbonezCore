#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec4 uClipPlane;
uniform vec4 uCinematicTerrain;
uniform vec4 uCinematicBasin;

float BasinDistance(vec2 xz)
{
    vec2 radius = max(uCinematicBasin.zw, vec2(1.0));
    return length((xz - uCinematicBasin.xy) / radius);
}

float CinematicTerrainOffset(vec2 xz)
{
    if (uCinematicTerrain.x < 0.5 || uCinematicTerrain.y <= 0.0)
    {
        return 0.0;
    }

    float d = BasinDistance(xz);
    float bowl = 1.0 - smoothstep(0.10, 0.94, d);
    float rim = exp(-pow((d - 1.04) * 3.1, 2.0));
    float slopeTexture = smoothstep(0.32, 0.92, d) * (1.0 - smoothstep(1.02, 1.55, d));
    float rough = (sin(xz.x * 0.045 + xz.y * 0.011) + sin(xz.y * 0.052 - xz.x * 0.017)) * 0.5;
    return uCinematicTerrain.y * (-uCinematicTerrain.z * bowl + uCinematicTerrain.w * rim + rough * 1.6 * slopeTexture);
}

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    worldPos.y += CinematicTerrainOffset(worldPos.xz);
    gl_Position = uProjection * uView * worldPos;
    gl_ClipDistance[0] = dot(worldPos, uClipPlane);
}
