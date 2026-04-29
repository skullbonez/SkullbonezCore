// --- Includes ---
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezHelper.h"
#include <cmath>


// --- Usings ---
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Basics;


GameModelCollection::GameModelCollection()
    : m_spatialGrid( Cfg().broadphaseCell )
{
    m_gameModels.reserve( MAX_GAME_MODELS );
};

void GameModelCollection::AddGameModel( GameModel gameModel )
{
    assert( static_cast<int>( m_gameModels.size() ) < MAX_GAME_MODELS && "Exceeded MAX_GAME_MODELS" );
    m_gameModels.push_back( std::move( gameModel ) );
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


void GameModelCollection::UpdateTerrainShadowUniforms( Geometry::Terrain* terrain )
{
    if ( !terrain )
    {
        return;
    }

    terrain->SetShadowUniforms( m_gameModels.data(), static_cast<int>( m_gameModels.size() ) );
}


Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return m_gameModels[index].GetPosition();
}


const std::vector<GameModel>& GameModelCollection::GetGameModels() const
{
    return m_gameModels;
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
            // (handles grounded/slow m_balls that the sweep test misses)
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


void GameModelCollection::ResetGLResources()
{
}
