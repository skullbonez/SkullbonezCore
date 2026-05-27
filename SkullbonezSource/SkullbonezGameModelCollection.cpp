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


// Per-instance data layout: mat4 (16 floats) + alpha (1 float)
static constexpr int SHADOW_INSTANCE_FLOATS = 17;


GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( MAX_GAME_MODELS );
    m_timeRemaining.reserve( MAX_GAME_MODELS );
    m_groundedThisFrame.reserve( MAX_GAME_MODELS );
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

        float availableTime = ( std::min )( m_timeRemaining[x], m_timeRemaining[y] );
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

        float availableTime = ( std::min )( m_timeRemaining[x], m_timeRemaining[y] );
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
