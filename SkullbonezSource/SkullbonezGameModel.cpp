// --- Includes ---
#include "SkullbonezGameModel.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezCollisionResponse.h"


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Environment;


GameModel::GameModel( WorldEnvironment* pWorldEnv,
                      const Vector3& vPosition,
                      const Vector3& vRotationalInertia,
                      float fMass )
{
    // check for valid world environment pointer
    if ( !pWorldEnv )
    {
        throw std::runtime_error( "Invalid world environment pointer supplied.  (GameModel::GameModel)" );
    }

    // set the important members
    m_worldEnvironment = pWorldEnv;
    m_physicsInfo.SetPosition( vPosition );
    m_physicsInfo.SetRotationalInertia( vRotationalInertia );
    m_physicsInfo.SetMass( fMass );
    m_physicsInfo.SetFrictionCoefficient( Cfg().frictionCoeff );

    // initialise pointers
    m_terrain = 0;

    // initialise other members
    m_projectedSurfaceArea = 0.0f;
    m_dragCoefficient = 0.0f;
    m_isResponseRequired = false;
    m_name[0] = '\0';
    m_isGrounded = false;
}


const BoundingSphere& GameModel::GetBoundingSphere() const
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


BoundingSphere& GameModel::GetBoundingSphere()
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


void GameModel::SetImpulseForce( const Vector3& vForce,
                                 const Vector3& vApplicationPoint )
{
    m_physicsInfo.SetImpulseForce( vForce, vApplicationPoint );
}


void GameModel::SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque )
{
    m_physicsInfo.SetWorldForce( vWorldForce, vWorldTorque );
}


void GameModel::SetCoefficientRestitution( float fCoefficientRestitution )
{
    m_physicsInfo.SetCoefficientRestitution( fCoefficientRestitution );
}


void GameModel::SetInitialOrientation( float fEulerXDeg, float fEulerYDeg, float fEulerZDeg )
{
    static constexpr float DEG2RAD = 3.14159265f / 180.0f;
    float x = fEulerXDeg * DEG2RAD;
    float y = fEulerYDeg * DEG2RAD;
    float z = fEulerZDeg * DEG2RAD;
    float xHalf = x * 0.5f;
    float yHalf = y * 0.5f;
    float zHalf = z * 0.5f;

    Quaternion xRotation( sinf( xHalf ), 0.0f, 0.0f, cosf( xHalf ) );
    Quaternion yRotation( 0.0f, sinf( yHalf ), 0.0f, cosf( yHalf ) );
    Quaternion zRotation( 0.0f, 0.0f, sinf( zHalf ), cosf( zHalf ) );

    Quaternion q;
    q *= xRotation * yRotation * zRotation;
    q.Normalise();
    m_physicsInfo.SetOrientation( q );
}


void GameModel::SetName( const char* name )
{
    strncpy_s( m_name, sizeof( m_name ), name, _TRUNCATE );
}


const char* GameModel::GetName() const
{
    return m_name;
}


void GameModel::SetGrounded( bool grounded )
{
    m_isGrounded = grounded;
}


bool GameModel::IsGrounded() const
{
    return m_isGrounded;
}


float GameModel::GetBoundingRadius()
{
    return GetBoundingSphere().GetBoundingRadius();
}


Vector3 GameModel::GetOrientationUp()
{
    // Returns the world-space up vector of the ball (local +Y after physics orientation).
    //
    // DERIVATION:
    //   GetModelMatrix() uses T * FromQuaternion(q) * Scale.
    //   The world-space up vector is col1 of FromQuaternion(q), which is:
    //     col1 = (xy2+wz2,  1-(xx2+zz2),  yz2-wx2)
    //          = (2(qx·qy + qw·qz),  1 - 2(qx² + qz²),  2(qy·qz - qw·qx))
    //
    //   This avoids building the full 4×4 matrix and extracting a column from it.

    float qx, qy, qz, qw;
    m_physicsInfo.GetOrientation().GetComponents( qx, qy, qz, qw );
    return Vector3(
        2.0f * ( qx * qy + qw * qz ),        // col1[0] = xy2+wz2
        1.0f - 2.0f * ( qx * qx + qz * qz ), // col1[1] = 1-(xx2+zz2)
        2.0f * ( qy * qz - qw * qx ) );      // col1[2] = yz2-wx2
}


void GameModel::AddBoundingSphere( float fRadius )
{
    m_boundingVolume = BoundingSphere( fRadius, Vector::ZERO_VECTOR );
    UpdateModelInfo();
}


float GameModel::GetDragCoefficient()
{
    return m_dragCoefficient;
}


float GameModel::GetProjectedSurfaceArea()
{
    return m_projectedSurfaceArea;
}


const Vector3& GameModel::GetVelocity()
{
    return m_physicsInfo.GetVelocity();
}


void GameModel::UpdateModelInfo()
{
    CalculateVolume();
    CalculateDragCoefficient();
    CalculateProjectedSurfaceArea();
}


float GameModel::GetModelCollisionTime( GameModel& collisionTarget,
                                        float changeInTime )
{
    // calculate the ray of the target
    Ray targetRay = CollisionResponse::CalculateRay( collisionTarget, changeInTime );

    // calculate the ray of the focus
    Ray focusRay = CollisionResponse::CalculateRay( *this, changeInTime );

    // Sphere-only fast path: bypass variant visitor dispatch in hot narrowphase.
    return GetBoundingSphere().TestCollision( collisionTarget.GetBoundingSphere(), targetRay, focusRay );
}


void GameModel::CollisionResponseGameModel( GameModel& responseTarget )
{
    // if there has been no collision, throw an exception!
    if ( !responseTarget.m_isResponseRequired || !m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot perform collision response when no collision has occured!  (GameModel::CollisionResponseGameModel)" );
    }

    // respond to the collision (velocity-only — m_position advancement handled by RunPhysics)
    CollisionResponse::RespondCollisionGameModels( *this, responseTarget );

    // clear response flags so both models can participate in further collisions this frame
    m_isResponseRequired = false;
    responseTarget.m_isResponseRequired = false;
}


void GameModel::StaticOverlapResponseGameModel( GameModel& overlapTarget )
{
    float thisRadius = GetBoundingSphere().GetBoundingRadius();
    float targetRadius = overlapTarget.GetBoundingSphere().GetBoundingRadius();

    Vector3 delta = overlapTarget.m_physicsInfo.GetPosition() - m_physicsInfo.GetPosition();
    float dist = Vector::VectorMag( delta );
    float radii = thisRadius + targetRadius;

    if ( dist >= radii || dist <= 0.0f )
    {
        return;
    }

    // spheres are overlapping — positional correction only
    // (skip full velocity response to avoid division by zero in angular
    // response when relative velocity is near-zero on slow balls)
    Vector3 axis = delta / dist;
    float halfOverlap = ( radii - dist ) * 0.5f;
    m_physicsInfo.SetPosition( m_physicsInfo.GetPosition() - axis * halfOverlap );
    overlapTarget.m_physicsInfo.SetPosition( overlapTarget.m_physicsInfo.GetPosition() + axis * halfOverlap );
}


void GameModel::CollisionResponseTerrain( float remainingTimeStep )
{
    // if there has been no collision, throw an exception!
    if ( !m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot perform collision response when no collision has occured!  (GameModel::CollisionResponseTerrain)" );
    }

    // respond to the collision...
    CollisionResponse::RespondCollisionTerrain( *this, remainingTimeStep );

    // update the m_position based on remaining time step
    UpdatePosition( remainingTimeStep );

    // set the collided collision object to null now the reaction has taken place
    m_isResponseRequired = false;
}


bool GameModel::IsResponseRequired()
{
    return m_isResponseRequired;
}


Matrix4 GameModel::GetModelMatrix()
{
    // Natural model transform: T(worldPos) * FromQuaternion(q) * Scale(radius).
    // Sphere mesh local frame is pre-rotated at build time, so no runtime visual
    // yaw compatibility shim is required.
    Matrix4 rotation = Matrix4::FromQuaternion( m_physicsInfo.GetOrientation() );
    return GetBoundingSphere().GetModelMatrix( m_physicsInfo.GetPosition(), rotation );
}


void GameModel::ApplyForces( float changeInTime )
{
    // throttle the angular velocity
    m_physicsInfo.ThrottleAngularVelocity();

    // apply the world forces
    ApplyWorldForces( changeInTime );

    // apply the forces to the model
    m_physicsInfo.ApplyForces();
}


void GameModel::ApplyWorldForces( float changeInTime )
{
    // apply the world forces now we know the pointer is valid
    m_worldEnvironment->AddWorldForces( *this, changeInTime );
}


void GameModel::CalculateProjectedSurfaceArea()
{
    // return the average submerged percentage
    m_projectedSurfaceArea = GetBoundingSphere().GetProjectedSurfaceArea();
}


void GameModel::CalculateDragCoefficient()
{
    // return the average submerged percentage
    m_dragCoefficient = GetBoundingSphere().GetDragCoefficient();
}


float GameModel::GetVolume()
{
    return m_physicsInfo.GetVolume();
}


void GameModel::UpdatePosition( float changeInTime )
{
    // update m_position based on airbourne model
    m_physicsInfo.UpdatePosition( changeInTime );

    // slam the ball to the m_terrain m_height if it has fallen below
    DEBUG_SetSphereToTerrain();
}


void GameModel::CalculateVolume()
{
    m_physicsInfo.SetVolume( GetBoundingSphere().GetVolume() );
}


float GameModel::GetMass()
{
    return m_physicsInfo.GetMass();
}


const Vector3& GameModel::GetAngularVelocity()
{
    return m_physicsInfo.GetAngularVelocity();
}


void GameModel::SetTerrain( Terrain* pTerrain )
{
    m_terrain = pTerrain;
}


float GameModel::GetTerrainCollisionTime( float changeInTime )
{
    // calculate the ray for the current dynamics object
    m_responseInformation.testingRay = CollisionResponse::CalculateRay( *this, changeInTime );

    // offset the origin by the bounding radius (for spheres, this is the sphere radius)
    float bottomOffset = GetBoundingSphere().GetBoundingRadius();

    // if out of bounds, no collision has occured
    if ( !m_terrain->IsInBounds( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z ) )
    {
        return NO_COLLISION;
    }

    // Cache-backed terrain lookup: one query returns the exact collision plane and
    // height for this XZ position, avoiding per-frame LocatePolygon + ComputePlane.
    float terrainHeight = 0.0f;
    m_terrain->GetTerrainHeightAndPlaneAt( m_physicsInfo.GetPosition().x,
                                           m_physicsInfo.GetPosition().z,
                                           terrainHeight,
                                           m_responseInformation.testingPlane );
    float gap = m_physicsInfo.GetPosition().y - bottomOffset - terrainHeight;
    if ( gap <= Cfg().contactEpsilon )
    {
        m_responseInformation.collisionTime = 0.0f;
        return 0.0f;
    }

    // if the dynamics object is stationary and not in contact, no collision will occur
    if ( m_responseInformation.testingRay.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    // offset the ray origin for swept test
    m_responseInformation.testingRay.origin.y -= bottomOffset;

    // save the collision time
    m_responseInformation.collisionTime = GeometricMath::CalculateIntersectionTime( m_responseInformation.testingPlane, m_responseInformation.testingRay );

    // return the point in time where the collision has occured
    return m_responseInformation.collisionTime;
}


float GameModel::CollisionDetectTerrain( float changeInTime )
{
    // ensure m_terrain pointer is valid
    if ( !m_terrain )
    {
        throw std::runtime_error( "Terrain pointer not valid!  (GameModel::CollisionDetectTerrain)" );
    }

    // check to ensure pending responses have been responded to
    if ( m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot detect collision when a response is required first!  (GameModel::CollisionDetectTerrain)" );
    }

    float collisionTime = GetTerrainCollisionTime( changeInTime );

    // if no collision in this time frame
    if ( collisionTime > 1.0f || collisionTime < ZERO_TAKE_TOLERANCE )
    {
        // allow full time to be applied as no collision will occur
        collisionTime = changeInTime;
    }
    else
    {
        // perform the cap - cap time to be applied by converting collision from time ratio to actual seconds
        collisionTime *= changeInTime;

        // set response required flag to true
        m_isResponseRequired = true;

        // store the response information
        m_responseInformation.collidedPlane = m_responseInformation.testingPlane;
        m_responseInformation.collidedRay = m_responseInformation.testingRay;
    }

    // return when the collision will occur
    return collisionTime;
}


float GameModel::CollisionDetectGameModel( GameModel& collisionTarget,
                                           float changeInTime )
{
    // if there is a collision pending to be responded to between one of the two models
    if ( m_isResponseRequired || collisionTarget.m_isResponseRequired )
    {
        // throw an exception!
        throw std::runtime_error( "Cannot detect collision when a response is required first!  (GameModel::CollisionDetectGameModel)" );
    }

    // get the time of collision
    float collisionTime = GetModelCollisionTime( collisionTarget, changeInTime );

    // if no collision in this time frame
    if ( collisionTime > 1.0f || collisionTime < ZERO_TAKE_TOLERANCE )
    {
        // allow full time to be applied as no collision will occur
        collisionTime = changeInTime;
    }
    else
    {
        // perform the cap - cap time to be applied by converting collision from time ratio to actual seconds
        collisionTime *= changeInTime;
        m_isResponseRequired = true;
        collisionTarget.m_isResponseRequired = true;
    }

    // return when the collision will occur
    return collisionTime;
}


const Vector3& GameModel::GetPosition()
{
    return m_physicsInfo.GetPosition();
}


void GameModel::DEBUG_SetSphereToTerrain()
{
    // if we are not in bounds then exit now!
    if ( !m_terrain->IsInBounds( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z ) )
    {
        return;
    }

    // get the total m_radius
    float rad = GetBoundingSphere().GetBoundingRadius();

    // get the m_height of the m_terrain at the current XZ m_position
    float m_height = m_terrain->GetTerrainHeightAt( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z );

    // if we are lower than the m_terrain
    if ( m_physicsInfo.GetPosition().y - rad < m_height )
    {
        // work out the m_position we will update the model to
        Vector3 updatePos( m_physicsInfo.GetPosition().x,
                           m_height + rad,
                           m_physicsInfo.GetPosition().z );

        // update the m_position
        m_physicsInfo.SetPosition( updatePos );
    }
}


float GameModel::GetSubmergedVolumePercent()
{
    float m_fluidSurfaceHeight = m_worldEnvironment->GetFluidSurfaceHeight();
    float totalPercentage = GetBoundingSphere().GetSubmergedVolumePercent( m_fluidSurfaceHeight - m_physicsInfo.GetPosition().y );

    // return the submerged percentage
    return totalPercentage;
}


const Quaternion& GameModel::GetOrientation() const
{
    return m_physicsInfo.GetOrientation();
}


const Vector3& GameModel::GetRotationalInertia()
{
    return m_physicsInfo.GetRotationalInertia();
}


float GameModel::GetCoefficientRestitution()
{
    return m_physicsInfo.GetCoefficientRestitution();
}


void GameModel::SetLinearVelocity( const Vector3& v )
{
    m_physicsInfo.SetLinearVelocity( v );
}


void GameModel::SetAngularVelocity( const Vector3& v )
{
    m_physicsInfo.SetAngularVelocity( v );
}


void GameModel::SetOrientation( const Quaternion& q )
{
    m_physicsInfo.SetOrientation( q );
}
