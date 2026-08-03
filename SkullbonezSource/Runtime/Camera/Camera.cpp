/*
File: SkullbonezSource/Runtime/Camera/Camera.cpp
Purpose:
  Stores camera pose and builds view/projection transforms for rendering.

Summary:
  Camera keeps one eye, look-at target, up vector, and bounded movement buffer;
  CameraCollection applies policy and publishes the repaired pose to rendering.

Glossary:
  Look-at target: World point the camera faces; subtracting the eye produces
    the view direction.
  Basis fallback: Stable world axis used when authored eye/view/up data cannot
    form a direction.
  Pitch cap: Angular limit that keeps the view direction from crossing either
    pole of the camera up axis.

Invariants:
  - m_view is a look-at target, not a direction vector; movement updates both
    eye and target when the camera translates.
  - m_viewMagnitude tracks the eye-to-target distance when orbit-style updates
    need to preserve the current zoom.
  - Degenerate authored poses use deterministic forward/right fallbacks and
    never terminate inside vector math.
  - Pitch-cap dot products are clamped to the inverse-cosine unit domain before
    comparisons, so a rounded pole cannot disable the cap through NaN.

Related:
  - SkullbonezSource/Runtime/Camera/Camera.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "Camera.h"
#include "../../Maths/RotationMatrix.h"


using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Geometry;


Camera::Camera()
    : m_position( 0.0f, 0.0f, 0.0f ), m_view( 0.0f, 0.0f, -1.0f ), m_upVector( 0.0f, 1.0f, 0.0f ),
      m_movementBuffer( 0.0f, 0.0f, 0.0f ), m_viewMagnitude( 1.0f ), m_isFinishedTranslationRecursed( false ),
      m_doCalculateViewMagnitude( false ), m_doPreserveViewMagnitude( false ), m_isLockedMode( false )
{
}


void Camera::SetAll( const Vector3& vPosition, const Vector3& vView, const Vector3& vUpVector )
{
    m_position = vPosition;
    m_view = vView;
    m_upVector = vUpVector;

    m_viewMagnitude = Vector::Distance( m_position, m_view );

    m_movementBuffer.Zero();

    // A zero authored up vector stays zero here; downstream camera queries own
    // their deterministic basis fallback rather than terminating in math.

    if ( !m_upVector.TryNormalise() )
    {
        m_upVector = Vector::ZERO_VECTOR;
    }

    m_isLockedMode = false;

    m_isFinishedTranslationRecursed = false;
    m_doPreserveViewMagnitude = false;

    // view magnitude must be initially calculated
    m_doCalculateViewMagnitude = true;

    // Start effectively unbounded; scenes can tighten the camera range later.
    m_boundary.m_xMin = -99999.9f;
    m_boundary.m_xMax = 99999.9f;
    m_boundary.m_zMin = -99999.9f;
    m_boundary.m_zMax = 99999.9f;
}


void Camera::MoveCamera( const TravelDirection enumDir, float fQuantity, const CameraMovementSettings& settings )
{

    // declare local variable to store movement results
    Vector3 movementResults = Vector::ZERO_VECTOR;

    switch ( enumDir )
    {
    case TravelDirection::Forward:

        if ( m_isLockedMode )
        {

            // in locked mode we only want to be able to translate the camera
            // within a certain m_distance to the view point, so here we test to
            // ensure this rule is not violated

            if ( Vector::Distance( m_position, m_view ) < settings.minViewMag )
            {
                return;
            }

            // if there has been a change in view magnitude, we need to recalculate
            m_doCalculateViewMagnitude = true;
        }

        // movement result is along the view vector, positive direction
        movementResults = GetViewVectorNormalised() * fQuantity;
        break;

    case TravelDirection::Left:

        // left movement does not exist in locked mode

        if ( !m_isLockedMode )
        {

            // movement result is along the right vector, negative direction
            movementResults = GetRightVector() * -fQuantity;
        }

        break;

    case TravelDirection::Right:

        // right movement does not exist in locked mode

        if ( !m_isLockedMode )
        {

            // movement result is along the right vector, positive direction
            movementResults = GetRightVector() * fQuantity;
        }

        break;

    case TravelDirection::Backward:

        if ( m_isLockedMode )
        {

            // in locked mode we only want to be able to translate the camera
            // within a certain m_distance from the view point, so here we test to
            // ensure this rule is not violated

            if ( Vector::Distance( m_position, m_view ) > settings.maxViewMag )
            {
                return;
            }

            // if there has been a change in view magnitude, we need to recalculate
            m_doCalculateViewMagnitude = true;
        }

        // movement result is along the view vector, negative direction
        movementResults = GetViewVectorNormalised() * -fQuantity;
    }

    m_movementBuffer += movementResults;
}


void Camera::ApplyMovementBuffer( const CameraMovementSettings& settings )
{
    Vector3 oldPosition = m_position;

    PrepareTranslation();
    m_position += m_movementBuffer;
    FinishTranslation( settings );

    Vector3 actualTranslation = m_position - oldPosition;

    // Locked mode translates the eye without moving the look target.

    if ( !m_isLockedMode )
    {
        m_view += actualTranslation;
    }

    m_movementBuffer.Zero();
}


void Camera::RotateCamera( float xMove, float yMove, const CameraMovementSettings& settings )
{

    // caps the y rotation quantity to avoid view-up collisions
    float yMoveCapped = UpVectorViewVectorRotationCap( yMove, settings );

    // the following code will move the view vector - this is not allowed
    // in locked mode

    if ( !m_isLockedMode )
    {

        /*
            NOTE: view member is set to original m_position plus our rotation result
            vector - this translates our new view vector into a point relative
            to the camera m_position

            ALSO: note that the normalised view vector is rotated - this is because
            in free camera mode, the view vector is always unit vector length from
            the m_cameras translation
        */

        // the mouses xMove will always represent a pivot on the up vector
        // (repsective to the camera translation)
        m_view = m_position + Transformation::RotatePointAboutArbitrary( xMove, m_upVector, GetViewVectorRaw() );

        // the mouses yMove will always represent a pivot on the right vector
        // (repsective to the camera translation)
        m_view = m_position + Transformation::RotatePointAboutArbitrary( yMoveCapped, GetRightVector(), GetViewVectorRaw() );
    }
    else
    {

        /*
            NOTE: m_position member is set to original view plus our rotation result
            vector - this translates our new m_position vector into a point relative
            to the camera view

            ALSO: the raw negated view vector is used here as a rotation subject
            because in locked camera mode, the m_distance the camera is from the view
            point is variable.  The view vector is negated to represent view point
            to camera translation opposed to camera to view point.
        */

        // local to add up translation proposal
        Vector3 proposedTranslation;

        // the mouses xMove will always represent a pivot on the up vector
        // (repsective to the view point)
        proposedTranslation = m_view + Transformation::RotatePointAboutArbitrary( xMove, m_upVector, -GetViewVectorRaw() );

        m_movementBuffer += proposedTranslation - m_position;

        // the mouses yMove will always represent a pivot on the right vector
        // (repsective to the view point)
        proposedTranslation = m_view + Transformation::RotatePointAboutArbitrary( yMoveCapped, GetRightVector(),
                                                                                  -GetViewVectorRaw() );

        m_movementBuffer += proposedTranslation - m_position;
    }
}


void Camera::PrepareTranslation()
{

    // Bounds recovery needs the pre-translation X/Z in case clamping moves the
    // camera back to an edge.
    m_xzStore.x = m_position.x;
    m_xzStore.z = m_position.z;
}


void Camera::FinishTranslation( const CameraMovementSettings& settings )
{
    bool isOnBoundX = false;
    bool isOnBoundZ = false;

    // reposition X on a bound violation

    if ( m_position.x < m_boundary.m_xMin + settings.minCameraHeight )
    {
        m_position.x = m_boundary.m_xMin + settings.minCameraHeight;
    }
    else if ( m_position.x > m_boundary.m_xMax - settings.minCameraHeight )
    {
        m_position.x = m_boundary.m_xMax - settings.minCameraHeight;
    }

    isOnBoundX = ( ( m_position.x == m_boundary.m_xMin + settings.minCameraHeight ) ||
                   ( m_position.x == m_boundary.m_xMax - settings.minCameraHeight ) );

    // reposition Z on a bound violation

    if ( m_position.z < m_boundary.m_zMin + settings.minCameraHeight )
    {
        m_position.z = m_boundary.m_zMin + settings.minCameraHeight;
    }
    else if ( m_position.z > m_boundary.m_zMax - settings.minCameraHeight )
    {
        m_position.z = m_boundary.m_zMax - settings.minCameraHeight;
    }

    isOnBoundZ = ( ( m_position.z == m_boundary.m_zMin + settings.minCameraHeight ) ||
                   ( m_position.z == m_boundary.m_zMax - settings.minCameraHeight ) );

    // if we have recursed once already

    if ( m_isFinishedTranslationRecursed )
    {
        m_isFinishedTranslationRecursed = false;

        return;
    }

    // if the flag has been set to recalculate the view magnitude

    if ( m_doCalculateViewMagnitude )
    {
        m_viewMagnitude = Vector::Distance( m_position, m_view );

        m_doCalculateViewMagnitude = false;
    }
    else
    {

        // test to see if we need to recover lost view magnitude
        RecoverViewMagnitude( isOnBoundX, isOnBoundZ, settings );
    }
}


void Camera::RecoverViewMagnitude( const bool isOnBoundX, const bool isOnBoundZ, const CameraMovementSettings& settings )
{

    // only recover view magnitude if the camera has been set to do so

    if ( !m_doPreserveViewMagnitude )
    {
        return;
    }

    // only recover view magnitude when in locked mode

    if ( !m_isLockedMode )
    {
        return;
    }

    // if we are not out of bounds on either the X or Z axis

    if ( !isOnBoundX || !isOnBoundZ )
    {
        float viewMagTmp = Vector::Distance( m_position, m_view );

        // if the current view magnitude is under quota

        if ( viewMagTmp < m_viewMagnitude )
        {
            Vector3 positionStore = m_position;

            // extend the current view magnitude to its quota
            m_position = m_view + ( -GetViewVectorNormalised() * m_viewMagnitude );

            // restore the y component (we will add to this later)
            m_position.y = positionStore.y;

            // keep track of which component gets modified
            bool isModifiedComponentX;

            // if neither X or Z are on their boundary

            if ( !isOnBoundX && !isOnBoundZ )
            {

                // determine component distances from m_boundaries
                float dxMin = positionStore.x - m_boundary.m_xMin + settings.minCameraHeight;
                float dxMax = m_boundary.m_xMax - settings.minCameraHeight - positionStore.x;
                float dzMin = positionStore.z - m_boundary.m_zMin + settings.minCameraHeight;
                float dzMax = m_boundary.m_zMax - settings.minCameraHeight - positionStore.z;

                // determine closest boundary per component
                float dx = ( dxMin < dxMax ) ? dxMin : dxMax;
                float dz = ( dzMin < dzMax ) ? dzMin : dzMax;

                // restore the component who is closest to their closest boundary

                if ( dx > dz )
                {

                    // restore X component
                    m_position.x = positionStore.x;

                    // X component is NOT being modified
                    isModifiedComponentX = false;
                }
                else
                {

                    // restore Z component
                    m_position.z = positionStore.z;

                    // X component IS being modified
                    isModifiedComponentX = true;
                }
            }
            else if ( isOnBoundZ )
            {

                // restore X if already maxed
                m_position.x = positionStore.x;

                // X component is NOT being modified
                isModifiedComponentX = false;
            }
            else
            {

                // restore Z if already maxed
                m_position.z = positionStore.z;

                // X component IS being modified
                isModifiedComponentX = true;
            }

            // specify we have recursed this function
            m_isFinishedTranslationRecursed = true;

            // indirectly recurse this function to cap the vector we have
            // just applied to m_position
            FinishTranslation( settings );

            if ( isModifiedComponentX )
            {
                float dx = positionStore.x - m_position.x;

                dx = abs( dx );

                // Preserve view distance by converting the clamped X loss into
                // a vertical lift.
                m_position.y += dx;
            }
            else
            {
                float dz = positionStore.z - m_position.z;

                dz = abs( dz );

                // Preserve view distance by converting the clamped Z loss into
                // a vertical lift.
                m_position.y += dz;
            }
        }

        // if the current view magnitude is over quota
        else if ( viewMagTmp > m_viewMagnitude )
        {

            // cap the magnitude to what it is set to
            m_view = m_position + ( GetViewVectorNormalised() * m_viewMagnitude );
        }
    }
}


float Camera::UpVectorViewVectorRotationCap( float requestRadians, const CameraMovementSettings& settings )
{
    Vector3 vNegatedView = -GetViewVectorNormalised();
    Vector3 effectiveUp = m_upVector;

    if ( !effectiveUp.TryNormalise() )
    {

        // A missing authored up basis uses world Y for pitch policy while
        // GetRightVector supplies world X as the matching rotation axis.
        effectiveUp = Vector3( 0.0f, 1.0f, 0.0f );
    }

    // Hazard: normalized float dots can round beyond either unit-domain pole.
    // Unclamped acosf returns NaN, every cap comparison becomes false, and the
    // raw request crosses the singularity this function exists to prevent.
    const float upDot = Math::ClampUnit( Dot( vNegatedView, effectiveUp ) );
    const float downDot = Math::ClampUnit( Dot( vNegatedView, -effectiveUp ) );
    float currentUpAngle = acosf( upDot );
    float currentDownAngle = acosf( downDot );

    // pre-detect up-vector view-vector collision, return a capped rotation angle

    if ( currentUpAngle - requestRadians < settings.cameraCollisionThreshold )
    {
        return currentUpAngle - settings.cameraCollisionThreshold;
    }

    // pre-detect down-vector view-vector collision, return a capped rotation angle
    // NOTE:  request radians will be negative, and if required should be returned
    // as a negative value

    if ( currentDownAngle + requestRadians < settings.cameraCollisionThreshold )
    {
        return -( currentDownAngle - settings.cameraCollisionThreshold );
    }

    // no collisions have been detected, return the requested rotation amount
    return requestRadians;
}


Vector3 Camera::GetRightVector()
{
    Vector3 vRight = Vector::CrossProduct( GetViewVectorNormalised(), m_upVector );

    if ( !vRight.TryNormalise() )
    {

        // Fallback: coincident/parallel camera axes use world +X as a stable
        // right direction until the next valid pose arrives.
        vRight = Vector3( 1.0f, 0.0f, 0.0f );
    }

    return vRight;
}


Vector3 Camera::GetViewVectorRaw()
{
    return m_view - m_position;
}


Vector3 Camera::GetViewVectorNormalised()
{

    /*
        Recall that vector subtraction works the following way:

                a = (-1, 2)
                b = ( 1, 2)

                c = b-a (for -> direction)
                c = a-b (for <- direction)
                -----------------------
                \		   |+y	      /
                 \		   |	  	 /
                  \		   |	    /
                   \	   |	   /
                    \	   |	  /
                   a \	   |	 / b
                      \    |    /
                       \   |   /
                        \  |  /
                         \ | /
                          \|/(0, 0)
         -x	---------------|------------- +x
                           |-y

        The value of c is given by b-a = (1-(-1), 2-2)
        Therefore c = (2, 0)

        As the member m_position corresponds to a point in 3d space
        where the camera is sitting, and the member m_view represents
        a point (NOT a vector) in 3d space where the camera is looking
        directly at, in order to find the vector representing the direction
        in which we are looking (so, the vector representing the path FROM
        the point m_position TO the point m_view) we treat m_view
        and m_position as vectors beginning at the origin.  We now have a
        3d case similar to the 2d image above - two vectors starting at the same
        m_position representing displacement to other points in space.  In order to
        find the vector that joints the two endpoints, we use vector subtraction
        (the triangle rule).

        Remember, vector subtraction is NOT COMMUTITIVE, it is important that we
        have subtracted m_view - m_position to find the current view, as
        m_position - m_view would be looking in exactly the opposite
        direction to where we want to be looking (see image).
    */

    Vector3 vView = m_view - m_position;

    if ( !vView.TryNormalise() )
    {

        // Fallback: a camera looking at its own position keeps the engine's
        // conventional forward direction instead of publishing NaNs.
        vView = Vector3( 0.0f, 0.0f, -1.0f );
    }

    return vView;
}


void Camera::ZeroCamera()
{
    m_position.Zero();
    m_view.Zero();
    m_upVector.Zero();
}


Camera& Camera::operator=( const Camera& target )
{
    m_position = target.m_position;
    m_view = target.m_view;
    m_upVector = target.m_upVector;
    m_isLockedMode = target.m_isLockedMode;

    return *this;
}


Camera& Camera::operator+=( const Camera& target )
{
    m_position += target.m_position;
    m_view += target.m_view;
    m_upVector += target.m_upVector;

    return *this;
}


Camera Camera::operator-( const Camera& target )
{
    Camera result;
    result.SetAll( m_position - target.m_position, m_view - target.m_view, m_upVector - target.m_upVector );

    return result;
}


Camera Camera::operator*( float f )
{
    Camera result;
    result.SetAll( m_position * f, m_view * f, m_upVector * f );

    return result;
}
