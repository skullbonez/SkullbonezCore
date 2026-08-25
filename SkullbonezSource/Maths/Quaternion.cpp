/*
File: SkullbonezSource/Maths/Quaternion.cpp
Purpose:
  Implements quaternion orientation math for rigid bodies and cameras.

Summary:
  Quaternion implements normalized rigid-body and camera orientation updates,
  with world-axis deltas composed in the engine's active-rotation order. The
  shared axis-angle entry point uses repository-owned deterministic cosine and
  sine because Physics reaches it through ragdoll correction.

Glossary:
  World-axis delta: Incremental rotation expressed around a world-space axis
    and composed before the current orientation.

Invariants:
  - Orientation quaternions should remain normalized before conversion to
    matrices or solver rows.
  - RotateAboutAxis requires a normalized world-space axis and interprets its
    angle in radians.
  - Axis-angle construction uses DeterministicMath, so its evaluation order can
    alter byte-exact Physics baselines.
  - World-axis deltas left-multiply the current orientation so call order stays
    visible in active-rotation space.

Related:
  - SkullbonezSource/Maths/Quaternion.h
  - SkullbonezSource/Maths/DeterministicMath.h
  - Agentic/Reference/engine-glossary.md
*/
#include "Quaternion.h"

#include "DeterministicMath.h"

#include <algorithm>
#include <cassert>
#include <cmath>


using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


Quaternion::Quaternion() : m_x( 0.0f ), m_y( 0.0f ), m_z( 0.0f ), m_w( 1.0f ) // Identity quaternion — no rotation
{
}


Quaternion::Quaternion( float x, float y, float z, float w ) : m_x( x ), m_y( y ), m_z( z ), m_w( w )
{
}


void Quaternion::Identity()
{
    m_x = 0.0f;
    m_y = 0.0f;
    m_z = 0.0f;
    m_w = 1.0f;
}


void Quaternion::Normalise()
{
    const float magSq = m_w * m_w + m_x * m_x + m_y * m_y + m_z * m_z;

    // Guard against zero or near-zero magnitude quaternions that can arise from
    // pathological floating-point cancellation during collision impulse resolution.
    // Reset to identity rather than crash so the object retains a valid orientation.
    static constexpr float EPSILON_SQ = 1e-12f;

    if ( magSq < EPSILON_SQ )
    {
        Identity();
        return;
    }

    if ( std::isfinite( magSq ) )
    {
        // Compatibility: ordinary finite magnitudes retain the established
        // component order and exact output bytes.
        const float oneOverMag = 1.0f / sqrtf( magSq );

        m_w *= oneOverMag;
        m_x *= oneOverMag;
        m_y *= oneOverMag;
        m_z *= oneOverMag;
        return;
    }

    if ( !std::isfinite( m_w ) || !std::isfinite( m_x ) || !std::isfinite( m_y ) || !std::isfinite( m_z ) )
    {
        Identity();
        return;
    }

    const float largestComponent = (std::max)( (std::max)( fabsf( m_w ), fabsf( m_x ) ),
                                                (std::max)( fabsf( m_y ), fabsf( m_z ) ) );
    const float scaledW = m_w / largestComponent;
    const float scaledX = m_x / largestComponent;
    const float scaledY = m_y / largestComponent;
    const float scaledZ = m_z / largestComponent;
    const float oneOverScaledMagnitude =
        1.0f / sqrtf( scaledW * scaledW + scaledX * scaledX + scaledY * scaledY + scaledZ * scaledZ );

    // Hazard: squaring a large finite component can overflow to infinity.
    // Normalize its bounded ratios so the orientation remains a unit value.
    m_w = scaledW * oneOverScaledMagnitude;
    m_x = scaledX * oneOverScaledMagnitude;
    m_y = scaledY * oneOverScaledMagnitude;
    m_z = scaledZ * oneOverScaledMagnitude;
}


void Quaternion::RotateAboutAxis( const Vector3& axis, float angle )
{

#ifdef _DEBUG
    // Hazard: normalizing the composed quaternion cannot recover the requested
    // angle when the axis length scales only delta.xyz. Preserve Release math,
    // but expose caller misuse in Debug like Vector3::Normalise.
    const float axisMagnitudeSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    assert( fabsf( axisMagnitudeSquared - 1.0f ) <= TOLERANCE && "Quaternion::RotateAboutAxis requires a normalized axis" );
#endif

    // A positive right-handed world-axis delta left-multiplies the current
    // orientation. Hamilton composition then exposes the same order as the
    // corresponding active matrices: R(delta * current) = R(delta)R(current).
    const float halfAngle = angle * 0.5f;
    const Deterministic::CosSin cosSin = Deterministic::ComputeCosSin( halfAngle );
    Quaternion delta( axis.x * cosSin.sine, axis.y * cosSin.sine, axis.z * cosSin.sine, cosSin.cosine );
    *this = delta * *this;
    Normalise();
}


RotationMatrix Quaternion::GetOrientationMatrix() const
{
    // The engine uses right-handed object orientation math; render projection is
    // handled separately by Matrix4.
    return RotationMatrix( 1 - ( 2 * m_y * m_y ) - ( 2 * m_z * m_z ), ( 2 * m_x * m_y ) - ( 2 * m_w * m_z ),
                           ( 2 * m_x * m_z ) + ( 2 * m_w * m_y ), ( 2 * m_x * m_y ) + ( 2 * m_w * m_z ),
                           1 - ( 2 * m_x * m_x ) - ( 2 * m_z * m_z ), ( 2 * m_y * m_z ) - ( 2 * m_w * m_x ),
                           ( 2 * m_x * m_z ) - ( 2 * m_w * m_y ), ( 2 * m_y * m_z ) + ( 2 * m_w * m_x ),
                           1 - ( 2 * m_x * m_x ) - ( 2 * m_y * m_y ) );
}


Quaternion Quaternion::operator*( const Quaternion& q ) const
{
    Quaternion result;

    result.m_w = m_w * q.m_w - m_x * q.m_x - m_y * q.m_y - m_z * q.m_z;

    result.m_x = m_w * q.m_x + m_x * q.m_w + m_y * q.m_z - m_z * q.m_y;

    result.m_y = m_w * q.m_y - m_x * q.m_z + m_y * q.m_w + m_z * q.m_x;

    result.m_z = m_w * q.m_z + m_x * q.m_y - m_y * q.m_x + m_z * q.m_w;

    return result;
}


Quaternion& Quaternion::operator*=( const Quaternion& q )
{
    *this = *this * q;
    return *this;
}


void Quaternion::GetComponents( float& x, float& y, float& z, float& w ) const
{
    x = m_x;
    y = m_y;
    z = m_z;
    w = m_w;
}
