// --- Includes ---
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezHelper.h"
#include "SkullbonezIRenderBackend.h"
#include <cmath>


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Basics;


// Per-instance data layout: mat4 (16 floats) + alpha (1 float)
static constexpr int SHADOW_INSTANCE_FLOATS = 17;


GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( MAX_GAME_MODELS );
    m_shadowInstanceData.reserve( MAX_GAME_MODELS * SHADOW_INSTANCE_FLOATS );
};

void GameModelCollection::AddGameModel( GameModel gameModel )
{
    assert( static_cast<int>( m_gameModels.size() ) < MAX_GAME_MODELS && "Exceeded MAX_GAME_MODELS" );
    m_gameModels.push_back( std::move( gameModel ) );
}


void GameModelCollection::Clear()
{
    m_gameModels.clear();
}


void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    if ( m_gameModels.empty() )
    {
        return;
    }

    SkullbonezHelper::DrawSphereBatchBegin( view, proj, lightPos, Cfg().renderCollisionVolumes );
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        Matrix4 model = m_gameModels[x].GetModelMatrix();
        SkullbonezHelper::DrawSphereBatchModel( model );
    }
    SkullbonezHelper::DrawSphereBatchEnd();
}


void GameModelCollection::RenderShadows( Geometry::Terrain* m_terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj )
{
    if ( !m_terrain )
    {
        return;
    }

    if ( !m_shadowInstMesh )
    {
        BuildShadowMesh();
    }

    // Build per-instance data: model matrix (16 floats) + alpha (1 float)
    m_shadowInstanceData.clear();
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        Vector3 pos = m_gameModels[i].GetPosition();
        float radius = m_gameModels[i].GetBoundingRadius();

        if ( !m_terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundY = m_terrain->GetTerrainHeightAt( pos.x, pos.z );
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

        Vector3 N = m_terrain->GetTerrainNormalAt( pos.x, pos.z );

        // Build model matrix: translate → rotate to terrain normal → scale
        Matrix4 model = Matrix4::Translate( pos.x, groundY + Cfg().shadowOffset, pos.z );

        float cosA = N.y;
        if ( cosA < 0.9999f )
        {
            float axisX = N.z;
            float axisZ = -N.x;
            float axisMag = sqrtf( axisX * axisX + axisZ * axisZ );
            axisX /= axisMag;
            axisZ /= axisMag;
            float angleDeg = acosf( cosA ) * ( 180.0f / 3.14159265f );
            model = model * Matrix4::RotateAxis( angleDeg, axisX, 0.0f, axisZ );
        }

        model = model * Matrix4::Scale( shadowRadius );

        // Append mat4 (16 floats) + alpha (1 float)
        const float* md = model.Data();
        m_shadowInstanceData.insert( m_shadowInstanceData.end(), md, md + 16 );
        m_shadowInstanceData.push_back( alpha );
    }

    int instanceCount = static_cast<int>( m_shadowInstanceData.size() ) / SHADOW_INSTANCE_FLOATS;
    if ( instanceCount == 0 )
    {
        return;
    }

    // Upload instance data
    Gfx().UploadInstanceData( m_shadowInstMesh, m_shadowInstanceData.data(), static_cast<int>( m_shadowInstanceData.size() ) );

    // Render all shadows in one instanced draw call
    Gfx().SetBlend( true );
    Gfx().SetPolygonOffset( true, -1.0f, -1.0f );
    Gfx().SetCullFace( false );

    m_shadowShader->Use();
    m_shadowShader->SetMat4( "uView", view );
    m_shadowShader->SetMat4( "uProjection", proj );

    Gfx().DrawInstancedMesh( m_shadowInstMesh, m_shadowDiscVertexCount, instanceCount );

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
    std::vector<float> timeRemaining( static_cast<int>( m_gameModels.size() ), fChangeInTime );

    // update the velocity of all models
    PROFILE_BEGIN( "Frame/Physics/ApplyForces" );
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        m_gameModels[x].ApplyForces( fChangeInTime );
    }
    PROFILE_END( "Frame/Physics/ApplyForces" );

    // broadphase: populate spatial grid and generate candidate pairs
    PROFILE_BEGIN( "Frame/Physics/Broadphase" );
    m_spatialGrid.Clear();
    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
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
        if ( timeRemaining[x] <= 0.0f || timeRemaining[y] <= 0.0f )
        {
            continue;
        }

        // use the minimum remaining time window for this pair
        float availableTime = ( std::min )( timeRemaining[x], timeRemaining[y] );

        // check the collision time
        float colTime = m_gameModels[x].CollisionDetectGameModel( m_gameModels[y], availableTime );

        // if there is a response required, perform it
        if ( m_gameModels[x].IsResponseRequired() && m_gameModels[y].IsResponseRequired() )
        {
            // advance both models to the collision point
            m_gameModels[x].UpdatePosition( colTime );
            m_gameModels[y].UpdatePosition( colTime );

            // subtract consumed time
            timeRemaining[x] -= colTime;
            timeRemaining[y] -= colTime;

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
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        // only check m_terrain if this model has remaining time
        if ( timeRemaining[x] > 0.0f )
        {
            // check the collision time
            float colTime = m_gameModels[x].CollisionDetectTerrain( timeRemaining[x] );

            // if a response is required, perform it
            if ( m_gameModels[x].IsResponseRequired() )
            {
                // update the time step before the collision
                m_gameModels[x].UpdatePosition( colTime );

                // calculate response and update the remaining time step (m_terrain response advances m_position internally)
                m_gameModels[x].CollisionResponseTerrain( timeRemaining[x] - colTime );

                // m_terrain response already advanced m_position; zero remaining time
                timeRemaining[x] = 0.0f;
            }
        }
    }
    PROFILE_END( "Frame/Physics/Terrain" );

    // apply the remaining time steps
    PROFILE_BEGIN( "Frame/Physics/Integrate" );
    for ( int x = 0; x < static_cast<int>( m_gameModels.size() ); ++x )
    {
        // advance by whatever time remains
        if ( timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( timeRemaining[x] );
        }
    }
    PROFILE_END( "Frame/Physics/Integrate" );
}


void GameModelCollection::BuildShadowMesh()
{
    // Unit-radius disc in XZ plane, converted from triangle fan to triangles.
    // Center at (0,0,0), ring vertices at unit distance.
    // The shadow shader uses length(aPosition.xz) for alpha fade.
    std::vector<float> verts;
    verts.reserve( Cfg().shadowSegments * 3 * 3 );

    for ( int s = 0; s < Cfg().shadowSegments; ++s )
    {
        float a0 = ( _2PI * s ) / Cfg().shadowSegments;
        float a1 = ( _2PI * ( s + 1 ) ) / Cfg().shadowSegments;

        // Center vertex
        verts.push_back( 0.0f );
        verts.push_back( 0.0f );
        verts.push_back( 0.0f );
        // Ring vertex 0
        verts.push_back( cosf( a0 ) );
        verts.push_back( 0.0f );
        verts.push_back( sinf( a0 ) );
        // Ring vertex 1
        verts.push_back( cosf( a1 ) );
        verts.push_back( 0.0f );
        verts.push_back( sinf( a1 ) );
    }

    m_shadowDiscVertexCount = Cfg().shadowSegments * 3;

    // Instance layout: 5 attributes (4×vec4 for mat4 + 1×float for alpha), starting at location 3
    int instanceAttribSizes[] = { 4, 4, 4, 4, 1 };
    m_shadowInstMesh = Gfx().CreateInstancedMesh( verts.data(), m_shadowDiscVertexCount, 3, MAX_GAME_MODELS, SHADOW_INSTANCE_FLOATS, 3, instanceAttribSizes, 5 );

    // Create shader
    m_shadowShader = Gfx().CreateShader( "SkullbonezData/shaders/shadow.vert",
                                         "SkullbonezData/shaders/shadow.frag" );
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
