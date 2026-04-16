/*---------------------------------------------------------------------------------*/
/*			      SEE HEADER FILE FOR CLASS AND METHOD DESCRIPTIONS				   */
/*---------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------------
                                  THE SKULLBONEZ CORE
                                        _______
                                     .-"       "-.
                                    /             \
                                   /               \
                                   �   .--. .--.   �
                                   � )/   � �   \( �
                                   �/ \__/   \__/ \�
                                   /      /^\      \
                                   \__    '='    __/
                                     �\         /�
                                     �\'"VUUUV"'/�
                                     \ `"""""""` /
                                      `-._____.-'

                                 www.simoneschbach.com
-----------------------------------------------------------------------------------*/

/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezGameModelCollection.h"
#include <cmath>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::GameObjects;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
GameModelCollection::GameModelCollection( void )
    : spatialGrid( BROADPHASE_CELL_SIZE )
{
    this->gameModels.reserve( 200 );
};

/* -- ADD GAME MODEL --------------------------------------------------------------*/
void GameModelCollection::AddGameModel( GameModel gameModel )
{
    this->gameModels.push_back( std::move( gameModel ) );
}

/* -- RENDER MODELS ---------------------------------------------------------------*/
void GameModelCollection::RenderModels( const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    for ( int x = 0; x < (int)this->gameModels.size(); ++x )
    {
        this->gameModels[x].RenderCollisionBounds( view, proj, lightPos );
    }
}

/* -- RENDER SHADOWS --------------------------------------------------------------*/
void GameModelCollection::RenderShadows( Geometry::Terrain* terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj )
{
    if ( !terrain )
    {
        return;
    }

    if ( !this->shadowMesh )
    {
        this->BuildShadowMesh();
    }

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( -1.0f, -1.0f );
    glDisable( GL_CULL_FACE );

    this->shadowShader->Use();
    this->shadowShader->SetMat4( "uView", view );
    this->shadowShader->SetMat4( "uProjection", proj );

    for ( int i = 0; i < (int)this->gameModels.size(); ++i )
    {
        Vector3 pos = this->gameModels[i].GetPosition();
        float radius = this->gameModels[i].GetBoundingRadius();

        if ( !terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundY = terrain->GetTerrainHeightAt( pos.x, pos.z );
        float height = pos.y - groundY - radius;
        if ( height < 0.0f )
        {
            height = 0.0f;
        }
        if ( height >= SHADOW_MAX_HEIGHT )
        {
            continue;
        }

        float alpha = SHADOW_MAX_ALPHA * ( 1.0f - height / SHADOW_MAX_HEIGHT );
        float shadowRadius = radius * SHADOW_SCALE;

        Vector3 N = terrain->GetTerrainNormalAt( pos.x, pos.z );

        // Build model matrix: translate → rotate to terrain normal → scale
        Matrix4 model = Matrix4::Translate( pos.x, groundY + SHADOW_OFFSET, pos.z );

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

        this->shadowShader->SetMat4( "uModel", model );
        this->shadowShader->SetFloat( "uAlpha", alpha );
        this->shadowMesh->Draw();
    }

    glUseProgram( 0 );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_CULL_FACE );
    glDisable( GL_BLEND );
}

/* -- GET MODEL POSITION ----------------------------------------------------------*/
Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= (int)this->gameModels.size() )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return this->gameModels[index].GetPosition();
}

/* -- RUN PHYSICS -------------------------------------------------------------------------------------------------------------------------------------------------------*/
void GameModelCollection::RunPhysics( float fChangeInTime )
{
    std::vector<float> timeRemaining( (int)this->gameModels.size(), fChangeInTime );

    // update the velocity of all models
    for ( int x = 0; x < (int)this->gameModels.size(); ++x )
    {
        this->gameModels[x].ApplyForces( fChangeInTime );
    }

    // broadphase: populate spatial grid and generate candidate pairs
    this->spatialGrid.Clear();
    for ( int i = 0; i < (int)this->gameModels.size(); ++i )
    {
        this->spatialGrid.Insert( i, this->gameModels[i].GetPosition(), this->gameModels[i].GetBoundingRadius() );
    }

    std::vector<std::pair<int, int>> candidatePairs;
    this->spatialGrid.GetCandidatePairs( candidatePairs );

    // detect and respond to collisions between game models (broadphase-culled pairs only)
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
        float colTime = this->gameModels[x].CollisionDetectGameModel( this->gameModels[y], availableTime );

        // if there is a response required, perform it
        if ( this->gameModels[x].IsResponseRequired() && this->gameModels[y].IsResponseRequired() )
        {
            // advance both models to the collision point
            this->gameModels[x].UpdatePosition( colTime );
            this->gameModels[y].UpdatePosition( colTime );

            // subtract consumed time
            timeRemaining[x] -= colTime;
            timeRemaining[y] -= colTime;

            // velocity-only response (clears isResponseRequired on both models)
            this->gameModels[x].CollisionResponseGameModel( this->gameModels[y] );
        }
        else
        {
            // sweep test found no collision — check for static overlap
            // (handles grounded/slow balls that the sweep test misses)
            this->gameModels[x].StaticOverlapResponseGameModel( this->gameModels[y] );
        }
    }

    // detect and respond to collisions between game models and the terrain
    for ( int x = 0; x < (int)this->gameModels.size(); ++x )
    {
        // only check terrain if this model has remaining time
        if ( timeRemaining[x] > 0.0f )
        {
            // check the collision time
            float colTime = this->gameModels[x].CollisionDetectTerrain( timeRemaining[x] );

            // if a response is required, perform it
            if ( this->gameModels[x].IsResponseRequired() )
            {
                // update the time step before the collision
                this->gameModels[x].UpdatePosition( colTime );

                // calculate response and update the remaining time step (terrain response advances position internally)
                this->gameModels[x].CollisionResponseTerrain( timeRemaining[x] - colTime );

                // terrain response already advanced position; zero remaining time
                timeRemaining[x] = 0.0f;
            }
        }
    }

    // apply the remaining time steps
    for ( int x = 0; x < (int)this->gameModels.size(); ++x )
    {
        // advance by whatever time remains
        if ( timeRemaining[x] > 0.0f )
        {
            this->gameModels[x].UpdatePosition( timeRemaining[x] );
        }
    }
}

/* -- BUILD SHADOW MESH -----------------------------------------------------------*/
void GameModelCollection::BuildShadowMesh( void )
{
    // Unit-radius disc in XZ plane, converted from triangle fan to triangles.
    // Center at (0,0,0), ring vertices at unit distance.
    // The shadow shader uses length(aPosition.xz) for alpha fade.
    std::vector<float> verts;
    verts.reserve( SHADOW_SEGMENTS * 3 * 3 );

    for ( int s = 0; s < SHADOW_SEGMENTS; ++s )
    {
        float a0 = ( _2PI * s ) / SHADOW_SEGMENTS;
        float a1 = ( _2PI * ( s + 1 ) ) / SHADOW_SEGMENTS;

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

    this->shadowMesh = std::make_unique<Mesh>( verts.data(), SHADOW_SEGMENTS * 3, false, false );
    this->shadowShader = std::make_unique<Shader>( "SkullbonezData/shaders/shadow.vert",
                                                   "SkullbonezData/shaders/shadow.frag" );
}

/* -- RESET GL RESOURCES ----------------------------------------------------------*/
void GameModelCollection::ResetGLResources( void )
{
    this->shadowMesh.reset();
    this->shadowShader.reset();
}
