/*
File: SkullbonezData/shaders/trajectory_ribbon.hlsl
Purpose:
  Render replay and predicted trajectory ribbon paths with halo highlights and time markers.

Summary:
  Expands 3D trajectory segments into screen-space ribbons with analytic width,
  applying path color, historical fading, branch selection halos, and contact
  markers.

Invariants:
  - CPU-side root signatures, input layouts, and descriptor bindings must match this shader exactly.
  - Ribbon expansion maintains constant screen-space pixel width regardless of camera distance.

Related:
  - Agentic/Reference/engine-glossary.md
  - SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h
  - SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h
*/

#pragma pack_matrix( column_major )
#include "shader_behavior.hlsli"

cbuffer Uniforms : register( b0 )
{
    float4x4 uViewProj;
    float4 uViewportPixels;
    float4 uRibbonStyle; // opacity scale, brightness scale, anti-aliasing scale, unused
};

struct VS_IN
{
    float3 start : POSITION;
    float4 endAndWidth : TEXCOORD0; // end.xyz, full screen-space width in pixels
    float4 color : TEXCOORD1;       // rgb, alpha
    float2 style : TEXCOORD2;       // anti-aliasing feather, selection emphasis
    float3 previous : TEXCOORD3;    // previous polyline point; start means an open cap
    float3 next : TEXCOORD4;        // next polyline point; end means an open cap
};

struct VS_OUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    noperspective float edgeCoord : TEXCOORD0; // signed perpendicular distance in pixels
    float2 style : TEXCOORD1;
    noperspective float3 ribbonCoord : TEXCOORD2; // along pixels, segment pixels, core half-width pixels
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
    const float widthPixels = clamp( input.endAndWidth.w, 1.0, 6.0 );
    const float halfWidthPixels = widthPixels * 0.5;
    const float aaPixels = clamp( max( input.style.x, 0.5 ) * max( uRibbonStyle.z, 0.25 ), 0.5, 1.25 );
    const float emphasis = saturate( input.style.y );
    // Invariant: the triangles must extend beyond the ideal line edge. Without
    // this overhang, rasterization clips the analytic feather and selected halo.
    const float geometryHalfWidthPixels = halfWidthPixels + aaPixels + emphasis * 3.0;
    const uint corner = vertexId % 6;
    const float endpointT = ( corner == 2 || corner == 4 || corner == 5 ) ? 1.0 : 0.0;
    const float side = ( corner == 1 || corner == 2 || corner == 4 ) ? 1.0 : -1.0;

    float4 previousClip = mul( uViewProj, float4( input.previous, 1.0 ) );
    float4 startClip = mul( uViewProj, float4( input.start, 1.0 ) );
    float4 endClip = mul( uViewProj, float4( end, 1.0 ) );
    float4 nextClip = mul( uViewProj, float4( input.next, 1.0 ) );
    const bool startWasBehind = startClip.w < 0.0001 || startClip.z < 0.0;
    const bool endWasBehind = endClip.w < 0.0001 || endClip.z < 0.0;
    // Hazard: the shared behavior seam clips w first, then the D3D z>=0 near
    // plane before any endpoint participates in screen-space expansion.
    if ( !ClipSegmentToNearPlane( startClip, endClip ) )
    {
        VS_OUT hidden;
        hidden.position = float4( 0.0, 0.0, -1.0, 1.0 );
        hidden.color = float4( 0.0, 0.0, 0.0, 0.0 );
        hidden.edgeCoord = 0.0;
        hidden.style = float2( 0.0, 0.0 );
        hidden.ribbonCoord = float3( 0.0, 0.0, 0.0 );
        return hidden;
    }

    // A clipped endpoint is a visible path cap. Adjacent behind-camera points
    // must not re-enter join math and recreate the same unbounded NDC vector.
    if ( startWasBehind )
    {
        previousClip = startClip;
    }
    else
    {
        float4 retainedStart = startClip;
        if ( !ClipSegmentToNearPlane( previousClip, retainedStart ) )
        {
            previousClip = startClip;
        }
    }
    if ( endWasBehind )
    {
        nextClip = endClip;
    }
    else
    {
        float4 retainedEnd = endClip;
        if ( !ClipSegmentToNearPlane( retainedEnd, nextClip ) )
        {
            nextClip = endClip;
        }
    }
    float4 baseClip = lerp( startClip, endClip, endpointT );

    const float2 startNdc = startClip.xy / SafeClipW( startClip.w );
    const float2 endNdc = endClip.xy / SafeClipW( endClip.w );
    const float2 previousNdc = previousClip.xy / SafeClipW( previousClip.w );
    const float2 nextNdc = nextClip.xy / SafeClipW( nextClip.w );
    float2 dir = endNdc - startNdc;
    const float dirLenSq = dot( dir, dir );
    dir = dirLenSq > 0.0000001 ? dir * rsqrt( dirLenSq ) : float2( 1.0, 0.0 );

    const float2 viewport = max( uViewportPixels.xy, float2( 1.0, 1.0 ) );
    const float2 segmentPixels = ( endNdc - startNdc ) * viewport * 0.5;
    const float segmentLengthPixels = max( length( segmentPixels ), 0.001 );
    const bool hasPrevious = dot( startNdc - previousNdc, startNdc - previousNdc ) > 0.0000001;
    const bool hasNext = dot( nextNdc - endNdc, nextNdc - endNdc ) > 0.0000001;
    float2 adjacentDir = dir;
    if ( endpointT < 0.5 && hasPrevious )
    {
        adjacentDir = normalize( startNdc - previousNdc );
    }
    else if ( endpointT > 0.5 && hasNext )
    {
        adjacentDir = normalize( nextNdc - endNdc );
    }
    const bool joinedEndpoint = endpointT < 0.5 ? hasPrevious : hasNext;
    const float turnAlignment = dot( dir, adjacentDir );
    const bool roundFallback = joinedEndpoint && turnAlignment < -0.25;
    const float2 currentNormal = float2( -dir.y, dir.x );
    const float2 adjacentNormal = float2( -adjacentDir.y, adjacentDir.x );
    float2 joinNormal = currentNormal;
    float joinScale = 1.0;
    if ( joinedEndpoint && !roundFallback )
    {
        joinNormal = normalize( currentNormal + adjacentNormal );
        joinScale = min( 2.5, 1.0 / max( abs( dot( joinNormal, currentNormal ) ), 0.4 ) );
    }
    const float2 ndcOffset = joinNormal * side * joinScale * ( geometryHalfWidthPixels * 2.0 / viewport );
    // Rounded caps exist only at real path ends or extreme reversal fallback.
    // Ordinary bends share a mitered edge, preventing per-sample glow beads.
    const bool needsRoundCap = !joinedEndpoint || roundFallback;
    const float endpointExtensionPixels =
        needsRoundCap ? ( endpointT > 0.5 ? geometryHalfWidthPixels : -geometryHalfWidthPixels ) : 0.0;
    const float2 endpointOffset = dir * endpointExtensionPixels * ( 2.0 / viewport );
    baseClip.xy += ndcOffset * baseClip.w;
    baseClip.xy += endpointOffset * baseClip.w;

    VS_OUT output;
    output.position = baseClip;
    output.color = saturate( input.color );
    output.edgeCoord = side * geometryHalfWidthPixels;
    output.style = float2( aaPixels, emphasis );
    output.ribbonCoord = float3( endpointT > 0.5 ? segmentLengthPixels + max( endpointExtensionPixels, 0.0 )
                                                 : min( endpointExtensionPixels, 0.0 ),
                                 segmentLengthPixels,
                                 halfWidthPixels );
    return output;
}

float4 main_ps( VS_OUT input ) : SV_TARGET
{
    const float halfWidthPixels = max( input.ribbonCoord.z, 0.5 );
    const float beforeStartPixels = max( -input.ribbonCoord.x, 0.0 );
    const float afterEndPixels = max( input.ribbonCoord.x - input.ribbonCoord.y, 0.0 );
    const float capDistancePixels = max( beforeStartPixels, afterEndPixels );
    // The distance is rectangular through the segment body and circular beyond
    // either endpoint, producing one crisp vector edge and analytic round caps.
    const float distancePixels = length( float2( abs( input.edgeCoord ), capDistancePixels ) );
    const float aaPixels = clamp( input.style.x, 0.5, 1.25 );
    const float coverage = 1.0 - smoothstep( halfWidthPixels, halfWidthPixels + aaPixels, distancePixels );
    const float emphasis = saturate( input.style.y );
    const float halo =
        emphasis * ( 1.0 - smoothstep( halfWidthPixels + aaPixels, halfWidthPixels + aaPixels + 3.0, distancePixels ) );
    const float alpha = saturate( input.color.a * max( uRibbonStyle.x, 0.0 ) * ( coverage + halo * 0.24 ) );
    clip( alpha - 0.001 );

    // At zero emphasis this is plain display-range color: no halo and no bloom
    // feed. Positive emphasis raises only the selected path above display range.
    const float brightness = max( uRibbonStyle.y, 0.0 );
    const float hdrScale = brightness * ( 1.0 + emphasis * ( 1.40 + coverage * 0.60 + halo * 0.30 ) );
    return float4( max( input.color.rgb, 0.0 ) * hdrScale, alpha );
}
