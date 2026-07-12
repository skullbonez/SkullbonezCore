/*
File: SkullbonezData/shaders/ui_render_target_preview.hlsl
Purpose:
  Displays renderer-owned texture resources inside the in-game UI.

Summary:
  This is a diagnostic UI shader. The CPU supplies an already shader-readable
  texture handle plus a mode flag for color, HDR color, or depth display.

Glossary:
  HDR (High Dynamic Range): Floating-point scene color that can hold values
  brighter than display white until tonemapping resolves it.
  SRV (Shader Resource View): Descriptor row used when shaders read a texture or
  buffer.

Invariants:
  - CPU-side dynamic vertex attributes are float2 position followed by float2 UV.
  - Texture slot t0 is the selected render target or buffer SRV.

Related:
  - SkullbonezSource/UI/UITabProfiler.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uProjection;
    float4   uPreviewParams; // x: mode 0=color 1=HDR color 2=depth, y: exposure, z: gamma, w: unused
};

Texture2D uTexture : register(t0);
SamplerState sSampler0 : register(s0);

struct VS_IN
{
    float2 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    output.position = mul(uProjection, float4(input.position, 0.0f, 1.0f));
    output.texCoord = input.texCoord;
    return output;
}

float3 TonemapPreview(float3 color)
{
    color = max(color, float3(0.0f, 0.0f, 0.0f)) * max(uPreviewParams.y, 0.0f);
    color = saturate((color * (2.51f * color + 0.03f)) /
                     (color * (2.43f * color + 0.59f) + 0.14f));
    return pow(color, 1.0f / max(uPreviewParams.z, 0.0001f));
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    float4 sampleValue = uTexture.Sample(sSampler0, saturate(input.texCoord));
    const int mode = (int)round(uPreviewParams.x);

    if (mode == 2)
    {
        float depth = saturate(sampleValue.r);
        float contrast = 1.0f - saturate((1.0f - depth) * 36.0f);
        return float4(contrast.xxx, 1.0f);
    }

    if (mode == 1)
    {
        return float4(TonemapPreview(sampleValue.rgb), 1.0f);
    }

    return float4(saturate(sampleValue.rgb), 1.0f);
}
