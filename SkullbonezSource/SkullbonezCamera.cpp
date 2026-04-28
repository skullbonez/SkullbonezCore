// --- Includes ---
#include "SkullbonezCamera.h"
#include "SkullbonezRotationMatrix.h"


// --- Usings ---
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Math;


Camera::Camera()
{
}


void Camera::SetAll( const Vector3& vPosition,  // Set m_position
                     const Vector3& vView,      // Set view
                     const Vector3& vUpVector ) // Set up vector
{
    m_position = vPosition;
    m_view = vView;
    m_upVector = vUpVector;

    // set initial view vector magnitude
    m_viewMagnitude = Vector::Distance( m_position, m_view );

    // init movement buffer
    m_movementBuffer.Zero();

    // normalise this in case it was not supplied as a unit vector
    // (do not normalise the zero vector though)
    if ( m_upVector != Vector::ZERO_VECTOR )
    {
        m_upVector.Normalise();
    }

    // init is locked mode to false by default
    m_isLockedMode = false;

    // init vars to do with view vector magnitude preservation
    m_isFinishedTranslationRecursed = false;
    m_doPreserveViewMagnitude = false;

    // view magnitude must be initially calculated
    m_doCalculateViewMagnitude = true;

    // init the boundary to something massive until the user changes it
    m_boundary.m_xMin = -99999.9f;
    m_boundary.m_xMax = 99999.9f;
    m_boundary.m_zMin = -99999.9f;
    m_boundary.m_zMax = 99999.9f;
}


void Camera::MoveCamera( const TravelDirection enumDir,
                         float fQuantity )
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
            if ( Vector::Distance( m_position, m_view ) < Cfg().minViewMag )
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
            if ( Vector::Distance( m_position, m_view ) > Cfg().maxViewMag )
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


void Camera::ApplyMovementBuffer()
{
    // store old m_position
    Vector3 oldPosition = m_position;

    // apply the movement vector to the camera m_position
    PrepareTranslation();
    m_position += m_movementBuffer;
    FinishTranslation();

    // get actual translation (movementBuffer may have been restricted)
    Vector3 actualTranslation = m_position - oldPosition;

    // update the view vector with the m_cameras translation, however,
    // if we are in locked mode we do not move the view vector at all
    if ( !m_isLockedMode )
    {
        m_view += actualTranslation;
    }

    // reset the movement buffer
    m_movementBuffer.Zero();
}


void Camera::RotateCamera( float xMove, float yMove )
{
    // caps the y rotation quantity to avoid view-up collisions
    float yMoveCapped = UpVectorViewVectorRotationCap( yMove );

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
        m_view = m_position +
                 Transformation::RotatePointAboutArbitrary( xMove,
                                                            m_upVector,
                                                            GetViewVectorRaw() );

        // the mouses yMove will always represent a pivot on the right vector
        // (repsective to the camera translation)
        m_view = m_position +
                 Transformation::RotatePointAboutArbitrary( yMoveCapped,
                                                            GetRightVector(),
                                                            GetViewVectorRaw() );
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
        proposedTranslation = m_view +
                              Transformation::RotatePointAboutArbitrary( xMove,
                                                                         m_upVector,
                                                                         -GetViewVectorRaw() );

        // store proposed translation into the movement buffer
        m_movementBuffer += proposedTranslation - m_position;

        // the mouses yMove will always represent a pivot on the right vector
        // (repsective to the view point)
        proposedTranslation = m_view +
                              Transformation::RotatePointAboutArbitrary( yMoveCapped,
                                                                         GetRightVector(),
                                                                         -GetViewVectorRaw() );

        // store proposed translation into the movement buffer
        m_movementBuffer += proposedTranslation - m_position;
    }
}


void Camera::PrepareTranslation()
{
    // store the current X and Z m_position before translation
    // (we want to revert the translation if bounds are exceeded)
    m_xzStore.x = m_position.x;
    m_xzStore.z = m_position.z;
}


void Camera::FinishTranslation()
{
    bool isOnBoundX = false;
    bool isOnBoundZ = false;

    // reposition X on a bound violation
    if ( m_position.x < m_boundary.m_xMin + Cfg().minCameraHeight )
    {
        m_position.x = m_boundary.m_xMin + Cfg().minCameraHeight;
    }
    else if ( m_position.x > m_boundary.m_xMax - Cfg().minCameraHeight )
    {
        m_position.x = m_boundary.m_xMax - Cfg().minCameraHeight;
    }

    // set if X is on a boundary
    isOnBoundX = ( ( m_position.x ==
                     m_boundary.m_xMin + Cfg().minCameraHeight ) ||
                   ( m_position.x ==
                     m_boundary.m_xMax - Cfg().minCameraHeight ) );

    // reposition Z on a bound violation
    if ( m_position.z < m_boundary.m_zMin + Cfg().minCameraHeight )
    {
        m_position.z = m_boundary.m_zMin + Cfg().minCameraHeight;
    }
    else if ( m_position.z > m_boundary.m_zMax - Cfg().minCameraHeight )
    {
        m_position.z = m_boundary.m_zMax - Cfg().minCameraHeight;
    }

    // set if Z is on a boundary
    isOnBoundZ = ( ( m_position.z ==
                     m_boundary.m_zMin + Cfg().minCameraHeight ) ||
                   ( m_position.z ==
                     m_boundary.m_zMax - Cfg().minCameraHeight ) );

    // if we have recursed once already
    if ( m_isFinishedTranslationRecursed )
    {
        // reset the flag
        m_isFinishedTranslationRecursed = false;

        // and exit the function
        return;
    }

    // if the flag has been set to recalculate the view magnitude
    if ( m_doCalculateViewMagnitude )
    {
        // recalculate it
        m_viewMagnitude = Vector::Distance( m_position, m_view );

        // reset the flag to false
        m_doCalculateViewMagnitude = false;
    }
    else
    {
        // test to see if we need to recover lost view magnitude
        RecoverViewMagnitude( isOnBoundX, isOnBoundZ );
    }
}


void Camera::RecoverViewMagnitude( const bool isOnBoundX, const bool isOnBoundZ )
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
        // calculate the current view vector magnitude
        float viewMagTmp = Vector::Distance( m_position, m_view );

        // if the current view magnitude is under quota
        if ( viewMagTmp < m_viewMagnitude )
        {
            // store the m_cameras m_position
            Vector3 positionStore = m_position;

            // extend the current view magnitude to its quota
            m_position = m_view +
                         ( -GetViewVectorNormalised() * m_viewMagnitude );

            // restore the y component (we will add to this later)
            m_position.y = positionStore.y;

            // keep track of which component gets modified
            bool isModifiedComponentX;

            // if neither X or Z are on their boundary
            if ( !isOnBoundX && !isOnBoundZ )
            {
                // determine component distances from m_boundaries
                float dxMin = positionStore.x - m_boundary.m_xMin + Cfg().minCameraHeight;
                float dxMax = m_boundary.m_xMax - Cfg().minCameraHeight - positionStore.x;
                float dzMin = positionStore.z - m_boundary.m_zMin + Cfg().minCameraHeight;
                float dzMax = m_boundary.m_zMax - Cfg().minCameraHeight - positionStore.z;

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
            FinishTranslation();

            // check to see which component ended up getting modified
            if ( isModifiedComponentX )
            {
                // if X was the modified component
                // work out how much it was modified in total
                float dx = positionStore.x - m_position.x;

                // get the absolute value representing the difference of X
                dx = abs( dx );

                // add this to the current Y translation component
                m_position.y += dx;
            }
            else
            {
                // if Z was the modified component
                // work out how much it was modified in total
                float dz = positionStore.z - m_position.z;

                // get the absolute value representing the difference of Z
                dz = abs( dz );

                // add this to the current Y translation component
                m_position.y += dz;
            }
        }
        // if the current view magnitude is over quota
        else if ( viewMagTmp > m_viewMagnitude )
        {
            // cap the magnitude to what it is set to
            m_view = m_position +
                     ( GetViewVectorNormalised() * m_viewMagnitude );
        }
    }
}


void Camera::ApplyDelta( const Camera& delta )
{
    PrepareTranslation();
    *this += delta;
    FinishTranslation();
}


float Camera::UpVectorViewVectorRotationCap( float requestRadians )
{
    // get the negated view vector (translation minus view)
    Vector3 vNegatedView = -GetViewVectorNormalised();

    // get the amount of radians the view is to the up vector
    float currentUpAngle = acosf( vNegatedView * m_upVector );

    // get the amount of radians the view is to the 'down vector'
    float currentDownAngle = acosf( vNegatedView * -m_upVector );

    // pre-detect up-vector view-vector collision, return a capped rotation angle
    if ( currentUpAngle - requestRadians < Cfg().cameraCollisionThreshold )
    {
        return currentUpAngle - Cfg().cameraCollisionThreshold;
    }

    // pre-detect down-vector view-vector collision, return a capped rotation angle
    // NOTE:  request radians will be negative, and if required should be returned
    // as a negative value
    if ( currentDownAngle + requestRadians < Cfg().cameraCollisionThreshold )
    {
        return -( currentDownAngle - Cfg().cameraCollisionThreshold );
    }

    // no collisions have been detected, return the requested rotation amount
    return requestRadians;
}


Vector3 Camera::GetRightVector()
{
    // Get the right vector (cross of view and up vectors)
    Vector3 vRight = Vector::CrossProduct( GetViewVectorNormalised(),
                                           m_upVector );

    // Normalise
    vRight.Normalise();

    // Return
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

    // Get the view vector
    Vector3 vView = m_view - m_position;

    // Normalise
    vView.Normalise();

    // Return
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
    result.SetAll( m_position - target.m_position,
                   m_view - target.m_view,
                   m_upVector - target.m_upVector );

    return result;
}


Camera Camera::operator*( float f )
{
    Camera result;
    result.SetAll( m_position * f,
                   m_view * f,
                   m_upVector * f );

    return result;
}
