// --- Includes ---
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezHelper.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezImpulseSolver.h"
#include "SkullbonezCollisionResponse.h"
#include <cmath>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Basics;
namespace Vector = SkullbonezCore::Math::Vector;


// Per-instance data layout: mat4 (16 floats) + alpha (1 float)
static constexpr int SHADOW_INSTANCE_FLOATS = 17;


GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( MAX_GAME_MODELS );
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    m_groundedThisFrame.reserve( MAX_GAME_MODELS );
    m_persistentContacts.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCache.reserve( MAX_GAME_MODELS * 4 );
    m_persistentContactCounts.reserve( MAX_GAME_MODELS );
    m_shadowInstanceData.reserve( MAX_GAME_MODELS * SHADOW_INSTANCE_FLOATS );
};

void GameModelCollection::AddGameModel( GameModel gameModel )
{
    assert( static_cast<int>( m_gameModels.size() ) < MAX_GAME_MODELS && "Exceeded MAX_GAME_MODELS" );
    m_gameModels.push_back( std::move( gameModel ) );
}


void GameModelCollection::SetLegacyMode( bool legacy )
{
    m_useLegacyPhysics = legacy;
}

bool GameModelCollection::GetLegacyMode() const
{
    return m_useLegacyPhysics;
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_timeRemaining.clear();
    m_groundedThisFrame.clear();
    m_sleepState.clear();
    m_sleepCounter.clear();
    m_persistentContacts.clear();
    m_persistentContactCache.clear();
    m_persistentContactCounts.clear();
}


void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    if ( m_gameModels.empty() )
    {
        return;
    }

    // Render spheres
    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes );
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        if ( !m_gameModels[x].IsBox() )
        {
            Matrix4 model = m_gameModels[x].GetModelMatrix();
            SkullbonezHelper::DrawSphereBatchModel( model );
        }
    }
    SkullbonezHelper::DrawSphereBatchEnd();

    // Render boxes (hidden in legacy mode - boxes don't exist in the legacy sphere-only solver)
    if ( !m_useLegacyPhysics )
    {
        SkullbonezHelper::DrawBoxBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes );
        for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
        {
            if ( m_gameModels[x].IsBox() )
            {
                Matrix4 model = m_gameModels[x].GetModelMatrix();
                SkullbonezHelper::DrawBoxBatchModel( model );
            }
        }
        SkullbonezHelper::DrawBoxBatchEnd();
    }
}


void GameModelCollection::RenderShadows( Geometry::Terrain* m_terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj,
                                         float waterSurfaceY )
{
    if ( !m_terrain )
    {
        return;
    }

    if ( !m_shadowInstMesh )
    {
        BuildShadowMesh();
    }

    // Build per-instance data: model matrix (16 floats) + alpha (1 float).
    // Pre-size once and write by index so we avoid repeated end-insert growth work.
    int modelCount = static_cast<int>( m_gameModels.size() );
    m_shadowInstanceData.resize( modelCount * SHADOW_INSTANCE_FLOATS );
    int writeOffset = 0;
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        // Skip boxes in legacy mode — they are hidden, so no shadow either
        if ( m_useLegacyPhysics && m_gameModels[i].IsBox() )
        {
            continue;
        }

        Vector3 pos = m_gameModels[i].GetPosition();
        float radius = m_gameModels[i].GetBoundingRadius();

        if ( !m_terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundY;
        Vector3 N;
        m_terrain->GetTerrainHeightAndNormalAt( pos.x, pos.z, groundY, N );

        // Fast-out: ball centre is over fully submerged terrain — no visible shadow.
        if ( groundY <= waterSurfaceY )
        {
            continue;
        }

        float height = pos.y - groundY - radius;
        if ( height < 0.0f )
        {
            height = 0.0f;
        }
        if ( height >= Cfg().shadowMaxHeight )
        {
            continue;
        }

        float alpha = Cfg().shadowMaxAlpha * ( 1.0f - height / Cfg().shadowMaxHeight );
        float shadowRadius = radius * Cfg().shadowScale;

        // Fused T(pos)*RotFromUpToN*Scale(shadowRadius) — no acosf/cosf/sinf, no Matrix4 products
        Matrix4 model = Matrix4::ShadowFromNormal( pos.x, groundY + Cfg().shadowOffset, pos.z, N, shadowRadius );

        // Write mat4 (16 floats) + alpha (1 float) at the current packed slot.
        const float* md = model.Data();
        memcpy( &m_shadowInstanceData[writeOffset], md, sizeof( float ) * 16 );
        m_shadowInstanceData[writeOffset + 16] = alpha;
        writeOffset += SHADOW_INSTANCE_FLOATS;
    }

    if ( writeOffset == 0 )
    {
        m_shadowInstanceData.clear();
        return;
    }

    m_shadowInstanceData.resize( writeOffset );
    int instanceCount = writeOffset / SHADOW_INSTANCE_FLOATS;
    if ( instanceCount == 0 )
    {
        return;
    }

    // Upload instance data
    Gfx().UploadInstanceData( m_shadowInstMesh, m_shadowInstanceData.data(), static_cast<int>( m_shadowInstanceData.size() ) );

    // Render all shadows in one instanced draw call
    Gfx().SetBlend( true );
    Gfx().SetBlendFunc( SkullbonezCore::Rendering::BlendFactor::SrcAlpha, SkullbonezCore::Rendering::BlendFactor::OneMinusSrcAlpha );
    Gfx().SetPolygonOffset( true, -1.0f, -1.0f );
    Gfx().SetCullFace( false );

    m_shadowShader->Use();
    m_shadowShader->SetMat4( "uView", view );
    m_shadowShader->SetMat4( "uProjection", proj );

    // Disable depth writes for shadow rendering. Shadow discs sit at groundY + offset which can
    // be above the water plane near shorelines. If they wrote to the depth buffer, the water
    // (drawn after) would fail the depth test at those pixels and disappear. With depth writes off,
    // terrain depth values are preserved and water renders correctly over the shoreline.
    // Depth testing remains ON so shadows still respect terrain occlusion.
    Gfx().SetDepthWrite( false );
    Gfx().DrawInstancedMesh( m_shadowInstMesh, m_shadowDiscVertexCount, instanceCount );
    Gfx().SetDepthWrite( true );

    Gfx().SetPolygonOffset( false );
    Gfx().SetCullFace( true );
    Gfx().SetBlend( false );
}


Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return m_gameModels[index].GetPosition();
}


int GameModelCollection::GetModelCount() const
{
    return static_cast<int>( m_gameModels.size() );
}


GameModel& GameModelCollection::GetModelAtIndex( int index )
{
    return m_gameModels[index];
}


void GameModelCollection::RunPhysics( float fChangeInTime )
{
    const int modelCount = static_cast<int>( m_gameModels.size() );
    m_timeRemaining.assign( modelCount, fChangeInTime );
    m_groundedThisFrame.assign( modelCount, 0 );

    // Ensure sleep state vectors are sized (persists across frames)
    if ( static_cast<int>( m_sleepState.size() ) != modelCount )
    {
        m_sleepState.assign( modelCount, 0 );
        m_sleepCounter.assign( modelCount, 0 );
    }

    // Dispatch to the appropriate physics implementation — the mode is checked exactly once here.
    if ( m_useLegacyPhysics )
    {
        RunLegacyPhysics( fChangeInTime );
    }
    else
    {
        RunSolverPhysics( fChangeInTime );
    }

    // Per-frame physics state log. Active only when a path is set via scene directive or
    // --physics-log CLI arg. Log().Writef is a no-op in non-Debug builds so there is no
    // file I/O overhead in Release/Profile even if a path is somehow set.
#ifdef _DEBUG
    if ( m_physicsLogPath[0] != '\0' )
    {
        if ( m_physicsLogFrame == 0 )
        {
            Log().Writef( m_physicsLogPath, "frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,grounded\n" );
        }
        for ( int i = 0; i < modelCount; ++i )
        {
            const char* name = m_gameModels[i].GetName();
            const Vector3& pos = m_gameModels[i].GetPosition();
            const Vector3& vel = m_gameModels[i].GetVelocity();
            const Vector3& omega = m_gameModels[i].GetAngularVelocity();
            float speed = sqrtf( vel.x * vel.x + vel.y * vel.y + vel.z * vel.z );
            float omegaMag = sqrtf( omega.x * omega.x + omega.y * omega.y + omega.z * omega.z );
            int grounded = m_groundedThisFrame[i];
            Log().Writef( m_physicsLogPath, "%d,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d\n", m_physicsLogFrame, i, name, pos.x, pos.y, pos.z, vel.x, vel.y, vel.z, speed, omega.x, omega.y, omega.z, omegaMag, grounded );
        }
        ++m_physicsLogFrame;
    }
#endif
}


void GameModelCollection::WakeModel( int index )
{
    // Size the sleep vectors if RunPhysics hasn't been called yet.
    if ( static_cast<int>( m_sleepState.size() ) != static_cast<int>( m_gameModels.size() ) )
    {
        m_sleepState.assign( m_gameModels.size(), 0 );
        m_sleepCounter.assign( m_gameModels.size(), 0 );
    }
    if ( index >= 0 && index < static_cast<int>( m_sleepState.size() ) )
    {
        m_sleepState[index] = 0;
        m_sleepCounter[index] = 0;
    }
}


#ifdef _DEBUG
void GameModelCollection::SetPhysicsLogPath( const char* path )
{
    strcpy_s( m_physicsLogPath, sizeof( m_physicsLogPath ), path );
    m_physicsLogFrame = 0;
}
#endif


// Physics tick: original sphere-only ad-hoc solver.
// Boxes are unconditionally skipped — they are frozen in legacy mode.
// Calls CollisionResponse::* directly; no per-iteration mode check.
void GameModelCollection::RunLegacyPhysics( float dt )
{
    const int modelCount = static_cast<int>( m_gameModels.size() );

    // Apply forces to all spheres (boxes ignored in legacy mode)
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_gameModels[x].IsBox() )
        {
            continue;
        }
        m_gameModels[x].ApplyForces( dt );
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // Broadphase: build spatial grid from sphere positions only
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    m_collisionCellKeys.clear();
    for ( int i = 0; i < modelCount; ++i )
    {
        if ( m_gameModels[i].IsBox() )
        {
            continue;
        }
        m_spatialGrid.Insert( i, m_gameModels[i].GetPosition(), m_gameModels[i].GetBoundingRadius() );
    }
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    PROFILE_END( "Frame/Physics/Broadphase" );

    // Narrowphase: legacy sphere-sphere collision response
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_spatialGrid.GetCellSize();
    for ( const auto& cp : candidatePairs )
    {
        int x = cp.first;
        int y = cp.second;

        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            continue;
        }

        float availableTime = (std::min)( m_timeRemaining[x], m_timeRemaining[y] );
        float colTime = m_gameModels[x].CollisionDetectGameModel( m_gameModels[y], availableTime );

        if ( m_gameModels[x].IsResponseRequired() && m_gameModels[y].IsResponseRequired() )
        {
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );
            m_timeRemaining[x] -= colTime;
            m_timeRemaining[y] -= colTime;

            // Legacy sphere-sphere response — called directly; no mode flag queried
            CollisionResponse::RespondCollisionGameModels( m_gameModels[x], m_gameModels[y] );
            m_gameModels[x].ClearResponseRequired();
            m_gameModels[y].ClearResponseRequired();

            // Record collision cell for broadphase visualizer
            Vector3 midpoint = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            int16_t cx = (int16_t)floorf( midpoint.x * invCellSize );
            int16_t cy = (int16_t)floorf( midpoint.y * invCellSize );
            int16_t cz = (int16_t)floorf( midpoint.z * invCellSize );
            int64_t key = ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
            m_collisionCellKeys.push_back( key );
        }
        else
        {
            m_gameModels[x].StaticOverlapResponseGameModel( m_gameModels[y] );
        }
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Terrain: legacy sphere-terrain response (boxes skipped)
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_gameModels[x].IsBox() )
        {
            continue;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            float colTime = m_gameModels[x].CollisionDetectTerrain( m_timeRemaining[x] );

            if ( m_gameModels[x].IsResponseRequired() )
            {
                m_gameModels[x].UpdatePosition( colTime );

                // Legacy terrain response — called directly; no mode flag queried
                CollisionResponse::RespondCollisionTerrain( m_gameModels[x], m_timeRemaining[x] - colTime );
                m_gameModels[x].UpdatePosition( m_timeRemaining[x] - colTime );
                m_gameModels[x].ClearResponseRequired();

                m_groundedThisFrame[x] = 1;
                m_timeRemaining[x] = 0.0f;
            }
        }
    }
    PROFILE_END( "Frame/Physics/Terrain" );

    // Integrate remaining time for spheres only
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_gameModels[x].IsBox() )
        {
            continue;
        }
        if ( m_timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


void GameModelCollection::SolvePersistentObjectContacts( float dt )
{
    PROFILE_SCOPED( "Frame/Physics/Narrowphase/PersistentContacts" );

    // This pass handles the quiet case that old one-shot impulses were bad at:
    // balls already touching each other, especially a ball resting on another ball.
    // Instead of waiting for a fresh "impact", we build contact rules for pairs
    // that are touching or nearly touching, then solve those rules like tiny springs
    // with hard limits: push apart along the normal, resist sliding along tangents.
    const int modelCount = static_cast<int>( m_gameModels.size() );
    if ( modelCount <= 1 || m_candidatePairs.empty() )
    {
        m_persistentContacts.clear();
        m_persistentContactCache.clear();
        return;
    }

    m_persistentContacts.clear();
    m_persistentContactCounts.assign( modelCount, 0 );

    // Small allowed overlap. Without this tolerance, floating-point noise makes
    // the solver chase microscopic errors and resting balls visibly tremble.
    constexpr float contactSlop = 0.005f;

    // Baumgarte bias is a gentle "please separate" velocity for bodies that are
    // already interpenetrating. It removes overlap over several ticks instead of
    // teleporting everything apart in one harsh correction.
    constexpr float baumgarteBeta = 0.2f;

    // A final direct positional nudge catches the remaining overlap after the
    // velocity solve. The percent is deliberately partial so stacks do not pop.
    constexpr float positionCorrectionPercent = 0.35f;

    // Projected Gauss-Seidel works by revisiting every contact repeatedly. Each
    // visit improves the answer a little; twelve passes is a compromise between
    // stack stability and keeping the physics hot path affordable.
    constexpr int solverIterations = 12;
    const float invDt = ( dt > TOLERANCE ) ? ( 1.0f / dt ) : 120.0f;

    // Catto's cache needs a stable name for "body A touching body B". The pair
    // key is order-independent, so A/B and B/A find the same remembered impulse.
    auto makeKey = []( int a, int b ) -> int64_t
    {
        int lo = ( a < b ) ? a : b;
        int hi = ( a < b ) ? b : a;
        return ( static_cast<int64_t>( lo ) << 32 ) | static_cast<unsigned int>( hi );
    };

    // Inertia is rotational mass. Applying an off-center push changes spin as
    // well as linear velocity; inverse inertia converts contact torque into the
    // amount of angular velocity change it should cause.
    auto applyInvInertia = [&]( int body, const Vector3& v ) -> Vector3
    {
        return Vector::VectorMultiply( m_gameModels[body].GetInvertedRotationalInertia(), v );
    };

    // Apply one impulse to both bodies using Newton's third law: equal and
    // opposite pushes. A receives -impulse, B receives +impulse. The cross
    // products turn off-center pushes into spin changes.
    auto applyImpulse = [&]( const PersistentContact& c, const Vector3& impulse )
    {
        GameModel& a = m_gameModels[c.bodyA];
        GameModel& b = m_gameModels[c.bodyB];

        Vector3 velA = a.GetVelocity();
        Vector3 velB = b.GetVelocity();
        Vector3 omegaA = a.GetAngularVelocity();
        Vector3 omegaB = b.GetAngularVelocity();

        velA -= impulse * a.GetInvertedMass();
        velB += impulse * b.GetInvertedMass();
        omegaA -= applyInvInertia( c.bodyA, Vector::CrossProduct( c.rA, impulse ) );
        omegaB += applyInvInertia( c.bodyB, Vector::CrossProduct( c.rB, impulse ) );

        a.SetLinearVelocity( velA );
        b.SetLinearVelocity( velB );
        a.SetAngularVelocity( omegaA );
        b.SetAngularVelocity( omegaB );
    };

    // First pass: turn broadphase candidate pairs into contact rows. The current
    // narrowphase only has bounding spheres for object-object contacts, so this
    // extrapolates the 2D paper to 3D by using the center-to-center direction as
    // the contact normal and two perpendicular tangent axes for friction.
    for ( const auto& cp : m_candidatePairs )
    {
        int aIndex = cp.first;
        int bIndex = cp.second;
        if ( aIndex == bIndex || m_sleepState[aIndex] || m_sleepState[bIndex] )
        {
            continue;
        }

        if ( bIndex < aIndex )
        {
            std::swap( aIndex, bIndex );
        }

        GameModel& a = m_gameModels[aIndex];
        GameModel& b = m_gameModels[bIndex];
        Vector3 posA = a.GetPosition();
        Vector3 posB = b.GetPosition();
        Vector3 delta = posB - posA;
        float distSq = delta * delta;
        float radiusA = a.GetBoundingRadius();
        float radiusB = b.GetBoundingRadius();
        float radiusSum = radiusA + radiusB;
        float contactDistance = radiusSum + Cfg().contactEpsilon;
        if ( distSq > contactDistance * contactDistance )
        {
            continue;
        }

        float dist = sqrtf( distSq );
        Vector3 normal( 0.0f, 1.0f, 0.0f );
        if ( dist > TOLERANCE )
        {
            normal = delta / dist;
        }

        float separation = dist - radiusSum;
        Vector3 pointA = posA + normal * radiusA;
        Vector3 pointB = posB - normal * radiusB;
        Vector3 contactPoint = ( pointA + pointB ) * 0.5f;

        // rA/rB are the small arms from each center to the contact point. They
        // are what let the same contact impulse both move and spin a ball.
        PersistentContact c;
        c.bodyA = aIndex;
        c.bodyB = bIndex;
        c.key = makeKey( aIndex, bIndex );
        c.normal = normal;
        c.rA = contactPoint - posA;
        c.rB = contactPoint - posB;
        c.penetration = ( separation < 0.0f ) ? -separation : 0.0f;
        m_persistentContacts.push_back( c );
        ++m_persistentContactCounts[aIndex];
        ++m_persistentContactCounts[bIndex];

        if ( fabsf( normal.y ) > 0.25f )
        {
            // A mostly vertical contact can support weight, so both bodies count
            // as grounded for sleep. This lets a ball sleeping on a stack stay
            // asleep instead of demanding terrain contact directly.
            m_groundedThisFrame[aIndex] = 1;
            m_groundedThisFrame[bIndex] = 1;
        }
    }

    if ( m_persistentContacts.empty() )
    {
        m_persistentContactCache.clear();
        return;
    }

    // Second pass: precompute each row. This is the "setup" part of the paper:
    // build friction axes, effective masses, bias, friction limits, and pull the
    // previous frame's accumulated impulses from the cache.
    for ( PersistentContact& c : m_persistentContacts )
    {
        GameModel& a = m_gameModels[c.bodyA];
        GameModel& b = m_gameModels[c.bodyB];

        // The normal is only one direction. In 3D, sliding can happen in any
        // sideways direction, so we create two perpendicular sideways axes.
        if ( fabsf( c.normal.x ) > 0.9f )
        {
            c.tangent1 = Vector3( 0.0f, 0.0f, 1.0f );
        }
        else
        {
            c.tangent1 = Vector3( 1.0f, 0.0f, 0.0f );
        }
        c.tangent1 -= c.normal * ( c.tangent1 * c.normal );
        float tangentMag = Vector::VectorMag( c.tangent1 );
        if ( tangentMag > TOLERANCE )
        {
            c.tangent1 /= tangentMag;
        }
        c.tangent2 = Vector::CrossProduct( c.normal, c.tangent1 );

        // Effective mass says how stubborn this contact is. A light ball pushed
        // through its center moves easily; a heavy or off-center body resists more
        // because some of the push also has to rotate it.
        Vector3 rAxN = Vector::CrossProduct( c.rA, c.normal );
        Vector3 rBxN = Vector::CrossProduct( c.rB, c.normal );
        float kNormal = a.GetInvertedMass() + b.GetInvertedMass() +
                        c.normal * Vector::CrossProduct( applyInvInertia( c.bodyA, rAxN ), c.rA ) +
                        c.normal * Vector::CrossProduct( applyInvInertia( c.bodyB, rBxN ), c.rB );
        c.normalMass = ( kNormal > TOLERANCE ) ? ( 1.0f / kNormal ) : 0.0f;

        Vector3 rAxT1 = Vector::CrossProduct( c.rA, c.tangent1 );
        Vector3 rBxT1 = Vector::CrossProduct( c.rB, c.tangent1 );
        float kT1 = a.GetInvertedMass() + b.GetInvertedMass() +
                    c.tangent1 * Vector::CrossProduct( applyInvInertia( c.bodyA, rAxT1 ), c.rA ) +
                    c.tangent1 * Vector::CrossProduct( applyInvInertia( c.bodyB, rBxT1 ), c.rB );
        c.tangentMass1 = ( kT1 > TOLERANCE ) ? ( 1.0f / kT1 ) : 0.0f;

        Vector3 rAxT2 = Vector::CrossProduct( c.rA, c.tangent2 );
        Vector3 rBxT2 = Vector::CrossProduct( c.rB, c.tangent2 );
        float kT2 = a.GetInvertedMass() + b.GetInvertedMass() +
                    c.tangent2 * Vector::CrossProduct( applyInvInertia( c.bodyA, rAxT2 ), c.rA ) +
                    c.tangent2 * Vector::CrossProduct( applyInvInertia( c.bodyB, rBxT2 ), c.rB );
        c.tangentMass2 = ( kT2 > TOLERANCE ) ? ( 1.0f / kT2 ) : 0.0f;

        Vector3 velA = a.GetVelocity() + Vector::CrossProduct( a.GetAngularVelocity(), c.rA );
        Vector3 velB = b.GetVelocity() + Vector::CrossProduct( b.GetAngularVelocity(), c.rB );
        float vn = ( velB - velA ) * c.normal;
        float restitution = sqrtf( a.GetCoefficientRestitution() * b.GetCoefficientRestitution() );

        // Bias has two jobs. On a real impact it is bounce. On a resting overlap
        // it is a small separating velocity that decays the overlap smoothly.
        c.bias = 0.0f;
        if ( vn < -Cfg().contactRestitutionThreshold )
        {
            c.bias = -restitution * vn;
        }
        else
        {
            float penetrationError = c.penetration - contactSlop;
            if ( penetrationError > 0.0f )
            {
                c.bias = baumgarteBeta * penetrationError * invDt;
            }
        }

        uint16_t countA = ( m_persistentContactCounts[c.bodyA] > 0 ) ? m_persistentContactCounts[c.bodyA] : 1;
        uint16_t countB = ( m_persistentContactCounts[c.bodyB] > 0 ) ? m_persistentContactCounts[c.bodyB] : 1;
        float contactMassA = a.GetMass() / static_cast<float>( countA );
        float contactMassB = b.GetMass() / static_cast<float>( countB );
        float contactMass = ( contactMassA < contactMassB ) ? contactMassA : contactMassB;
        c.frictionLimit = Cfg().frictionCoeff * contactMass * fabsf( Cfg().gravity ) * dt;

        // Warm starting: if this same pair was touching last frame, start from
        // the old solution instead of zero. This is the paper's key ingredient
        // for stacking, because support forces are almost unchanged frame to frame.
        for ( const PersistentContactCacheEntry& cached : m_persistentContactCache )
        {
            if ( cached.key == c.key )
            {
                c.accN = ( cached.accN > 0.0f ) ? cached.accN : 0.0f;
                c.accT1 = std::clamp( cached.accT1, -c.frictionLimit, c.frictionLimit );
                c.accT2 = std::clamp( cached.accT2, -c.frictionLimit, c.frictionLimit );
                break;
            }
        }

        if ( c.accN > 0.0f || fabsf( c.accT1 ) > 0.0f || fabsf( c.accT2 ) > 0.0f )
        {
            // Cached impulses are not just bookkeeping: they must be applied to
            // the bodies before iteration starts, otherwise the solver would clamp
            // against a pretend push that never actually happened.
            Vector3 warmImpulse = c.normal * c.accN + c.tangent1 * c.accT1 + c.tangent2 * c.accT2;
            applyImpulse( c, warmImpulse );
        }
    }

    // Third pass: Projected Gauss-Seidel. Each contact computes the extra impulse
    // needed to reduce its current violation, adds that to the accumulated total,
    // clamps the total to valid bounds, then applies only the difference.
    for ( int iter = 0; iter < solverIterations; ++iter )
    {
        float iterImpulseSq = 0.0f;
        for ( PersistentContact& c : m_persistentContacts )
        {
            GameModel& a = m_gameModels[c.bodyA];
            GameModel& b = m_gameModels[c.bodyB];

            Vector3 velA = a.GetVelocity() + Vector::CrossProduct( a.GetAngularVelocity(), c.rA );
            Vector3 velB = b.GetVelocity() + Vector::CrossProduct( b.GetAngularVelocity(), c.rB );
            float vn = ( velB - velA ) * c.normal;
            float lambdaN = c.normalMass * ( c.bias - vn );
            float oldAccN = c.accN;

            // Normal impulses are one-way. Contacts can push bodies apart, but
            // they cannot glue bodies together, so the accumulated value is >= 0.
            c.accN = ( oldAccN + lambdaN > 0.0f ) ? oldAccN + lambdaN : 0.0f;
            float deltaN = c.accN - oldAccN;
            applyImpulse( c, c.normal * deltaN );

            velA = a.GetVelocity() + Vector::CrossProduct( a.GetAngularVelocity(), c.rA );
            velB = b.GetVelocity() + Vector::CrossProduct( b.GetAngularVelocity(), c.rB );
            float vt1 = ( velB - velA ) * c.tangent1;
            float lambdaT1 = c.tangentMass1 * ( -vt1 );
            float oldAccT1 = c.accT1;

            // Friction can push either sideways direction, but only up to the
            // contact's friction budget. Past this clamp the bodies are sliding.
            c.accT1 = std::clamp( oldAccT1 + lambdaT1, -c.frictionLimit, c.frictionLimit );
            float deltaT1 = c.accT1 - oldAccT1;
            applyImpulse( c, c.tangent1 * deltaT1 );

            velA = a.GetVelocity() + Vector::CrossProduct( a.GetAngularVelocity(), c.rA );
            velB = b.GetVelocity() + Vector::CrossProduct( b.GetAngularVelocity(), c.rB );
            float vt2 = ( velB - velA ) * c.tangent2;
            float lambdaT2 = c.tangentMass2 * ( -vt2 );
            float oldAccT2 = c.accT2;
            c.accT2 = std::clamp( oldAccT2 + lambdaT2, -c.frictionLimit, c.frictionLimit );
            float deltaT2 = c.accT2 - oldAccT2;
            applyImpulse( c, c.tangent2 * deltaT2 );

            iterImpulseSq += deltaN * deltaN + deltaT1 * deltaT1 + deltaT2 * deltaT2;
        }

        if ( iterImpulseSq < 1.0e-6f )
        {
            break;
        }
    }

    // Fourth pass: remove any visible leftover overlap. The velocity solver does
    // most of the work, but this direct correction keeps persistent contacts from
    // sinking deeper into each other over many frames.
    for ( const PersistentContact& c : m_persistentContacts )
    {
        if ( c.penetration <= contactSlop )
        {
            continue;
        }

        GameModel& a = m_gameModels[c.bodyA];
        GameModel& b = m_gameModels[c.bodyB];
        float invMassA = a.GetInvertedMass();
        float invMassB = b.GetInvertedMass();
        float totalInvMass = invMassA + invMassB;
        if ( totalInvMass <= TOLERANCE )
        {
            continue;
        }

        Vector3 correction = c.normal * ( ( c.penetration - contactSlop ) * positionCorrectionPercent / totalInvMass );
        a.SetPosition( a.GetPosition() - correction * invMassA );
        b.SetPosition( b.GetPosition() + correction * invMassB );
    }

    // Final pass: store this frame's accumulated pushes for next frame. This is
    // why a settled stack can remain settled; it does not have to rediscover from
    // scratch how much support force each contact needs every tick.
    m_persistentContactCache.clear();
    for ( const PersistentContact& c : m_persistentContacts )
    {
        if ( c.accN <= 0.0f && fabsf( c.accT1 ) <= TOLERANCE && fabsf( c.accT2 ) <= TOLERANCE )
        {
            continue;
        }

        PersistentContactCacheEntry cached;
        cached.key = c.key;
        cached.accN = c.accN;
        cached.accT1 = c.accT1;
        cached.accT2 = c.accT2;
        m_persistentContactCache.push_back( cached );
    }
}


// Physics tick: unified impulse solver for all object types (spheres and boxes).
// No object filtering needed — all models participate.
void GameModelCollection::RunSolverPhysics( float dt )
{
    const int modelCount = static_cast<int>( m_gameModels.size() );

    // Sleep thresholds: object goes to sleep after SLEEP_FRAMES consecutive frames
    // with both linear speed² < SLEEP_LINEAR_SQ and angular speed² < SLEEP_ANGULAR_SQ.
    constexpr float SLEEP_LINEAR_SQ = 0.5f * 0.5f;  // 0.5 units/s
    constexpr float SLEEP_ANGULAR_SQ = 0.3f * 0.3f; // 0.3 rad/s
    constexpr uint8_t SLEEP_FRAMES = 30;            // ~0.5s at 60Hz fixed step

    // Apply forces to awake models only
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] )
        {
            m_timeRemaining[x] = 0.0f;
            continue;
        }
        m_gameModels[x].ApplyForces( dt );
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // Broadphase: build spatial grid from all object positions (include sleeping for wake detection)
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    m_collisionCellKeys.clear();
    for ( int i = 0; i < modelCount; ++i )
    {
        m_spatialGrid.Insert( i, m_gameModels[i].GetPosition(), m_gameModels[i].GetBoundingRadius() );
    }
    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    PROFILE_END( "Frame/Physics/Broadphase" );

    // Narrowphase: impulse solver collision response for all pairs
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    float invCellSize = 1.0f / m_spatialGrid.GetCellSize();
    for ( const auto& cp : candidatePairs )
    {
        int x = cp.first;
        int y = cp.second;

        // Wake sleeping objects if an awake object is nearby and overlapping
        if ( m_sleepState[x] || m_sleepState[y] )
        {
            // Only wake if one is awake and moving toward the sleeper
            if ( m_sleepState[x] && !m_sleepState[y] )
            {
                // The awake object (y) must still have time this frame — if it has
                // already exhausted its budget it has moved to its final position and
                // cannot validly collide with the sleeper.
                if ( m_timeRemaining[y] <= 0.0f )
                {
                    continue;
                }
                // Check actual overlap before waking
                m_gameModels[y].CollisionDetectGameModel( m_gameModels[x], dt );
                if ( m_gameModels[y].IsResponseRequired() && m_gameModels[x].IsResponseRequired() )
                {
                    m_sleepState[x] = 0;
                    m_sleepCounter[x] = 0;
                    m_timeRemaining[x] = dt;
                    m_gameModels[x].ApplyForces( dt );
                    // Response flags already set — go straight to response
                    m_gameModels[x].CollisionResponseGameModel( m_gameModels[y] );
                }
                continue;
            }
            else if ( m_sleepState[y] && !m_sleepState[x] )
            {
                // Guard: awake object (x) must have time remaining this frame.
                if ( m_timeRemaining[x] <= 0.0f )
                {
                    continue;
                }
                m_gameModels[x].CollisionDetectGameModel( m_gameModels[y], dt );
                if ( m_gameModels[x].IsResponseRequired() && m_gameModels[y].IsResponseRequired() )
                {
                    m_sleepState[y] = 0;
                    m_sleepCounter[y] = 0;
                    m_timeRemaining[y] = dt;
                    m_gameModels[y].ApplyForces( dt );
                    m_gameModels[x].CollisionResponseGameModel( m_gameModels[y] );
                }
                continue;
            }
            else
            {
                // Both sleeping — skip
                continue;
            }
        }

        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            continue;
        }

        float availableTime = (std::min)( m_timeRemaining[x], m_timeRemaining[y] );
        float colTime = m_gameModels[x].CollisionDetectGameModel( m_gameModels[y], availableTime );

        if ( m_gameModels[x].IsResponseRequired() && m_gameModels[y].IsResponseRequired() )
        {
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );
            m_timeRemaining[x] -= colTime;
            m_timeRemaining[y] -= colTime;

            // Impulse solver response (velocity-only; clears response flags on both models)
            m_gameModels[x].CollisionResponseGameModel( m_gameModels[y] );

            // Record collision cell for broadphase visualizer
            Vector3 midpoint = ( m_gameModels[x].GetPosition() + m_gameModels[y].GetPosition() ) * 0.5f;
            int16_t cx = (int16_t)floorf( midpoint.x * invCellSize );
            int16_t cy = (int16_t)floorf( midpoint.y * invCellSize );
            int16_t cz = (int16_t)floorf( midpoint.z * invCellSize );
            int64_t key = ( int64_t( cx ) * 73856093 ) ^ ( int64_t( cy ) * 19349663 ) ^ ( int64_t( cz ) * 83492791 );
            m_collisionCellKeys.push_back( key );
        }
        else
        {
            m_gameModels[x].StaticOverlapResponseGameModel( m_gameModels[y] );
        }
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // Terrain: impulse solver terrain response for awake models only
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] || m_timeRemaining[x] <= 0.0f )
        {
            continue;
        }

        float colTime = m_gameModels[x].CollisionDetectTerrain( m_timeRemaining[x] );

        if ( m_gameModels[x].IsResponseRequired() )
        {
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[x].CollisionResponseTerrain( m_timeRemaining[x] - colTime );
            m_groundedThisFrame[x] = 1;
            m_timeRemaining[x] = 0.0f;
        }
    }
    PROFILE_END( "Frame/Physics/Terrain" );

    SolvePersistentObjectContacts( dt );

    // Integrate remaining time for awake models
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    for ( int x = 0; x < modelCount; ++x )
    {
        if ( m_sleepState[x] )
        {
            continue;
        }

        if ( m_timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        }

        // Update sleep counter: check if object is below sleep thresholds
        const Vector3& vel = m_gameModels[x].GetVelocity();
        const Vector3& omega = m_gameModels[x].GetAngularVelocity();
        float speedSq = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        float omegaSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;

        if ( speedSq < SLEEP_LINEAR_SQ && omegaSq < SLEEP_ANGULAR_SQ && m_groundedThisFrame[x] )
        {
            if ( m_sleepCounter[x] < SLEEP_FRAMES )
            {
                ++m_sleepCounter[x];
            }
            if ( m_sleepCounter[x] >= SLEEP_FRAMES )
            {
                m_sleepState[x] = 1;
                // Zero velocities to prevent drift on wake
                m_gameModels[x].SetLinearVelocity( Math::Vector::ZERO_VECTOR );
                m_gameModels[x].SetAngularVelocity( Math::Vector::ZERO_VECTOR );
            }
        }
        else
        {
            m_sleepCounter[x] = 0;
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


void GameModelCollection::BuildShadowMesh()
{
    // Shadow disc rendered as a single quad (-1..+1 in XZ, Y=0).
    // The fragment shader discards pixels outside the unit circle (length(uv) > 1.0)
    // and applies a smooth per-pixel radial fade — no triangle fan needed.
    //
    // 2 triangles = 6 vertices (vs the old 16-segment fan = 48 vertices).
    // At 300 balls this saves 4,200 triangles per frame.
    //
    //  (-1,0,-1)-------(1,0,-1)
    //      |          /    |
    //      |        /      |
    //      |      /        |
    //  (-1,0, 1)-------(1,0, 1)
    //
    static const float verts[] =
        {
            // Triangle 1
            -1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
            -1.0f,
            -1.0f,
            0.0f,
            1.0f,
            // Triangle 2
            1.0f,
            0.0f,
            -1.0f,
            1.0f,
            0.0f,
            1.0f,
            -1.0f,
            0.0f,
            1.0f,
        };
    m_shadowDiscVertexCount = 6;

    // Instance layout: 5 attributes (4×vec4 for mat4 + 1×float for alpha), starting at location 3
    int instanceAttribSizes[] = { 4, 4, 4, 4, 1 };
    m_shadowInstMesh = Gfx().CreateInstancedMesh( verts, m_shadowDiscVertexCount, 3, MAX_GAME_MODELS, SHADOW_INSTANCE_FLOATS, 3, instanceAttribSizes, 5 );

    // Create shader
    m_shadowShader = Gfx().CreateShader( "shaders/shadow" );
}


void GameModelCollection::ResetGLResources()
{
    m_shadowShader.reset();
    if ( m_shadowInstMesh )
    {
        Gfx().DestroyInstancedMesh( m_shadowInstMesh );
        m_shadowInstMesh = 0;
    }
    m_shadowDiscVertexCount = 0;
}


bool GameModelCollection::SaveSceneSnapshot( const char* path, bool physicsOn, bool textOn, Environment::WorldEnvironment& worldEnv, const Vector3& camEye, const Vector3& camView, const Vector3& camUp )
{
    FILE* f = nullptr;
    if ( fopen_s( &f, path, "w" ) != 0 || !f )
    {
        return false;
    }

    fprintf( f, "# Snapshot — %d balls\n", static_cast<int>( m_gameModels.size() ) );
    fprintf( f, "physics %s\n", physicsOn ? "on" : "off" );
    fprintf( f, "text %s\n", textOn ? "on" : "off" );
    fprintf( f, "frames unlimited\n" );
    fprintf( f, "world %f %f %f\n", worldEnv.GetGravity(), worldEnv.GetFluidSurfaceHeight(), worldEnv.GetFluidDensity() );
    fprintf( f, "camera main  %.4f %.4f %.4f  %.4f %.4f %.4f  %.4f %.4f %.4f\n", camEye.x, camEye.y, camEye.z, camView.x, camView.y, camView.z, camUp.x, camUp.y, camUp.z );
    fprintf( f, "\n" );

    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        const char* name = m_gameModels[i].GetName();
        char safeName[64];
        if ( !name[0] )
        {
            sprintf_s( safeName, sizeof( safeName ), "_ball_%d", i );
            name = safeName;
        }

        const Vector3& pos = m_gameModels[i].GetPosition();
        const Vector3& vel = m_gameModels[i].GetVelocity();
        const Vector3& avel = m_gameModels[i].GetAngularVelocity();
        const Vector3& ri = m_gameModels[i].GetRotationalInertia();
        float qx, qy, qz, qw;
        m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
        float r = m_gameModels[i].GetBoundingRadius();
        float mass = m_gameModels[i].GetMass();
        float rest = m_gameModels[i].GetCoefficientRestitution();

        fprintf( f,
                 "ball_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                 "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f %.4f\n",
                 name,
                 pos.x,
                 pos.y,
                 pos.z,
                 vel.x,
                 vel.y,
                 vel.z,
                 avel.x,
                 avel.y,
                 avel.z,
                 qx,
                 qy,
                 qz,
                 qw,
                 r,
                 mass,
                 rest,
                 ri.x,
                 ri.y,
                 ri.z );
    }

    fclose( f );
    return true;
}
