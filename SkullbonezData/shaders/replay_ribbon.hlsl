/*
File: SkullbonezData/shaders/replay_ribbon.hlsl
Purpose:
  Draws smooth replay prediction ribbons for causal paths and marker outlines.

Mental model:
  CPU code expands each world-space line segment into a camera-facing quad.
  This shader transforms the quad, then fades alpha near both ribbon edges so
  prediction overlays read as soft energy strokes instead of jagged line lists.

Glossary:
  Ribbon: Camera-facing quad that replaces one debug line segment.
  Edge coordinate: Per-vertex -1/+1 value interpolated across the ribbon width.
  HDR scale: Per-vertex brightness multiplier used to feed cinematic bloom.

Invariants:
  - Input layout is position, color, then replay style payload; CPU generation
    in RunEditorTracer must keep the same 11-float vertex shape.
  - Glow/emphasis is presentation-only. It must not alter replay prediction or
    deterministic physics state.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTracer.inl
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp
*/
#pragma pack_matrix( column_major )

cbuffer Uniforms : register( b0 )
{
    float4x4 uViewProj;
};

struct VS_IN
{
    float3 position : POSITION;
    float4 color : TEXCOORD0; // rgb, alpha
    float4 fx : TEXCOORD1;    // edgeCoord, edgeFeather, hdrScale, unused
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float4 fx : TEXCOORD0;
};

VS_OUT main_vs( VS_IN input )
{
    VS_OUT output;
    output.position = mul( uViewProj, float4( input.position, 1.0 ) );
    output.color = input.color;
    output.fx = input.fx;
    return output;
}

float4 main_ps( VS_OUT input ) : SV_TARGET
{
    const float edge = saturate( abs( input.fx.x ) );
    const float feather = clamp( input.fx.y, 0.02, 0.95 );
    const float coverage = 1.0 - smoothstep( 1.0 - feather, 1.0, edge );
    const float alpha = saturate( input.color.a * coverage );
    clip( alpha - 0.001 );

    const float hdrScale = max( input.fx.z, 0.0 );
    return float4( max( input.color.rgb, 0.0 ) * hdrScale, alpha );
}
