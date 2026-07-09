/*
File: SkullbonezData/shaders/trajectory_ribbon.hlsl
Purpose:
  Draws replay trajectory ribbons from compact world-space segment payloads.

Mental model:
  CPU code emits six vertices per segment, but every vertex carries the same
  start/end/style payload. The vertex shader uses SV_VertexID to pick the corner
  and expands the segment in clip space, keeping the ribbon width stable in
  screen pixels instead of world units.

Glossary:
  Segment payload: start position, end position plus width, and rgba color.
  Edge coordinate: Shader-derived -1/+1 value interpolated across the ribbon.
  Screen-space width: Ribbon width measured in pixels after projection.

Invariants:
  - Input layout is position, end/width, then color/alpha; CPU generation in
    RunEditorTracer must keep the same 11-float vertex shape.
  - The shader changes only presentation. Replay simulation and prediction data
    remain owned by replay runtime code.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp
*/
#pragma pack_matrix( column_major )

cbuffer Uniforms : register( b0 )
{
    float4x4 uViewProj;
    float4 uViewportPixels;
};

struct VS_IN
{
    float3 start : POSITION;
    float4 endAndWidth : TEXCOORD0; // end.xyz, width in replay-ribbon units
    float4 color : TEXCOORD1;       // rgb, alpha
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float edgeCoord : TEXCOORD0;
};

float SafeClipW( float w )
{
    if ( abs( w ) < 0.0001 )
    {
        return w < 0.0 ? -0.0001 : 0.0001;
    }
    return w;
}

VS_OUT main_vs( VS_IN input, uint vertexId : SV_VertexID )
{
    const float3 end = input.endAndWidth.xyz;
    const float widthPixels = clamp( input.endAndWidth.w * 10.0, 1.5, 32.0 );
    const uint corner = vertexId % 6;
    const float endpointT = ( corner == 2 || corner == 4 || corner == 5 ) ? 1.0 : 0.0;
    const float side = ( corner == 1 || corner == 2 || corner == 4 ) ? 1.0 : -1.0;

    float4 startClip = mul( uViewProj, float4( input.start, 1.0 ) );
    float4 endClip = mul( uViewProj, float4( end, 1.0 ) );
    float4 baseClip = lerp( startClip, endClip, endpointT );

    const float2 startNdc = startClip.xy / SafeClipW( startClip.w );
    const float2 endNdc = endClip.xy / SafeClipW( endClip.w );
    float2 dir = endNdc - startNdc;
    const float dirLenSq = dot( dir, dir );
    dir = dirLenSq > 0.0000001 ? dir * rsqrt( dirLenSq ) : float2( 1.0, 0.0 );

    const float2 normal = float2( -dir.y, dir.x );
    const float2 viewport = max( uViewportPixels.xy, float2( 1.0, 1.0 ) );
    const float2 ndcOffset = normal * side * ( widthPixels / viewport );
    baseClip.xy += ndcOffset * baseClip.w;

    VS_OUT output;
    output.position = baseClip;
    output.color = saturate( input.color );
    output.edgeCoord = side;
    return output;
}

float4 main_ps( VS_OUT input ) : SV_TARGET
{
    const float edge = saturate( abs( input.edgeCoord ) );
    const float glow = 1.0 - smoothstep( 0.18, 1.0, edge );
    const float core = 1.0 - smoothstep( 0.0, 0.38, edge );
    const float coverage = saturate( glow * 0.36 + core * 0.88 );
    const float alpha = saturate( input.color.a * coverage );
    clip( alpha - 0.001 );

    const float hdrScale = 1.35 + core * 1.75 + glow * 0.55;
    return float4( max( input.color.rgb, 0.0 ) * hdrScale, alpha );
}
