/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezGameModel.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezCollisionResponse.h"

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Environment;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
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
    m_isGrounded = false;
    m_isResponseRequired = false;
}

/* -- APPLY FORCE -----------------------------------------------------------------*/
void GameModel::SetImpulseForce( const Vector3& vForce,
                                 const Vector3& vApplicationPoint )
{
    m_physicsInfo.SetImpulseForce( vForce, vApplicationPoint );
}

/* -- SET WORLD FORCE -------------------------------------------------------------*/
void GameModel::SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque )
{
    m_physicsInfo.SetWorldForce( vWorldForce, vWorldTorque );
}

/* -- SET COEFFICIENT RESTITUTION -------------------------------------------------*/
void GameModel::SetCoefficientRestitution( float fCoefficientRestitution )
{
    m_physicsInfo.SetCoefficientRestitution( fCoefficientRestitution );
}

/* -- GET BOUNDING RADIUS ---------------------------------------------------------*/
float GameModel::GetBoundingRadius( void )
{
    return GetShapeBoundingRadius( m_boundingVolume );
}

/* -- ADD BOUNDING SPHERE ---------------------------------------------------------*/
void GameModel::AddBoundingSphere( float fRadius )
{
    m_boundingVolume = BoundingSphere( fRadius, Vector::ZERO_VECTOR );
    this->UpdateModelInfo();
}

/* -- GET DRAG COEFFICIENT --------------------------------------------------------*/
float GameModel::GetDragCoefficient( void )
{
    return m_dragCoefficient;
}

/* -- GET PROJECTED SURFACE AREA --------------------------------------------------*/
float GameModel::GetProjectedSurfaceArea( void )
{
    return m_projectedSurfaceArea;
}

/* -- GET VELOCITY ----------------------------------------------------------------*/
const Vector3& GameModel::GetVelocity( void )
{
    return m_physicsInfo.GetVelocity();
}

/* -- UPDATE MODEL INFO -----------------------------------------------------------*/
void GameModel::UpdateModelInfo( void )
{
    this->CalculateVolume();
    this->CalculateDragCoefficient();
    this->CalculateProjectedSurfaceArea();
}

/* -- GET MODEL COLLISION TIME ----------------------------------------------------*/
float GameModel::GetModelCollisionTime( GameModel& collisionTarget,
                                        float changeInTime )
{
    // calculate the ray of the target
    Ray targetRay = CollisionResponse::CalculateRay( collisionTarget, changeInTime );

    // calculate the ray of the focus
    Ray focusRay = CollisionResponse::CalculateRay( *this, changeInTime );

    // perform the collision test
    return TestShapeCollision( m_boundingVolume,
                               collisionTarget.m_boundingVolume,
                               focusRay,
                               targetRay );
}

/* -- COLLISION RESPONSE GAME MODEL -----------------------------------------------*/
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

/* -- STATIC OVERLAP RESPONSE GAME MODEL ------------------------------------------*/
void GameModel::StaticOverlapResponseGameModel( GameModel& overlapTarget )
{
    float thisRadius = GetShapeBoundingRadius( m_boundingVolume );
    float targetRadius = GetShapeBoundingRadius( overlapTarget.m_boundingVolume );

    Vector3 delta = overlapTarget.m_physicsInfo.GetPosition() - m_physicsInfo.GetPosition();
    float dist = Vector::VectorMag( delta );
    float radii = thisRadius + targetRadius;

    if ( dist >= radii || dist <= 0.0f )
    {
        return;
    }

    // spheres are overlapping — positional correction only
    // (skip full velocity response to avoid division by zero in angular
    // response when relative velocity is near-zero on grounded m_balls)
    Vector3 axis = delta / dist;
    float halfOverlap = ( radii - dist ) * 0.5f;
    m_physicsInfo.SetPosition( m_physicsInfo.GetPosition() - axis * halfOverlap );
    overlapTarget.m_physicsInfo.SetPosition( overlapTarget.m_physicsInfo.GetPosition() + axis * halfOverlap );
}

/* -- COLLISION RESPONSE TERRAIN --------------------------------------------------*/
void GameModel::CollisionResponseTerrain( float remainingTimeStep )
{
    // if there has been no collision, throw an exception!
    if ( !m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot perform collision response when no collision has occured!  (GameModel::CollisionResponseTerrain)" );
    }

    // respond to the collision...
    CollisionResponse::RespondCollisionTerrain( *this );

    // update the m_position based on remaining time step
    this->UpdatePosition( remainingTimeStep );

    // set the collided collision object to null now the reaction has taken place
    m_isResponseRequired = false;
}

/* -- IS RESPONSE REQUIRED --------------------------------------------------------*/
bool GameModel::IsResponseRequired( void )
{
    return m_isResponseRequired;
}

/* -- RENDER COLLISION BOUNDS -----------------------------------------------------*/
Matrix4 GameModel::GetRenderMatrix( void ) const
{
    Matrix4 rotation = Matrix4::FromQuaternion( m_physicsInfo.GetOrientation() );
    return std::visit( [&]( const auto& s )
                       { return s.GetRenderMatrix( m_physicsInfo.GetPosition(), rotation ); },
                       m_boundingVolume );
}

void GameModel::RenderCollisionBounds( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    // Build the rotation matrix from the physics m_orientation quaternion.
    // The bounding m_volume renders with: T(pos) * R * T(localOffset) * S(m_radius).
    Matrix4 rotation = Matrix4::FromQuaternion( m_physicsInfo.GetOrientation() );
    DEBUG_RenderShapeCollisionVolume( m_boundingVolume, m_physicsInfo.GetPosition(), rotation, view, proj, lightPos );
}

/* -- UPDATE VELOCITY -------------------------------------------------------------*/
void GameModel::ApplyForces( float changeInTime )
{
    // throttle the angular velocity
    m_physicsInfo.ThrottleAngularVelocity();

    // apply the world forces
    this->ApplyWorldForces( changeInTime );

    // apply the forces to the model
    m_physicsInfo.ApplyForces();
}

/* -- APPLY WORLD FORCES ----------------------------------------------------------*/
void GameModel::ApplyWorldForces( float changeInTime )
{
    // apply the world forces now we know the pointer is valid
    m_worldEnvironment->AddWorldForces( *this, changeInTime );
}

/* -- CALCULATE PROJECTED SURFACE AREA --------------------------------------------*/
void GameModel::CalculateProjectedSurfaceArea( void )
{
    // return the average submerged percentage
    m_projectedSurfaceArea = GetShapeProjectedSurfaceArea( m_boundingVolume );
}

/* -- CALCULATE DRAG COEFFICIENT --------------------------------------------------*/
void GameModel::CalculateDragCoefficient( void )
{
    // return the average submerged percentage
    m_dragCoefficient = GetShapeDragCoefficient( m_boundingVolume );
}

/* -- GET VOLUME ------------------------------------------------------------------*/
float GameModel::GetVolume( void )
{
    return m_physicsInfo.GetVolume();
}

/* -- UPDATE POSITION -------------------------------------------------------------*/
void GameModel::UpdatePosition( float changeInTime )
{
    // update m_position based on airbourne model
    m_physicsInfo.UpdatePosition( changeInTime );

    // slam the ball to the m_terrain m_height if it has fallen below
    this->DEBUG_SetSphereToTerrain();
}

/* -- UPDATE POSITION -------------------------------------------------------------*/
void GameModel::CalculateVolume( void )
{
    m_physicsInfo.SetVolume( GetShapeVolume( m_boundingVolume ) );
}

/* -- GET MASS --------------------------------------------------------------------*/
float GameModel::GetMass( void )
{
    return m_physicsInfo.GetMass();
}

/* -- GET ANGULAR VELOCITY --------------------------------------------------------*/
const Vector3& GameModel::GetAngularVelocity( void )
{
    return m_physicsInfo.GetAngularVelocity();
}

/* -- SET TERRAIN -----------------------------------------------------------------*/
void GameModel::SetTerrain( Terrain* pTerrain )
{
    m_terrain = pTerrain;
}

/* -- IS GROUNDED -----------------------------------------------------------------*/
bool GameModel::IsGrounded( void )
{
    return m_isGrounded;
}

/* -- SET IS GROUNDED -------------------------------------------------------------*/
void GameModel::SetIsGrounded( bool fIsGrounded )
{
    m_isGrounded = fIsGrounded;
}

/* -- GET TERRAIN COLLISION TIME ---------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::GetTerrainCollisionTime( float changeInTime )
{
    // calculate the ray for the current dynamics object
    m_responseInformation.testingRay = CollisionResponse::CalculateRay( *this, changeInTime );

    // if the dynamics object is stationary, no collision will occur
    if ( m_responseInformation.testingRay.vector3.IsCloseToZero() )
    {
        return NO_COLLISION;
    }

    // the origin will be in a different m_position based on the geometrical shape of the object
    // offset the origin by the bounding radius (for spheres, this is the sphere radius)
    m_responseInformation.testingRay.origin.y -= GetShapeTerrainBottomOffset( m_boundingVolume );

    // if out of bounds, no collision has occured
    if ( !m_terrain->IsInBounds( m_responseInformation.testingRay.origin.x, m_responseInformation.testingRay.origin.z ) )
    {
        return NO_COLLISION;
    }

    // store the plane vertically aligned with the object...
    m_responseInformation.testingPlane = GeometricMath::ComputePlane( m_terrain->LocatePolygon( m_responseInformation.testingRay.origin.x,
                                                                                                m_responseInformation.testingRay.origin.z ) );

    // if the ball is grounded then it has already hit the ground
    if ( m_isGrounded )
    {
        m_responseInformation.collisionTime = 0.0f;
        return 0.0f;
    }

    // save the collision time
    m_responseInformation.collisionTime = GeometricMath::CalculateIntersectionTime( m_responseInformation.testingPlane, m_responseInformation.testingRay );

    // return the point in time where the collision has occured (yes, we do save this to a member but it is more intuitive just to return the value as well)
    return m_responseInformation.collisionTime;
}

/* -- COLLISION DETECT TERRAIN -----------------------------------------------------------------------------------------------------------------------------------------*/
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

    float collisionTime = this->GetTerrainCollisionTime( changeInTime );

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

/* -- COLLISION DETECT GAME MODEL --------------------------------------------------------------------------------------------------------------------------------------*/
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
    float collisionTime = this->GetModelCollisionTime( collisionTarget, changeInTime );

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

/* -- GET POSITION -----------------------------------------------------------------------------------------------------------------------------------------------------*/
const Vector3& GameModel::GetPosition( void )
{
    return m_physicsInfo.GetPosition();
}

/* -- DEBUG_SET SPHERE TO TERRAIN --------------------------------------------------------------------------------------------------------------------------------------*/
void GameModel::DEBUG_SetSphereToTerrain( void )
{
    // if we are not in bounds then exit now!
    if ( !m_terrain->IsInBounds( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z ) )
    {
        return;
    }

    // get the total m_radius
    float rad = GetShapeBoundingRadius( m_boundingVolume );

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

/* -- GET SUBMERGED VOLUME PERCENT --------------------------------------------------------------------------------------------------------------------------------------*/
float GameModel::GetSubmergedVolumePercent( void )
{
    float m_fluidSurfaceHeight = m_worldEnvironment->GetFluidSurfaceHeight();
    float totalPercentage = GetShapeSubmergedVolumePercent( m_boundingVolume, m_fluidSurfaceHeight - m_physicsInfo.GetPosition().y );

    // return the submerged percentage
    return totalPercentage;
}
