// --- Includes ---
#include "SkullbonezBoundingSphere.h"


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
    // calculate the total movement
    Vector3 totalMovement = targetRay.vector3 - focusRay.vector3;

    // if the total movement is insignificant, exit early
    if ( totalMovement.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    // calculate the total displacement of the total movement
    float displacement = Vector::VectorMag( totalMovement );

    // calculate the difference vector between both bounding spheres
    Vector3 difference = focusRay.origin - targetRay.origin;

    // dot product the difference vector
    float diffDotDiff = difference * difference;

    // sum the radii of the bounding spheres
    float radiusSum = target.m_radius + m_radius;

    // square the sums of the radii
    float radiusSumSq = radiusSum * radiusSum;

    // normalise the total movement vector to dispose its magnitude
    totalMovement.Normalise();

    // dot the difference vector with the normalised total movement vector
    float diffDotMoveDir = difference * totalMovement;

    // square the result from above
    float diffDotMoveDirSq = diffDotMoveDir * diffDotMoveDir;

    // store part of the quadratic theorem result into a temp variable
    float tmp = diffDotMoveDirSq + radiusSumSq - diffDotDiff;

    // ensure no square root of a negative will be
    // performed - if this was going to be attempted, there
    // will be no collision in ANY given time frame
    if ( tmp < 0.0f )
    {
        return NO_COLLISION;
    }

    // calculate the negative root via quadratic formula
    float collisionStartDist = diffDotMoveDir - sqrtf( tmp );

    // return the proportionate time in which the collision started
    return collisionStartDist / displacement;
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
    return Matrix4::Translate( worldPos ) * rotation * Matrix4::Translate( m_position ) * Matrix4::Scale( m_radius, m_radius, m_radius );
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
