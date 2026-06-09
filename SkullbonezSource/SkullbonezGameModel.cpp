// --- Includes ---
#include "SkullbonezGameModel.h"
#include "SkullbonezCollisionShape.h"
#include "SkullbonezGeometricStructures.h"
#include "SkullbonezGeometricMath.h"
#include "SkullbonezImpulseSolver.h"
#include "SkullbonezObjectContactManifold.h"
#include "SkullbonezProfiler.h"


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
    m_fixedContactHighlightSeconds = 0.0f;
    m_renderTintR = 1.0f;
    m_renderTintG = 1.0f;
    m_renderTintB = 1.0f;
    m_renderColorOverride = 0.0f;
    m_isResponseRequired = false;
    m_isFixed = false;
    m_name[0] = '\0';
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


void GameModel::SetFixed( bool isFixed )
{
    m_isFixed = isFixed;
    if ( m_isFixed )
    {
        m_physicsInfo.SetLinearVelocity( Vector::ZERO_VECTOR );
        m_physicsInfo.SetAngularVelocity( Vector::ZERO_VECTOR );
        m_physicsInfo.SetWorldForce( Vector::ZERO_VECTOR, Vector::ZERO_VECTOR );
        m_physicsInfo.ZeroForce();
        m_isResponseRequired = false;
    }
}


bool GameModel::IsFixed() const
{
    return m_isFixed;
}


void GameModel::NotifyFixedContact( float highlightSeconds )
{
    if ( m_isFixed && highlightSeconds > m_fixedContactHighlightSeconds )
    {
        m_fixedContactHighlightSeconds = highlightSeconds;
    }
}


void GameModel::TickFixedContactHighlight( float dt )
{
    if ( m_fixedContactHighlightSeconds <= 0.0f || dt <= 0.0f )
    {
        return;
    }

    m_fixedContactHighlightSeconds -= dt;
    if ( m_fixedContactHighlightSeconds < 0.0f )
    {
        m_fixedContactHighlightSeconds = 0.0f;
    }
}


float GameModel::GetFixedContactHighlightAlpha() const
{
    static constexpr float FADE_SECONDS = 0.5f;
    float alpha = m_fixedContactHighlightSeconds / FADE_SECONDS;
    if ( alpha < 0.0f )
    {
        return 0.0f;
    }
    if ( alpha > 1.0f )
    {
        return 1.0f;
    }
    return alpha;
}


void GameModel::SetImpulseForce( const Vector3& vForce,
                                 const Vector3& vApplicationPoint )
{
    if ( m_isFixed )
    {
        return;
    }
    m_physicsInfo.SetImpulseForce( vForce, vApplicationPoint );
}


void GameModel::SetWorldForce( const Vector3& vWorldForce, const Vector3& vWorldTorque )
{
    if ( m_isFixed )
    {
        m_physicsInfo.SetWorldForce( Vector::ZERO_VECTOR, Vector::ZERO_VECTOR );
        return;
    }
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


void GameModel::SetRenderTint( float tintR, float tintG, float tintB, float colorOverride )
{
    m_renderTintR = tintR;
    m_renderTintG = tintG;
    m_renderTintB = tintB;
    m_renderColorOverride = colorOverride;
}


void GameModel::GetRenderTint( float& tintR, float& tintG, float& tintB, float& colorOverride ) const
{
    tintR = m_renderTintR;
    tintG = m_renderTintG;
    tintB = m_renderTintB;
    colorOverride = m_renderColorOverride;
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
    // Swept tests use the path each body would travel during this substep if
    // unobstructed.  The solver only needs the simple kinematic ray: current
    // position plus velocity scaled by the candidate timestep.
    Ray targetRay( collisionTarget.m_physicsInfo.GetPosition(), collisionTarget.m_physicsInfo.GetVelocity() * changeInTime );
    Ray focusRay( m_physicsInfo.GetPosition(), m_physicsInfo.GetVelocity() * changeInTime );

    // Dispatch collision test via the variant visitor (handles sphere-sphere, sphere-box, box-box)
    return TestShapeCollision( m_boundingVolume, collisionTarget.m_boundingVolume, focusRay, targetRay );
}


void GameModel::CollisionResponseGameModel( GameModel& responseTarget )
{
    // Deprecated object/object impulse bridge. RunSolverPhysics now clears
    // swept object flags after candidate advancement and lets the persistent
    // Catto rows own velocity response. This function remains only until any
    // external/debug callers are audited.
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
    // ENGINE-SPECIFIC / NOVEL:
    //   This is not Catto's velocity-level PGS solve. It is a conservative
    //   positional cleanup used when the swept collision pass did not schedule a
    //   one-shot response but the authoritative Skullbonez shape-pair manifold
    //   says two bodies are already overlapping.
    //
    // CATTO CONNECTION:
    //   The manifold data is still in Catto's contact-row shape: normal plus
    //   point penetration. We only use the deepest penetration here because this
    //   routine moves positions directly instead of solving per-row impulses.
    Physics::ObjectContactManifold manifold;
    if ( !Physics::BuildObjectContactManifold( *this, overlapTarget, 0, 1, 0.0f, manifold ) )
    {
        return;
    }

    // Objects are overlapping: positional correction only.
    float maxPenetration = 0.0f;
    for ( uint8_t i = 0; i < manifold.pointCount; ++i )
    {
        maxPenetration = (std::max)( maxPenetration, manifold.points[i].penetration );
    }

    if ( maxPenetration <= 0.0f )
    {
        return;
    }

    float invMassA = GetInvertedMass();
    float invMassB = overlapTarget.GetInvertedMass();
    float totalInvMass = invMassA + invMassB;
    if ( totalInvMass <= TOLERANCE )
    {
        return;
    }

    // Objects are overlapping: push along the real narrowphase normal, not the
    // broadphase bounding-radius axis.
    Vector3 correction = manifold.normal * ( maxPenetration / totalInvMass );
    m_physicsInfo.SetPosition( m_physicsInfo.GetPosition() - correction * invMassA );
    overlapTarget.m_physicsInfo.SetPosition( overlapTarget.m_physicsInfo.GetPosition() + correction * invMassB );
}


bool GameModel::CollisionResponseTerrain( float remainingTimeStep )
{
    if ( m_isFixed )
    {
        m_isResponseRequired = false;
        return false;
    }

    // if there has been no collision, throw an exception!
    if ( !m_isResponseRequired )
    {
        throw std::runtime_error( "Cannot perform collision response when no collision has occured!  (GameModel::CollisionResponseTerrain)" );
    }

    // respond to the collision...
    bool contactSupportsSleep = ImpulseSolver::RespondCollisionTerrain( *this, remainingTimeStep );

    // update the m_position based on remaining time step
    UpdatePosition( remainingTimeStep );

    // set the collided collision object to null now the reaction has taken place
    m_isResponseRequired = false;

    return contactSupportsSleep;
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
    if ( m_isFixed )
    {
        m_physicsInfo.SetLinearVelocity( Vector::ZERO_VECTOR );
        m_physicsInfo.SetAngularVelocity( Vector::ZERO_VECTOR );
        return;
    }

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
    if ( m_isFixed )
    {
        return;
    }

    // Skip entirely when no time has passed (e.g., zero-time terrain collision cap).
    // Position hasn't changed, so no need to clamp against terrain.
    if ( changeInTime <= 0.0f )
    {
        return;
    }

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
    if ( m_isFixed )
    {
        return 0.0f;
    }
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


bool GameModel::GetClosestBoxTerrainVertex( Vector3& outVertex, float& outTerrainHeight, Plane& outPlane, float& outGap )
{
    // Profile just the eight-vertex terrain sampling loop. This is called from
    // collision detection and debug clamping, so keeping the marker narrow makes
    // it easy to see the cost of the box-specific fix separately from the rest of
    // the terrain response.
    PROFILE_SCOPED( "Frame/Physics/Terrain/BoxClosestVertexProbe" );

    if ( !m_terrain || !std::holds_alternative<BoundingBox>( m_boundingVolume ) )
    {
        return false;
    }

    const BoundingBox& box = std::get<BoundingBox>( m_boundingVolume );
    const Vector3& he = box.GetHalfExtents();
    Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
    const Vector3& position = m_physicsInfo.GetPosition();

    bool found = false;
    float bestGap = 1.0e30f;
    for ( int v = 0; v < 8; ++v )
    {
        // The low three bits enumerate the OBB corner signs. Sampling each
        // world-space corner against its own terrain height keeps sleep/contact
        // decisions tied to the visible geometry instead of a center XZ sample.
        Vector3 local(
            ( v & 1 ) ? he.x : -he.x,
            ( v & 2 ) ? he.y : -he.y,
            ( v & 4 ) ? he.z : -he.z );
        Vector3 worldVertex = position + ( rotMat * local );

        if ( !m_terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        m_terrain->GetTerrainHeightAndPlaneAt( worldVertex.x, worldVertex.z, terrainHeight, terrainPlane );
        float gap = worldVertex.y - terrainHeight;
        if ( !found || gap < bestGap )
        {
            found = true;
            bestGap = gap;
            outVertex = worldVertex;
            outTerrainHeight = terrainHeight;
            outPlane = terrainPlane;
            outGap = gap;
        }
    }

    return found;
}


float GameModel::GetTerrainCollisionTime( float changeInTime )
{
    // Swept terrain tests use the model's unobstructed path for the candidate
    // timestep. Keeping this local makes the ray construction explicit at the
    // point where terrain collision state is prepared.
    m_responseInformation.testingRay = Ray( m_physicsInfo.GetPosition(), m_physicsInfo.GetVelocity() * changeInTime );

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
        // Closed-form lowest-vertex Y offset. For an OBB, the maximum downward extent
        // from centre is dot(abs(rotationRow_Y), halfExtents). This replaces the naive
        // 8-vertex loop with 3 abs + 3 multiply-adds (>10× faster for boxes).
        const BoundingBox& box = std::get<BoundingBox>( m_boundingVolume );
        const Vector3& he = box.GetHalfExtents();
        Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
        bottomOffset = rotMat.SupportExtentY( he );
    }
    else
    {
        bottomOffset = m_ballPhysics.radius;
    }

    // Airborne early-out: if the object's lowest point cannot reach the terrain's
    // maximum height during this timestep (even while falling), skip the expensive
    // cached terrain query entirely.
    {
        float minBottomY = m_physicsInfo.GetPosition().y - bottomOffset;
        float velY = m_physicsInfo.GetVelocity().y;
        if ( velY < 0.0f )
        {
            minBottomY += velY * changeInTime;
        }
        if ( minBottomY > m_terrain->GetMaxHeight() )
        {
            return NO_COLLISION;
        }
    }

    if ( isBox )
    {
        // Boxes need a real vertex/terrain gap test before the old center-based
        // sphere path runs. The at_rest regression exposed that using the model
        // center height plus SupportExtentY can produce a false current contact
        // on sloped terrain, leaving the box sleeping above the ground.
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( !GetClosestBoxTerrainVertex( closestVertex, terrainHeight, terrainPlane, gap ) )
        {
            return NO_COLLISION;
        }

        if ( gap <= Cfg().contactEpsilon )
        {
            // Reuse the terrain plane from the actual closest vertex so detection
            // and response agree about which patch of terrain is carrying the box.
            m_responseInformation.testingPlane = terrainPlane;
            m_responseInformation.collisionTime = 0.0f;
            return 0.0f;
        }

        if ( m_responseInformation.testingRay.vector3.IsCloseToZero() )
        {
            return NO_COLLISION;
        }

        const BoundingBox& box = std::get<BoundingBox>( m_boundingVolume );
        const Vector3& he = box.GetHalfExtents();
        Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
        const Vector3& position = m_physicsInfo.GetPosition();

        float earliestCollisionTime = NO_COLLISION;
        Plane earliestPlane;
        {
            // When no vertex is currently touching, sweep every box vertex along
            // the body's linear motion and take the earliest plane hit. This keeps
            // the existing linear CCD behavior but avoids inventing terrain contact
            // from a center sample that may be nowhere near the lowest corner.
            PROFILE_SCOPED( "Frame/Physics/Terrain/BoxSweptVertexProbe" );
            for ( int v = 0; v < 8; ++v )
            {
                Vector3 local(
                    ( v & 1 ) ? he.x : -he.x,
                    ( v & 2 ) ? he.y : -he.y,
                    ( v & 4 ) ? he.z : -he.z );
                Vector3 worldVertex = position + ( rotMat * local );

                if ( !m_terrain->IsInBounds( worldVertex.x, worldVertex.z ) )
                {
                    continue;
                }

                float vertexTerrainHeight = 0.0f;
                Plane vertexPlane;
                m_terrain->GetTerrainHeightAndPlaneAt( worldVertex.x,
                                                       worldVertex.z,
                                                       vertexTerrainHeight,
                                                       vertexPlane );

                Ray vertexRay( worldVertex, m_physicsInfo.GetVelocity() * changeInTime );
                float vertexCollisionTime = GeometricMath::CalculateIntersectionTime( vertexPlane, vertexRay );
                if ( vertexCollisionTime >= ZERO_TAKE_TOLERANCE &&
                     vertexCollisionTime <= 1.0f &&
                     vertexCollisionTime < earliestCollisionTime )
                {
                    earliestCollisionTime = vertexCollisionTime;
                    earliestPlane = vertexPlane;
                }
            }
        }

        if ( earliestCollisionTime <= 1.0f )
        {
            m_responseInformation.testingPlane = earliestPlane;
            m_responseInformation.collisionTime = earliestCollisionTime;
            return earliestCollisionTime;
        }

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


GameModel::ObjectSweepResult GameModel::SweepGameModel( GameModel& collisionTarget,
                                                        float changeInTime )
{
    ObjectSweepResult result;
    result.collisionTime = changeInTime;

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
        result.hit = true;
        result.collisionTime = collisionTime * changeInTime;
    }

    // return when the collision will occur
    return result;
}


const Vector3& GameModel::GetPosition()
{
    return m_physicsInfo.GetPosition();
}


const Vector3& GameModel::GetPosition() const
{
    // ENGINE-SPECIFIC:
    //   Const access lets the narrowphase manifold builder inspect immutable
    //   GameModels without opening write access to physics state.
    return m_physicsInfo.GetPosition();
}


void GameModel::DEBUG_SetSphereToTerrain()
{
    // if we are not in bounds then exit now!
    if ( !m_terrain->IsInBounds( m_physicsInfo.GetPosition().x, m_physicsInfo.GetPosition().z ) )
    {
        return;
    }

    if ( std::holds_alternative<BoundingBox>( m_boundingVolume ) )
    {
        // This debug clamp is intentionally conservative for boxes: only lift the
        // model by the deepest actual vertex penetration. The previous center
        // height plus support extent logic could push boxes upward on uneven
        // terrain and preserve a visible floating gap after sleep.
        Vector3 closestVertex;
        float terrainHeight = 0.0f;
        Plane terrainPlane;
        float gap = 0.0f;
        if ( GetClosestBoxTerrainVertex( closestVertex, terrainHeight, terrainPlane, gap ) && gap < 0.0f )
        {
            Vector3 updatePos( m_physicsInfo.GetPosition().x,
                               m_physicsInfo.GetPosition().y - gap,
                               m_physicsInfo.GetPosition().z );
            m_physicsInfo.SetPosition( updatePos );
        }
        return;
    }

    float bottomOffset = m_ballPhysics.radius;

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
    if ( m_isFixed )
    {
        return Vector::ZERO_VECTOR;
    }
    return m_ballPhysics.invRotationalInertia;
}


const CollisionShape& GameModel::GetCollisionShape() const
{
    // ENGINE-SPECIFIC:
    //   The manifold builder dispatches on the variant shape type. Returning the
    //   CollisionShape by const reference keeps that dispatch explicit and avoids
    //   reintroducing broadphase-radius guesses into narrowphase code.
    return m_boundingVolume;
}


float GameModel::GetCoefficientRestitution()
{
    return m_physicsInfo.GetCoefficientRestitution();
}


void GameModel::SetLinearVelocity( const Vector3& v )
{
    if ( m_isFixed )
    {
        m_physicsInfo.SetLinearVelocity( Vector::ZERO_VECTOR );
        return;
    }
    m_physicsInfo.SetLinearVelocity( v );
}


void GameModel::SetAngularVelocity( const Vector3& v )
{
    if ( m_isFixed )
    {
        m_physicsInfo.SetAngularVelocity( Vector::ZERO_VECTOR );
        return;
    }
    m_physicsInfo.SetAngularVelocity( v );
}


void GameModel::SetPosition( const Vector3& pos )
{
    m_physicsInfo.SetPosition( pos );
}


void GameModel::SetOrientation( const Quaternion& q )
{
    m_physicsInfo.SetOrientation( q );
}
