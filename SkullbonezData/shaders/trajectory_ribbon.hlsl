/*
File: SkullbonezData/shaders/trajectory_ribbon.hlsl
Purpose:
  Draws replay trajectory ribbons from compact world-space segment payloads.

Summary:
  CPU code emits six vertices per segment, but every vertex carries the same
  start/end/style payload. The vertex shader uses SV_VertexID to pick the corner
  and expands the segment in clip space, keeping the ribbon width stable in
  screen pixels instead of world units.

Glossary:
  Segment payload: start position, end position plus width, rgba color, and
    feather/brightness style hints.
  Edge coordinate: Shader-derived -1/+1 value interpolated across the ribbon.
  Screen-space width: Ribbon width measured in pixels after projection.

Invariants:
  - Input layout is position, end/width, color/alpha, then style hints; CPU
    generation in RunEditorTracer must keep the same 13-float vertex shape.
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
    float4 uRibbonStyle; // opacity scale, brightness scale, feather scale, unused
};

struct VS_IN
{
    float3 start : POSITION;
    float4 endAndWidth : TEXCOORD0; // end.xyz, width in replay-ribbon units
    float4 color : TEXCOORD1;       // rgb, alpha
    float2 style : TEXCOORD2;       // edge feather, HDR emphasis
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float edgeCoord : TEXCOORD0;
    float2 style : TEXCOORD1;
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
    output.style = float2( max( input.style.x, 0.02 ), max( input.style.y, 0.0 ) );
    return output;
}

float4 main_ps( VS_OUT input ) : SV_TARGET
{
    const float edge = saturate( abs( input.edgeCoord ) );
    const float feather = clamp( input.style.x * max( uRibbonStyle.z, 0.25 ), 0.08, 1.25 );
    const float glowStart = saturate( 0.96 - feather * 0.46 );
    const float shoulderStart = saturate( 0.56 - feather * 0.22 );
    const float coreEnd = saturate( 0.24 + feather * 0.18 );
    const float glow = 1.0 - smoothstep( glowStart, 1.0, edge );
    const float shoulder = 1.0 - smoothstep( shoulderStart, 1.0, edge );
    const float core = 1.0 - smoothstep( 0.0, coreEnd, edge );
    const float coverage = saturate( glow * 0.22 + shoulder * 0.28 + core * 0.72 );
    const float alpha = saturate( input.color.a * max( uRibbonStyle.x, 0.0 ) * coverage );
    clip( alpha - 0.001 );

    const float hdrScale =
        input.style.y * max( uRibbonStyle.y, 0.0 ) * ( 0.70 + core * 0.30 + shoulder * 0.12 + glow * 0.06 );
    return float4( max( input.color.rgb, 0.0 ) * hdrScale, alpha );
}
