/*
File: SkullbonezData/shaders/shadow_depth_instanced.hlsl
Purpose:
  Render instanced mesh depth from the directional light view into shadow maps.

Summary:
  Applies per-instance transform matrices from instance vertex buffers to
  project instanced trees, rocks, and props into light space for shadow mapping.

Invariants:
  - Instance buffer layout matches RenderInstanceStore vertex stream format.
  - Culled or hidden instances are excluded prior to draw call dispatch.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Rendering/RenderInstanceStore.h
  - SkullbonezSource/Rendering/Shadow.h
*/

#pragma pack_matrix(column_major)

cbuffer Uniforms : register(b0)
{
    float4x4 uView;
    float4x4 uProjection;
    float4   uClipPlane;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD0;
    float4 model0   : TEXCOORD1;
    float4 model1   : TEXCOORD2;
    float4 model2   : TEXCOORD3;
    float4 model3   : TEXCOORD4;
    // Keep the shadow caster input layout identical to lit_textured_instanced.
    // The depth pass ignores material rows, but the shared instanced mesh buffer
    // still strides over them.
    float4 material0 : TEXCOORD5;
    float4 material1 : TEXCOORD6;
    float4 material2 : TEXCOORD7;
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float  clipDist : SV_ClipDistance0;
};

VS_OUT main_vs(VS_IN input)
{
    VS_OUT output;
    float4x4 model = transpose(float4x4(input.model0, input.model1, input.model2, input.model3));
    float4 worldPos = mul(model, float4(input.position, 1.0f));
    output.position = mul(uProjection, mul(uView, worldPos));
    output.clipDist = dot(worldPos, uClipPlane);
    return output;
}

float4 main_ps(VS_OUT input) : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
