// --- Includes ---
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezHelper.h"
#include "SkullbonezIRenderBackend.h"
#include <cmath>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Basics;


// Per-instance data layout: mat4 (16 floats) + alpha (1 float)
static constexpr int SHADOW_INSTANCE_FLOATS = 17;


GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell ), m_rollLog( nullptr )
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
    m_planeSeenGreen.push_back( false );
    m_planeFailed.push_back( false );
    m_planeBlueStreak.push_back( 0 );
}


void GameModelCollection::SetRollLog( FILE* file )
{
    m_rollLog = file;
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
    m_timeRemaining.clear();
    m_groundedThisFrame.clear();
    m_planeSeenGreen.clear();
    m_planeFailed.clear();
    m_planeBlueStreak.clear();
}


void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    if ( m_gameModels.empty() )
    {
        return;
    }

    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, Cfg().runtimeRender.renderCollisionVolumes );
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        Matrix4 model = m_gameModels[x].GetModelMatrix();
        SkullbonezHelper::DrawSphereBatchModel( model );
    }
    SkullbonezHelper::DrawSphereBatchEnd();
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

    // update the velocity of all models
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    for ( int x = 0; x < modelCount; ++x )
    {
        m_gameModels[x].ApplyForces( fChangeInTime );
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // broadphase: populate spatial grid and generate candidate pairs
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    for ( int i = 0; i < modelCount; ++i )
    {
        m_spatialGrid.Insert( i, m_gameModels[i].GetPosition(), m_gameModels[i].GetBoundingRadius() );
    }

    std::vector<std::pair<int, int>>& candidatePairs = m_candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    PROFILE_END( "Frame/Physics/Broadphase" );

    // detect and respond to collisions between game models (broadphase-culled pairs only)
    PROFILE_BEGIN( "Frame/Physics/Narrowphase" );
    for ( const auto& cp : candidatePairs )
    {
        int x = cp.first;
        int y = cp.second;

        // skip pairs where either ball has exhausted its frame time
        if ( m_timeRemaining[x] <= 0.0f || m_timeRemaining[y] <= 0.0f )
        {
            continue;
        }

        // use the minimum remaining time window for this pair
        float availableTime = ( std::min )( m_timeRemaining[x], m_timeRemaining[y] );

        // check the collision time
        float colTime = m_gameModels[x].CollisionDetectGameModel( m_gameModels[y], availableTime );

        // if there is a response required, perform it
        if ( m_gameModels[x].IsResponseRequired() && m_gameModels[y].IsResponseRequired() )
        {
            // advance both models to the collision point
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );

            // subtract consumed time
            m_timeRemaining[x] -= colTime;
            m_timeRemaining[y] -= colTime;

            // velocity-only response (clears m_isResponseRequired on both models)
            m_gameModels[x].CollisionResponseGameModel( m_gameModels[y] );
        }
        else
        {
            // sweep test found no collision — check for static overlap
            // (handles slow m_balls that the sweep test misses)
            m_gameModels[x].StaticOverlapResponseGameModel( m_gameModels[y] );
        }
    }
    PROFILE_END( "Frame/Physics/Narrowphase" );

    // detect and respond to collisions between game models and the m_terrain
    PROFILE_BEGIN( "Frame/Physics/Terrain" );
    for ( int x = 0; x < modelCount; ++x )
    {
        // only check m_terrain if this model has remaining time
        if ( m_timeRemaining[x] > 0.0f )
        {
            // check the collision time
            float colTime = m_gameModels[x].CollisionDetectTerrain( m_timeRemaining[x] );

            // if a response is required, perform it
            if ( m_gameModels[x].IsResponseRequired() )
            {
                // update the time step before the collision
                m_gameModels[x].UpdatePosition( colTime );

                // calculate response and update the remaining time step (m_terrain response advances m_position internally)
                m_gameModels[x].CollisionResponseTerrain( m_timeRemaining[x] - colTime );

                m_groundedThisFrame[x] = 1;

                // m_terrain response already advanced m_position; zero remaining time
                m_timeRemaining[x] = 0.0f;
            }
        }
    }
    PROFILE_END( "Frame/Physics/Terrain" );

    // apply the remaining time steps
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    for ( int x = 0; x < modelCount; ++x )
    {
        // advance by whatever time remains
        if ( m_timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( m_timeRemaining[x] );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );

    // Roll orientation log: one line per named ball per frame
    if ( m_rollLog )
    {
        const float axisToleranceRad = 5.0f * _PI / 180.0f;
        const int lockBlueFrames = 60;

        if ( static_cast<int>( m_planeSeenGreen.size() ) != modelCount ||
             static_cast<int>( m_planeBlueStreak.size() ) != modelCount )
        {
            m_planeSeenGreen.assign( modelCount, false );
            m_planeFailed.assign( modelCount, false );
            m_planeBlueStreak.assign( modelCount, 0 );
        }

        for ( int i = 0; i < modelCount; ++i )
        {
            const char* name = m_gameModels[i].GetName();
            if ( !name[0] )
            {
                continue;
            }

            bool isGrounded = ( m_groundedThisFrame[i] != 0 );
            m_gameModels[i].SetGrounded( isGrounded );
            const char* state = isGrounded ? "LANDED  " : "AIRBORNE";
            Vector3 spike = m_gameModels[i].GetOrientationUp();
            Vector3 omega = m_gameModels[i].GetAngularVelocity();
            Vector3 pos = m_gameModels[i].GetPosition();

            bool withinPlaneTolerance = false;
            float omegaMag = VectorMag( omega );
            float spikeMag = VectorMag( spike );
            if ( omegaMag > TOLERANCE && spikeMag > TOLERANCE )
            {
                float dotRed = ( spike * omega ) / ( spikeMag * omegaMag );
                if ( dotRed > 1.0f )
                {
                    dotRed = 1.0f;
                }
                else if ( dotRed < -1.0f )
                {
                    dotRed = -1.0f;
                }
                float angleFromPerp = asinf( fabsf( dotRed ) );
                withinPlaneTolerance = ( angleFromPerp <= axisToleranceRad );
            }

            if ( isGrounded && withinPlaneTolerance )
            {
                if ( !m_planeSeenGreen[i] )
                {
                    ++m_planeBlueStreak[i];
                    if ( m_planeBlueStreak[i] >= lockBlueFrames )
                    {
                        m_planeSeenGreen[i] = true;
                    }
                }
            }
            else if ( isGrounded )
            {
                m_planeBlueStreak[i] = 0;
                if ( m_planeSeenGreen[i] )
                {
                    m_planeFailed[i] = true;
                }
            }
            else
            {
                m_planeBlueStreak[i] = 0;
            }

            const char* planeState = withinPlaneTolerance ? "BLUE" : "WHITE";
            const char* failState = m_planeFailed[i] ? "FAIL" : ( m_planeSeenGreen[i] ? "LOCKED" : "UNLOCKED" );

            fprintf( m_rollLog, "[%s] %s  pos.y=%8.2f  spike=(%6.3f, %6.3f, %6.3f)  omega=(%6.3f, %6.3f, %6.3f)  axis=%s  axis_lock=%s\n", name, state, pos.y, spike.x, spike.y, spike.z, omega.x, omega.y, omega.z, planeState, failState );
        }
        fflush( m_rollLog );
    }
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
