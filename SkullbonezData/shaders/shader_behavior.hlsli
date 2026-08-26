/*
File: SkullbonezData/shaders/shader_behavior.hlsli
Purpose:
  Shares numerically testable shader behavior between HLSL and CPU fixtures.

Summary:
  Procedural-longitude mapping and homogeneous near-plane clipping compile from
  these exact function bodies in both shipping shaders and focused C++ tests.

Invariants:
  - Keep this header valid in both HLSL and C++ translation units.
  - The shader bake hashes this include as an executable shader input.

Related:
  - SkullbonezData/shaders/sky_atmosphere.hlsl
  - SkullbonezData/shaders/trajectory_ribbon.hlsl
  - tools/bake_shaders.py
*/
#pragma once

#ifdef __cplusplus
#include <cmath>
namespace SkullbonezCore::Rendering::ShaderBehavior
{
struct Float4
{
    float x;
    float y;
    float z;
    float w;
};

inline Float4 Lerp( const Float4& start, const Float4& end, float amount )
{
    return { start.x + ( end.x - start.x ) * amount, start.y + ( end.y - start.y ) * amount,
             start.z + ( end.z - start.z ) * amount, start.w + ( end.w - start.w ) * amount };
}

inline float Frac( float value ) { return value - std::floor( value ); }
inline float Sin( float value ) { return std::sin( value ); }
inline float Cos( float value ) { return std::cos( value ); }

#define SB_SHADER_INLINE inline
#define SB_SHADER_FLOAT4 Float4
#define SB_SHADER_INOUT(type) type&
#define SB_SHADER_LERP Lerp
#define SB_SHADER_FRAC Frac
#define SB_SHADER_SIN Sin
#define SB_SHADER_COS Cos
#define SB_SHADER_ABS std::abs
#else
#define SB_SHADER_INLINE
#define SB_SHADER_FLOAT4 float4
#define SB_SHADER_INOUT(type) inout type
#define SB_SHADER_LERP lerp
#define SB_SHADER_FRAC frac
#define SB_SHADER_SIN sin
#define SB_SHADER_COS cos
#define SB_SHADER_ABS abs
#endif

SB_SHADER_INLINE float PeriodicLongitudeX( float longitude )
{
    const float angle = SB_SHADER_FRAC( longitude ) * 6.28318530718f;
    return SB_SHADER_COS( angle );
}

SB_SHADER_INLINE float PeriodicLongitudeY( float longitude )
{
    const float angle = SB_SHADER_FRAC( longitude ) * 6.28318530718f;
    return SB_SHADER_SIN( angle );
}

SB_SHADER_INLINE float CloudLongitudeDomainX( float longitude, float height, float directionX )
{
    return PeriodicLongitudeX( longitude ) + height * 0.31f + directionX * 0.15f +
           SB_SHADER_SIN( height * 5.4f ) * 0.055f;
}

SB_SHADER_INLINE float CloudLongitudeDomainY( float longitude, float height )
{
    return PeriodicLongitudeY( longitude ) + height * 1.42f;
}

SB_SHADER_INLINE float PeriodicTriangle( float longitude, float cycles, float phase )
{
    const float wrappedLongitude = SB_SHADER_FRAC( longitude );
    return 1.0f - SB_SHADER_ABS( SB_SHADER_FRAC( wrappedLongitude * cycles + phase ) * 2.0f - 1.0f );
}

SB_SHADER_INLINE float PeriodicStreak( float longitude, float height )
{
    const float angle = SB_SHADER_FRAC( longitude ) * 6.28318530718f;
    return 0.5f + 0.5f * SB_SHADER_SIN( angle * 7.0f + height * 1.65f * 6.28318530718f );
}

SB_SHADER_INLINE bool ClipSegmentToHalfSpace( SB_SHADER_INOUT( SB_SHADER_FLOAT4 ) startClip,
                                               SB_SHADER_INOUT( SB_SHADER_FLOAT4 ) endClip,
                                               float startDistance, float endDistance )
{
    if ( startDistance < 0.0f && endDistance < 0.0f )
    {
        return false;
    }
    if ( startDistance < 0.0f )
    {
        startClip = SB_SHADER_LERP( startClip, endClip, startDistance / ( startDistance - endDistance ) );
    }
    else if ( endDistance < 0.0f )
    {
        endClip = SB_SHADER_LERP( startClip, endClip, startDistance / ( startDistance - endDistance ) );
    }
    return true;
}

SB_SHADER_INLINE bool ClipSegmentToNearPlane( SB_SHADER_INOUT( SB_SHADER_FLOAT4 ) startClip,
                                               SB_SHADER_INOUT( SB_SHADER_FLOAT4 ) endClip )
{
    const float minimumClipW = 0.0001f;
    if ( !ClipSegmentToHalfSpace( startClip, endClip, startClip.w - minimumClipW, endClip.w - minimumClipW ) )
    {
        return false;
    }
    return ClipSegmentToHalfSpace( startClip, endClip, startClip.z, endClip.z );
}

#undef SB_SHADER_INLINE
#undef SB_SHADER_FLOAT4
#undef SB_SHADER_INOUT
#undef SB_SHADER_LERP
#undef SB_SHADER_FRAC
#undef SB_SHADER_SIN
#undef SB_SHADER_COS
#undef SB_SHADER_ABS

#ifdef __cplusplus
} // namespace SkullbonezCore::Rendering::ShaderBehavior
#endif
