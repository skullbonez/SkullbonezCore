/*
File: SkullbonezSource/Physics/PhysicsMass.h
Purpose:
  Defines shared mass, density, and inertia defaults for physics object creation.

Mental model:
  Object mass is an authored physical property. Hull masses come from baked
  assets; only parametric editor primitives still derive defaults at runtime.

Related:
  - SkullbonezSource/Runtime/RunInput.cpp
  - SkullbonezSource/Physics/ConvexHullShape.cpp
*/
#pragma once

#include "../Core/Common.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Physics
{
constexpr float DEFAULT_FLOATING_OBJECT_DENSITY = 0.90f;
constexpr float MIN_DYNAMIC_MASS = 0.001f;

inline float ClampPositiveMass( float mass )
{
    return std::isfinite( mass ) && mass > MIN_DYNAMIC_MASS ? mass : MIN_DYNAMIC_MASS;
}

inline float CalculateSphereVolume( float radius )
{
    radius = (std::max)( 0.0f, radius );
    return FOUR_OVER_THREE * _PI * radius * radius * radius;
}

inline float CalculateBoxVolume( const Math::Vector::Vector3& halfExtents )
{
    const float halfX = (std::max)( 0.0f, halfExtents.x );
    const float halfY = (std::max)( 0.0f, halfExtents.y );
    const float halfZ = (std::max)( 0.0f, halfExtents.z );
    return 8.0f * halfX * halfY * halfZ;
}

inline float CalculateSphereMass( float radius )
{
    return ClampPositiveMass( CalculateSphereVolume( radius ) * DEFAULT_FLOATING_OBJECT_DENSITY );
}

inline float CalculateBoxMass( const Math::Vector::Vector3& halfExtents )
{
    return ClampPositiveMass( CalculateBoxVolume( halfExtents ) * DEFAULT_FLOATING_OBJECT_DENSITY );
}

inline Math::Vector::Vector3 CalculateSphereInertia( float radius, float mass )
{
    radius = (std::max)( 0.0f, radius );
    mass = ClampPositiveMass( mass );
    const float moment = 0.4f * mass * radius * radius;
    return Math::Vector::Vector3( moment, moment, moment );
}

inline Math::Vector::Vector3 CalculateBoxInertiaForHalfExtents( const Math::Vector::Vector3& halfExtents, float mass )
{
    mass = ClampPositiveMass( mass );
    const float hx2 = halfExtents.x * halfExtents.x;
    const float hy2 = halfExtents.y * halfExtents.y;
    const float hz2 = halfExtents.z * halfExtents.z;
    const float m3 = mass / 3.0f;
    return Math::Vector::Vector3( m3 * ( hy2 + hz2 ), m3 * ( hx2 + hz2 ), m3 * ( hx2 + hy2 ) );
}
} // namespace Physics
} // namespace SkullbonezCore
