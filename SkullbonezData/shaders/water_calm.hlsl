// =============================================================================
// CALM WATER SHADER — HLSL 5.0 (Combined VS+PS)
// =============================================================================
//
// PURPOSE: Render the INNER water zone as a perfectly flat mirror surface.
// HLSL equivalent of water_calm.vert + water_calm.frag.
//
// --- Why a Separate Calm Shader? ---
//
//  The inner water zone (surrounding the terrain) uses a flat surface with
//  undistorted reflections. This gives a clean, mirror-like lake appearance
//  while the outer ocean zone has waves and shimmer.
//
//  No wave displacement, no UV perturbation — just clean projective texturing.
//
// --- DX Texture Flip ---
//
//  reflUV.y = 1.0 - reflUV.y is needed because DirectX texture coordinates
//  have their origin at the TOP-LEFT, while our reflection camera assumes
//  BOTTOM-LEFT origin. This Y-flip corrects the orientation.
//
// Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-resources-coordinates
// =============================================================================

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uModel;               // Water quad positioning
    float4x4 uView;                // Camera view
    float4x4 uProjection;          // Perspective projection
    float4x4 uReflectVP;           // Reflected camera View×Projection
    float4   uColorTint;           // Base water color (dark blue-green)
    float    uReflectionStrength;  // Reflection blend factor (0=tint, 1=mirror)
    int      uNoReflect;           // 1 = skip reflection, output flat tint
    float    uCinematicMode;       // 1 = warm sunset response
    int      uWaterMode;           // 1 = basin pool, 2 = full calm plane, 4 = stylized basin
    float    uSunGlintStrength;
    float3   uSunColor;
    float4   uBasinMask;
    float    uBasinMaskFeather;
    float3   _pad0;
};

Texture2D    uReflectionTex : register(t1);  // Scene rendered from reflected camera
SamplerState sSampler1      : register(s1);

struct VS_IN
{
    float3 position : POSITION;  // Flat water grid vertex
};

struct VS_OUT
{
    float4 position       : SV_POSITION;
    float4 reflectClipPos : TEXCOORD0;   // Vertex in reflection camera's clip space
    float2 worldXZ        : TEXCOORD1;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;

    // No wave displacement — calm water stays perfectly flat.
    float4 worldPos       = mul(uModel, float4(input.position, 1.0));
    output.position       = mul(uProjection, mul(uView, worldPos));
    output.reflectClipPos = mul(uReflectVP, mul(uModel, float4(input.position, 1.0)));
    output.worldXZ        = input.position.xz;

    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float basinMask = 1.0f;
    float basinDistance = 0.0f;
    float2 basinOffset = float2(0.0f, 0.0f);
    if (uCinematicMode > 0.5f && (uWaterMode == 1 || uWaterMode == 4))
    {
        // Cinematic mode turns the calm water into an oval pool in the basin.
        // Outside the oval we discard pixels so the old broad water plane does
        // not cover the shot.
        basinOffset = (input.worldXZ - uBasinMask.xy) / max(uBasinMask.zw, float2(1.0f, 1.0f));
        basinDistance = length(basinOffset);
        basinMask = 1.0f - smoothstep(max(0.0f, 1.0f - uBasinMaskFeather), 1.0f, basinDistance);
        clip(basinMask - 0.01f);
    }

    // When reflection is disabled, output flat tint to match ocean behaviour.
    if (uNoReflect != 0)
        return float4(uColorTint.rgb, uColorTint.a * basinMask);
    // Projective texture mapping: clip → NDC → UV.
    float2 reflUV = (input.reflectClipPos.xy / input.reflectClipPos.w) * 0.5 + 0.5;
    reflUV.y = 1.0 - reflUV.y;  // DX texture Y-flip (top-left origin)
    // Sample undistorted reflection — perfect mirror.
    float4 reflection = uReflectionTex.Sample(sSampler1, reflUV);
    float3 waterColor = lerp(uColorTint.rgb, reflection.rgb, uReflectionStrength);
    if (uCinematicMode > 0.5f && uWaterMode == 4)
    {
        // Low-poly water: clean turquoise depth bands with a bright shoreline.
        // It keeps a tiny reflection contribution but avoids the mirror-like
        // grey sheet that fights the stylized terrain.
        float shore = smoothstep(0.54f, 0.94f, basinDistance);
        float angle = atan2(basinOffset.y, basinOffset.x);
        float shard = floor(frac(angle * 1.90986f + basinDistance * 2.4f + 0.35f) * 4.0f) / 4.0f;
        float depthBand = floor(saturate(1.0f - basinDistance) * 5.0f + shard * 0.45f) / 5.0f;
        float3 deep = float3(0.030f, 0.24f, 0.31f);
        float3 mid = float3(0.075f, 0.48f, 0.52f);
        float3 shallow = float3(0.50f, 0.76f, 0.62f);
        waterColor = lerp(deep, mid, 0.24f + depthBand * 0.54f);
        waterColor = lerp(waterColor, shallow, shore * 0.36f);
        waterColor *= 0.88f + shard * 0.13f;
        float wedge = frac(angle * 2.86479f + basinDistance * 0.30f + 0.5f);
        float panelEdge = 1.0f - smoothstep(0.0f, 0.040f, min(wedge, 1.0f - wedge));
        waterColor = lerp(waterColor, waterColor * float3(0.64f, 0.82f, 0.86f), panelEdge * 0.20f);
        waterColor = lerp(waterColor, reflection.rgb, min(uReflectionStrength, 0.24f));
        float rimLine = smoothstep(0.70f, 0.91f, basinDistance) * (1.0f - smoothstep(0.94f, 1.0f, basinDistance));
        float innerRim = smoothstep(0.52f, 0.72f, basinDistance) * (1.0f - smoothstep(0.76f, 0.88f, basinDistance));
        waterColor += float3(0.48f, 0.42f, 0.18f) * rimLine;
        waterColor += float3(0.10f, 0.20f, 0.16f) * innerRim;
        float sunShard = pow(max(0.0f, 1.0f - abs(reflUV.x - 0.64f) * 6.0f), 3.0f) *
                         pow(max(0.0f, 1.0f - abs(reflUV.y - 0.48f) * 8.0f), 2.0f);
        waterColor += uSunColor * sunShard * 0.105f;
    }
    if (uCinematicMode > 0.5f)
    {
        // Add a fake sunset glint where the reflected sun column would land.
        // This is deliberately screen/reflection-space, so it is stable and easy
        // to tune without building a full physical water lighting model.
        float sunColumn = pow(max(0.0f, 1.0f - abs(reflUV.x - 0.28f) * 4.6f), 3.0f);
        float horizonLine = pow(max(0.0f, 1.0f - abs(reflUV.y - 0.54f) * 9.0f), 2.0f);
        float glint = sunColumn * horizonLine * uSunGlintStrength;
        if (uWaterMode != 4)
        {
            waterColor = lerp(waterColor * float3(0.72f, 0.58f, 0.42f), float3(0.58f, 0.24f, 0.065f), 0.14f);
        }
        waterColor += uSunColor * glint;
    }
    return float4(waterColor, uColorTint.a * basinMask);
}
