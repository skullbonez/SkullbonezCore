/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   |   .--. .--.   |
                                   | )/   | |   \( |
                                   |/ \__/   \__/ \|
                                   /      /^\      \
                                   \__    '='    __/
                                     |\         /|
                                     |\'"VUUUV"'/|
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include <cmath>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
GameModelCollection::GameModelCollection( void )
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( 200 );
};

/* -- ADD GAME MODEL --------------------------------------------------------------*/
void GameModelCollection::AddGameModel( GameModel gameModel )
{
    m_gameModels.push_back( std::move( gameModel ) );
}

/* -- RENDER MODELS ---------------------------------------------------------------*/
void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    for ( int x = 0; x < (int)m_gameModels.size(); ++x )
    {
        m_gameModels[x].RenderCollisionBounds( view, proj, lightPos );
    }
}

/* -- RENDER SHADOWS --------------------------------------------------------------*/
void GameModelCollection::RenderShadows( Geometry::Terrain* m_terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj )
{
    if ( !m_terrain )
    {
        return;
    }

    if ( !m_shadowMesh )
    {
        this->BuildShadowMesh();
    }

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( -1.0f, -1.0f );
    glDisable( GL_CULL_FACE );

    m_shadowShader->Use();
    m_shadowShader->SetMat4( "uView", view );
    m_shadowShader->SetMat4( "uProjection", proj );

    for ( int i = 0; i < (int)m_gameModels.size(); ++i )
    {
        Vector3 pos = m_gameModels[i].GetPosition();
        float m_radius = m_gameModels[i].GetBoundingRadius();

        if ( !m_terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundY = m_terrain->GetTerrainHeightAt( pos.x, pos.z );
        float m_height = pos.y - groundY - m_radius;
        if ( m_height < 0.0f )
        {
            m_height = 0.0f;
        }
        if ( m_height >= Cfg().shadowMaxHeight )
        {
            continue;
        }

        float alpha = Cfg().shadowMaxAlpha * ( 1.0f - m_height / Cfg().shadowMaxHeight );
        float shadowRadius = m_radius * Cfg().shadowScale;

        Vector3 N = m_terrain->GetTerrainNormalAt( pos.x, pos.z );

        // Build model matrix: translate → rotate to m_terrain m_normal → scale
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

        m_shadowShader->SetMat4( "uModel", model );
        m_shadowShader->SetFloat( "uAlpha", alpha );
        m_shadowMesh->Draw();
    }

    glUseProgram( 0 );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_CULL_FACE );
    glDisable( GL_BLEND );
}

/* -- GET MODEL POSITION ----------------------------------------------------------*/
Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= (int)m_gameModels.size() )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return m_gameModels[index].GetPosition();
}

/* -- RUN PHYSICS -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void GameModelCollection::RunPhysics( float fChangeInTime )
{
    std::vector<float> timeRemaining( (int)m_gameModels.size(), fChangeInTime );

    // update the velocity of all models
    {
        PROFILE_SCOPED( "Gravity" );
        for ( int x = 0; x < (int)m_gameModels.size(); ++x )
        {
            m_gameModels[x].ApplyForces( fChangeInTime );
        }
    }

    // broadphase: populate spatial grid and generate candidate pairs
    PROFILE_BEGIN( "Broadphase" );
    m_spatialGrid.Clear();
    for ( int i = 0; i < (int)m_gameModels.size(); ++i )
    {
        m_spatialGrid.Insert( i, m_gameModels[i].GetPosition(), m_gameModels[i].GetBoundingRadius() );
    }

    std::vector<std::pair<int, int>> candidatePairs;
    m_spatialGrid.GetCandidatePairs( candidatePairs );
    PROFILE_END( "Broadphase" );

    // detect and respond to collisions between game models (broadphase-culled pairs only)
    PROFILE_BEGIN( "NarrowPhase" );
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
            // (handles grounded/slow m_balls that the sweep test misses)
            m_gameModels[x].StaticOverlapResponseGameModel( m_gameModels[y] );
        }
    }
    PROFILE_END( "NarrowPhase" );

    // detect and respond to collisions between game models and the m_terrain
    PROFILE_BEGIN( "TerrainCollision" );
    for ( int x = 0; x < (int)m_gameModels.size(); ++x )
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
    PROFILE_END( "TerrainCollision" );

    // apply the remaining time steps
    for ( int x = 0; x < (int)m_gameModels.size(); ++x )
    {
        // advance by whatever time remains
        if ( timeRemaining[x] > 0.0f )
        {
            m_gameModels[x].UpdatePosition( timeRemaining[x] );
        }
    }
}

/* -- BUILD SHADOW MESH -----------------------------------------------------------*/
void GameModelCollection::BuildShadowMesh( void )
{
    // Unit-m_radius disc in XZ plane, converted from triangle fan to triangles.
    // Center at (0,0,0), ring vertices at unit m_distance.
    // The shadow m_shader uses length(aPosition.xz) for alpha fade.
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

    m_shadowMesh = std::make_unique<Mesh>( verts.data(), Cfg().shadowSegments * 3, false, false );
    m_shadowShader = std::make_unique<Shader>( "SkullbonezData/shaders/shadow.vert",
                                               "SkullbonezData/shaders/shadow.frag" );
}

/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void GameModelCollection::ResetGLResources( void )
{
    m_shadowMesh.reset();
    m_shadowShader.reset();
}
