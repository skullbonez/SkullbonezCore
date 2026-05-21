// --- Includes ---
#include "SkullbonezGameModel.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezCollisionResponse.h"
#include "SkullbonezImpulseSolver.h"


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

    // Immutable body properties are read repeatedly in broadphase/narrowphase and
    // terrain response. Cache them once at construction to keep hot loops on plain
    // scalar loads instead of repeated getter/ratio work.
    m_ballPhysics.mass = fMass;
    m_ballPhysics.invMass = 1.0f / fMass;
    m_ballPhysics.rotationalInertia = vRotationalInertia;
    m_ballPhysics.invRotationalInertia = Vector3( 1.0f / vRotationalInertia.x,
                                                  1.0f / vRotationalInertia.y,
                                                  1.0f / vRotationalInertia.z );
    m_ballPhysics.radius = 0.0f;
    m_ballPhysics.radiusSq = 0.0f;
    m_ballPhysics.volume = 0.0f;
    m_ballPhysics.invVolume = 0.0f;
    m_ballPhysics.projectedSurfaceArea = 0.0f;
    m_ballPhysics.dragCoefficient = 0.0f;

    // initialise pointers
    m_terrain = 0;

    // initialise other members
    m_projectedSurfaceArea = 0.0f;
    m_dragCoefficient = 0.0f;
    m_isResponseRequired = false;
    m_name[0] = '\0';
    m_isGrounded = false;
}


void GameModel::BuildSpherePhysicsCache( float radius )
{
    if ( radius <= 0.0f )
    {
        throw std::runtime_error( "Bounding sphere radius must be greater than zero.  (GameModel::BuildSpherePhysicsCache)" );
    }

    m_ballPhysics.radius = radius;
    m_ballPhysics.radiusSq = radius * radius;
    m_ballPhysics.volume = FOUR_OVER_THREE * _PI * m_ballPhysics.radiusSq * radius;
    m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
    m_ballPhysics.projectedSurfaceArea = _PI * m_ballPhysics.radiusSq;
    m_ballPhysics.dragCoefficient = Cfg().sphereDragCoeff;
}


const BoundingSphere& GameModel::GetBoundingSphere() const
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


BoundingSphere& GameModel::GetBoundingSphere()
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


bool GameModel::IsBox() const
{
    return std::holds_alternative<BoundingBox>( m_boundingVolume );
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
    return m_ballPhysics.radius;
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
    BuildSpherePhysicsCache( fRadius );
    m_boundingVolume = BoundingSphere( fRadius, Vector::ZERO_VECTOR );
    UpdateModelInfo();
}


void GameModel::AddBoundingBox( const Vector3& halfExtents )
{
    if ( halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f )
    {
        throw std::runtime_error( "Bounding box half-extents must all be greater than zero.  (GameModel::AddBoundingBox)" );
    }

    // Compute box inertia tensor: I_xx = m/3 * (hy² + hz²) for half-extents
    float mass = m_physicsInfo.GetMass();
    float hx2 = halfExtents.x * halfExtents.x;
    float hy2 = halfExtents.y * halfExtents.y;
    float hz2 = halfExtents.z * halfExtents.z;
    float mOver3 = mass / 3.0f;
    Vector3 inertia( mOver3 * ( hy2 + hz2 ),
                     mOver3 * ( hx2 + hz2 ),
                     mOver3 * ( hx2 + hy2 ) );

    // Populate physics cache using bounding radius as "radius" (for broadphase)
    float boundRadius = sqrtf( hx2 + hy2 + hz2 );
    m_ballPhysics.radius = boundRadius;
    m_ballPhysics.radiusSq = boundRadius * boundRadius;
    m_ballPhysics.volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
    m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
    m_ballPhysics.projectedSurfaceArea = ( 4.0f * halfExtents.x * halfExtents.y +
                                           4.0f * halfExtents.x * halfExtents.z +
                                           4.0f * halfExtents.y * halfExtents.z ) /
                                         3.0f;
    m_ballPhysics.dragCoefficient = 1.05f;
    m_ballPhysics.mass = mass;
    m_ballPhysics.invMass = 1.0f / mass;
    m_ballPhysics.rotationalInertia = inertia;
    m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );

    m_physicsInfo.SetRotationalInertia( inertia );
    m_boundingVolume = BoundingBox( halfExtents, Vector::ZERO_VECTOR );
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

    // Dispatch collision test via the variant visitor (handles sphere-sphere, sphere-box, box-box)
    return TestShapeCollision( m_boundingVolume, collisionTarget.m_boundingVolume, focusRay, targetRay );
}


void GameModel::CollisionResponseGameModel( GameModel& responseTarget )
{
    // if there has been no collision, throw an exception!
    if ( !responseTarget.m_isResponseRequired || !m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot perform collision response when no collision has occured!  (GameModel::CollisionResponseGameModel)" );
    }

    // respond to the collision (velocity-only — m_position advancement handled by RunPhysics)
    ImpulseSolver::RespondCollisionGameModels( *this, responseTarget );

    // clear response flags so both models can participate in further collisions this frame
    m_isResponseRequired = false;
    responseTarget.m_isResponseRequired = false;
}


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

    // Objects are overlapping — positional correction only
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
    ImpulseSolver::RespondCollisionTerrain( *this, remainingTimeStep );

    // update the m_position based on remaining time step
    UpdatePosition( remainingTimeStep );

    // set the collided collision object to null now the reaction has taken place
    m_isResponseRequired = false;
}


bool GameModel::IsResponseRequired()
{
    return m_isResponseRequired;
}


void GameModel::ClearResponseRequired()
{
    m_isResponseRequired = false;
}


Matrix4 GameModel::GetModelMatrix()
{
    // Natural model transform: T(worldPos) * FromQuaternion(q) * Scale(size).
    // Sphere mesh local frame is pre-rotated at build time, so no runtime visual
    // yaw compatibility shim is required.
    Matrix4 rotation = Matrix4::FromQuaternion( m_physicsInfo.GetOrientation() );
    Vector3 pos = m_physicsInfo.GetPosition();
    return std::visit( [&]( auto& shape )
                       { return shape.GetModelMatrix( pos, rotation ); },
                       m_boundingVolume );
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
    m_projectedSurfaceArea = m_ballPhysics.projectedSurfaceArea;
}


void GameModel::CalculateDragCoefficient()
{
    m_dragCoefficient = m_ballPhysics.dragCoefficient;
}


float GameModel::GetVolume()
{
    return m_ballPhysics.volume;
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
    m_physicsInfo.SetVolume( m_ballPhysics.volume );
}


float GameModel::GetMass()
{
    return m_ballPhysics.mass;
}


float GameModel::GetInvertedMass()
{
    return m_ballPhysics.invMass;
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

    // if out of bounds, no collision has occured
    if ( !m_terrain->IsInBounds( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z ) )
    {
        return NO_COLLISION;
    }

    // For boxes, check the lowest vertex against terrain instead of using sphere radius offset.
    // For spheres, use the classic single-point test.
    bool isBox = std::holds_alternative<BoundingBox>( m_boundingVolume );
    float bottomOffset;

    if ( isBox )
    {
        // Find the lowest world-space vertex of the OBB
        const BoundingBox& box = std::get<BoundingBox>( m_boundingVolume );
        const Vector3& he = box.GetHalfExtents();
        Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
        Vector3 pos = m_physicsInfo.GetPosition();

        float lowestY = pos.y;
        for ( int v = 0; v < 8; ++v )
        {
            Vector3 local(
                ( v & 1 ) ? he.x : -he.x,
                ( v & 2 ) ? he.y : -he.y,
                ( v & 4 ) ? he.z : -he.z );
            Vector3 worldVert = pos + ( rotMat * local );
            if ( worldVert.y < lowestY )
            {
                lowestY = worldVert.y;
            }
        }
        bottomOffset = pos.y - lowestY;
    }
    else
    {
        bottomOffset = m_ballPhysics.radius;
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

    // For boxes: use the lowest world-space vertex to determine the minimum center height.
    // For spheres: use the cached radius.
    float bottomOffset;
    if ( std::holds_alternative<BoundingBox>( m_boundingVolume ) )
    {
        const BoundingBox& box = std::get<BoundingBox>( m_boundingVolume );
        const Vector3& he = box.GetHalfExtents();
        Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
        Vector3 pos = m_physicsInfo.GetPosition();

        float lowestY = pos.y;
        for ( int v = 0; v < 8; ++v )
        {
            Vector3 local(
                ( v & 1 ) ? he.x : -he.x,
                ( v & 2 ) ? he.y : -he.y,
                ( v & 4 ) ? he.z : -he.z );
            Vector3 worldVert = pos + ( rotMat * local );
            if ( worldVert.y < lowestY )
            {
                lowestY = worldVert.y;
            }
        }
        bottomOffset = pos.y - lowestY;
    }
    else
    {
        bottomOffset = m_ballPhysics.radius;
    }

    // get the height of the terrain at the current XZ position
    float terrainH = m_terrain->GetTerrainHeightAt( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z );

    // if we are lower than the terrain, push up
    if ( m_physicsInfo.GetPosition().y - bottomOffset < terrainH )
    {
        Vector3 updatePos( m_physicsInfo.GetPosition().x,
                           terrainH + bottomOffset,
                           m_physicsInfo.GetPosition().z );
        m_physicsInfo.SetPosition( updatePos );
    }
}


float GameModel::GetSubmergedVolumePercent()
{
    // For current sphere-only scenes, submerged volume is evaluated from cached
    // immutable sphere terms (radius + inverse volume) and the current center height.
    float fluidHeightRelativeToCenter = m_worldEnvironment->GetFluidSurfaceHeight() - m_physicsInfo.GetPosition().y;
    float radius = m_ballPhysics.radius;

    if ( fluidHeightRelativeToCenter <= -radius )
    {
        return 0.0f;
    }

    if ( fluidHeightRelativeToCenter >= radius )
    {
        return 1.0f;
    }

    float yValue = fluidHeightRelativeToCenter + radius;
    return ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) * m_ballPhysics.invVolume;
}


const Quaternion& GameModel::GetOrientation() const
{
    return m_physicsInfo.GetOrientation();
}


const Vector3& GameModel::GetRotationalInertia()
{
    return m_ballPhysics.rotationalInertia;
}


const Vector3& GameModel::GetInvertedRotationalInertia()
{
    return m_ballPhysics.invRotationalInertia;
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
