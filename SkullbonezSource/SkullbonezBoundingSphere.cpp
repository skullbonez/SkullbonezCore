// --- Includes ---
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezBoundingBox.h"
#include <immintrin.h> // SSE intrinsics for scale pass in GetModelMatrix


// --- Usings ---
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;


BoundingSphere::BoundingSphere()
    : m_radius( 0.0f )
{
}


BoundingSphere::BoundingSphere( float fRadius,
                                const Vector3& vPosition )
    : m_position( vPosition ), m_radius( fRadius )
{
}


float BoundingSphere::CollisionDetect( const BoundingSphere& target,
                                       const Ray& targetRay,
                                       const Ray& focusRay ) const
{
    // Relative motion this frame (focus in target space).
    Vector3 relativeMovement = focusRay.vector3 - targetRay.vector3;
    float relativeMovementSq = relativeMovement * relativeMovement;
    if ( relativeMovementSq <= TOLERANCE * TOLERANCE )
    {
        return NO_COLLISION;
    }

    // Relative center vector from target to focus at frame start.
    Vector3 difference = focusRay.origin - targetRay.origin;
    float centerDistanceSq = difference * difference;
    float radiusSum = target.m_radius + m_radius;
    float radiusSumSq = radiusSum * radiusSum;

    // Already overlapping: skip swept solve and let static overlap handling resolve contact.
    if ( centerDistanceSq <= radiusSumSq )
    {
        return NO_COLLISION;
    }

    // If relative motion is separating, no swept impact can occur this frame.
    float closingDot = difference * relativeMovement;
    if ( closingDot >= 0.0f )
    {
        return NO_COLLISION;
    }

    // Cheap reachability cull: even if moving directly together for the whole frame,
    // they cannot reach contact distance.
    float maxTravel = sqrtf( relativeMovementSq );
    float reachRadius = radiusSum + maxTravel;
    if ( centerDistanceSq > reachRadius * reachRadius )
    {
        return NO_COLLISION;
    }

    // Solve |difference + relativeMovement * t|^2 = radiusSum^2 for earliest t in [0,1].
    float a = relativeMovementSq;
    float b = closingDot;
    float c = centerDistanceSq - radiusSumSq;
    float discriminant = b * b - a * c;
    if ( discriminant < 0.0f )
    {
        return NO_COLLISION;
    }

    return ( -b - sqrtf( discriminant ) ) / a;
}


float BoundingSphere::TestCollision( const BoundingSphere& target,
                                     const Ray& targetRay,
                                     const Ray& focusRay ) const
{
    return CollisionDetect( target, targetRay, focusRay );
}


float BoundingSphere::GetRadius() const
{
    return m_radius;
}


float BoundingSphere::GetBoundingRadius() const
{
    return m_radius;
}


const Vector3& BoundingSphere::GetPosition() const
{
    return m_position;
}


Matrix4 BoundingSphere::GetModelMatrix( const Vector3& worldPos, const Matrix4& rotation ) const
{
    // Builds the full TRS model matrix: T(worldPos) * rotation * T(m_position) * Scale(radius).
    //
    // SIMPLIFICATION: m_position is always ZERO_VECTOR in this engine — every sphere's local
    // offset is zero because GameModel::AddBoundingSphere() hard-codes it to ZERO_VECTOR.
    // T(m_position) is therefore identity and the chain collapses to:
    //   T(worldPos) * rotation * Scale(radius)
    //
    // This means the final matrix is just 'rotation' with each of its three direction columns
    // (col0, col1, col2) uniformly scaled by m_radius, and col3 replaced by worldPos.
    // No matrix multiply is needed at all.
#ifdef _DEBUG
    // Debug: full formula — makes T(m_position) visible even though it's always identity.
    return Matrix4::Translate( worldPos ) * rotation * Matrix4::Translate( m_position ) *
           Matrix4::Scale( m_radius, m_radius, m_radius );
#else
    // Release/Profile: 3 SSE scale passes + direct col3 write.
    //
    // Each pass loads one 4-float column of 'rotation' (16-byte-unaligned is fine with
    // _mm_loadu_ps), multiplies all 4 lanes by the broadcast scalar m_radius, and stores
    // the result directly into the output array.  col3 (the translation column) is set
    // from worldPos with no SSE needed — it's only 4 scalar writes.
    //
    // SSE INTRINSICS USED:
    //   _mm_set1_ps(s)         — broadcast s into all 4 lanes: (s, s, s, s)
    //   _mm_loadu_ps(ptr)      — load 4 floats from unaligned memory
    //   _mm_mul_ps(a, b)       — lane-wise multiply: (a0*b0, a1*b1, a2*b2, a3*b3)
    //   _mm_storeu_ps(ptr, r)  — store 4 floats to unaligned memory

    const __m128 rr = _mm_set1_ps( m_radius ); // broadcast radius
    float res[16];
    _mm_storeu_ps( res + 0, _mm_mul_ps( _mm_loadu_ps( rotation.m + 0 ), rr ) ); // col0 * radius
    _mm_storeu_ps( res + 4, _mm_mul_ps( _mm_loadu_ps( rotation.m + 4 ), rr ) ); // col1 * radius
    _mm_storeu_ps( res + 8, _mm_mul_ps( _mm_loadu_ps( rotation.m + 8 ), rr ) ); // col2 * radius
    res[12] = worldPos.x;                                                       // col3: translation
    res[13] = worldPos.y;
    res[14] = worldPos.z;
    res[15] = 1.0f; // homogeneous w
    return Matrix4( res );
#endif
}


float BoundingSphere::GetVolume() const
{
    // m_volume of sphere = 4/3 * PI * m_radius^3
    return FOUR_OVER_THREE * _PI * m_radius * m_radius * m_radius;
}


float BoundingSphere::GetSubmergedVolumePercent( float m_fluidSurfaceHeight ) const
{
    if ( m_position.y - m_radius >= m_fluidSurfaceHeight )
    {
        // not touching fluid
        return 0.0f;
    }
    else if ( m_position.y + m_radius <= m_fluidSurfaceHeight )
    {
        // totally submerged in fluid
        return 1.0f;
    }
    else
    {
        /*
            Partially submerged in fluid, return submerged proportion

            Volume for partially submerged sphere:
            v = 1/3 * PI * (3r-y)y^2

            Formula from: http://vps.arachnoid.com/calculus/volume1.html
        */
        float yValue = m_fluidSurfaceHeight - ( m_position.y - m_radius );
        return ( ( ( ONE_OVER_THREE * _PI *
                     ( ( 3.0f * m_radius ) - yValue ) *
                     yValue * yValue ) ) /
                 GetVolume() );
    }
}


float BoundingSphere::GetDragCoefficient() const
{
    return Cfg().sphereDragCoeff;
}


float BoundingSphere::GetProjectedSurfaceArea() const
{
    // Area of circle = PI * r^2
    return _PI * m_radius * m_radius;
}


// Sphere vs Box swept test: approximate box as bounding sphere for broadphase.
// Precise OBB-sphere tests are done in the narrowphase collision response.
float BoundingSphere::TestCollision( const BoundingBox& target, const Ray& targetRay, const Ray& focusRay ) const
{
    float combinedRadius = m_radius + target.GetBoundingRadius();
    float combinedRadiusSq = combinedRadius * combinedRadius;

    Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;
    float totalMovementSq = VectorMagSquared( totalMovement );

    if ( totalMovementSq < TOLERANCE )
    {
        Vector3 delta = ( targetRay.origin + target.GetPosition() ) -
                        ( focusRay.origin + m_position );
        if ( VectorMagSquared( delta ) <= combinedRadiusSq )
        {
            return 0.0f;
        }
        return NO_COLLISION;
    }

    Vector3 d = ( focusRay.origin + m_position ) - ( targetRay.origin + target.GetPosition() );
    float totalMovementMag = sqrtf( totalMovementSq );
    Vector3 moveDir = totalMovement / totalMovementMag;

    float dDotMoveDir = d * moveDir;
    float discriminant = dDotMoveDir * dDotMoveDir - ( VectorMagSquared( d ) - combinedRadiusSq );

    if ( discriminant < 0.0f )
    {
        return NO_COLLISION;
    }

    float t = ( dDotMoveDir - sqrtf( discriminant ) ) / totalMovementMag;
    if ( t < 0.0f || t > 1.0f )
    {
        return NO_COLLISION;
    }

    return t;
}
