/*
File: SkullbonezSource/Maths/Quaternion.h
Purpose:
  Declares quaternion orientation math for rigid bodies and cameras.

Summary:
  Quaternion.h declares quaternion orientation math for rigid bodies and
  cameras. As a public header, keep edits anchored on units, basis
  conventions, and numerical assumptions and on the glossary/invariants below.

Glossary:
  Quaternion: Four-component rotation representation that avoids gimbal lock
  during incremental orientation updates.
  Identity quaternion: No-rotation value (0,0,0,1); multiplying by it leaves an
  orientation unchanged.
  Euler decomposition: Splitting rotation into ordered X/Y/Z angles; this file
  avoids depending on that order for incremental angular displacement.
  Shortest nlerp: Normalized linear interpolation that first chooses the
    quaternion sign representing the shorter equivalent rotation arc.

Invariants:
  - IDENTITY_QUATERNION is the no-rotation sentinel and must stay (0,0,0,1).
  - Orientation interpolation normalizes its result and clamps alpha to [0,1].
  - RotateAboutAxis requires a normalized world-space axis and interprets angle
    in radians.

Related:
  - SkullbonezSource/Maths/Quaternion.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include "MathsCommon.h"
#include "Vector3.h"
#include "RotationMatrix.h"

#include <algorithm>

namespace SkullbonezCore
{
namespace Math
{
namespace Orientation
{

/* -- Quaternion
---------------------------------------------------------------------------------------------------------------------------------------------

    Represents a quaternion to express orientation in 3d space.
-------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class Quaternion
{

  public:
    Quaternion();                                                     // Initializes to identity orientation.
    Quaternion( float fX, float fY, float fZ, float fW );             // Explicit component construction for deserialization/math helpers.
    ~Quaternion() = default;
    void Identity();                                                  // Resets orientation to the no-rotation value.
    void Normalise();                                                 // Removes floating-point drift before conversion to matrices or solver rows.

    // Precondition: axis is normalized. Debug asserts on misuse; Release keeps
    // the existing unchecked arithmetic and post-composition normalization.
    void RotateAboutAxis( const Vector::Vector3& axis, float angle ); // Applies angle radians about a world-space axis.

    // Returns the active-rotation matrix for this Hamilton quaternion.
    Transformation::RotationMatrix GetOrientationMatrix() const;

    // Standard Hamilton order: lhs * rhs applies rhs first, then lhs.
    Quaternion operator*( const Quaternion& q ) const;
    Quaternion& operator*=( const Quaternion& q );                    // In-place rotation composition; caller normalizes if drift matters.
    void GetComponents( float& x, float& y, float& z,
                        float& w ) const;                             // Exposes raw components for deterministic serialization.

  private:
    float m_x, m_y, m_z, m_w;                                         // Stored as vector part xyz plus scalar w.
};

// One program-wide no-rotation value shared by every including translation unit.
inline const Quaternion IDENTITY_QUATERNION( 0.0f, 0.0f, 0.0f, 1.0f );

inline void ConjugateQuaternionVectorPart( float& x, float& y, float& z ) noexcept
{

    // Invariant: IEEE-754 negation changes only the sign bit. Applying this
    // migration twice therefore restores every finite component bit-for-bit,
    // including signed zero and subnormal values.
    x = -x;
    y = -y;
    z = -z;
}

inline Quaternion NlerpShortest( const Quaternion& previous, const Quaternion& current, float alpha )
{
    float previousX = 0.0f;
    float previousY = 0.0f;
    float previousZ = 0.0f;
    float previousW = 1.0f;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float currentZ = 0.0f;
    float currentW = 1.0f;
    previous.GetComponents( previousX, previousY, previousZ, previousW );
    current.GetComponents( currentX, currentY, currentZ, currentW );

    // Concept: q and -q describe the same orientation. Negating the current
    // endpoint when their dot product is negative keeps normalized linear
    // interpolation on the shorter rotation arc instead of spinning the long way.
    const float dot = previousX * currentX + previousY * currentY + previousZ * currentZ + previousW * currentW;

    if ( dot < 0.0f )
    {
        currentX = -currentX;
        currentY = -currentY;
        currentZ = -currentZ;
        currentW = -currentW;
    }

    const float t = std::clamp( alpha, 0.0f, 1.0f );
    Quaternion blended( previousX + ( currentX - previousX ) * t, previousY + ( currentY - previousY ) * t,
                        previousZ + ( currentZ - previousZ ) * t, previousW + ( currentW - previousW ) * t );
    blended.Normalise();
    return blended;
}
} // namespace Orientation
} // namespace Math
} // namespace SkullbonezCore
