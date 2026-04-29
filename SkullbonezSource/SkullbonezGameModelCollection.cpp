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


void GameModelCollection::RenderShadows( Geometry::Terrain* terrain,
                                         const Matrix4& view,
                                         const Matrix4& proj,
                                         const float lightPos[4] )
{
    if ( !terrain || m_gameModels.empty() )
    {
        return;
    }

    if ( !m_shadowMesh )
    {
        BuildShadowResources();
    }

    // Per-instance data: mat4 (16 floats) + float alpha = 17 floats per shadow
    static const int FLOATS_PER_INSTANCE = 17;
    std::vector<float> instanceData;
    instanceData.reserve( static_cast<int>( m_gameModels.size() ) * FLOATS_PER_INSTANCE );

    Vector3 lightDir( lightPos[0], lightPos[1], lightPos[2] );

    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        Vector3 pos = m_gameModels[i].GetPosition();
        float radius = m_gameModels[i].GetBoundingRadius();

        // Light-directed projection: cast ray from light through ball bottom to terrain
        Vector3 ballBottom( pos.x, pos.y - radius, pos.z );
        Vector3 toLight( lightDir.x - ballBottom.x, lightDir.y - ballBottom.y, lightDir.z - ballBottom.z );

        // Guard: light must be above ball bottom
        if ( toLight.y <= 0.001f )
        {
            continue;
        }

        // Rough ground height at ball position for initial height check
        if ( !terrain->IsInBounds( pos.x, pos.z ) )
        {
            continue;
        }

        float groundYAtBall = terrain->GetTerrainHeightAt( pos.x, pos.z );
        float height = ballBottom.y - groundYAtBall;
        if ( height < 0.0f )
        {
            height = 0.0f;
        }
        if ( height >= Cfg().shadowMaxHeight )
        {
            continue;
        }

        // Project: find where light ray from ball bottom hits groundY plane
        float t = ( ballBottom.y - groundYAtBall ) / toLight.y;
        float projX = ballBottom.x - toLight.x * t;
        float projZ = ballBottom.z - toLight.z * t;

        // Resample terrain at projected point
        if ( !terrain->IsInBounds( projX, projZ ) )
        {
            continue;
        }

        float groundY = terrain->GetTerrainHeightAt( projX, projZ );
        Vector3 N = terrain->GetTerrainNormalAt( projX, projZ );

        // Recompute height at projected point for alpha
        height = ballBottom.y - groundY;
        if ( height < 0.0f )
        {
            height = 0.0f;
        }
        if ( height >= Cfg().shadowMaxHeight )
        {
            continue;
        }

        float alpha = Cfg().shadowMaxAlpha * ( 1.0f - height / Cfg().shadowMaxHeight );

        // Penumbra: shadow grows with altitude
        float penumbraScale = 1.0f + height * Cfg().shadowPenumbraGrowth;
        float shadowRadius = radius * Cfg().shadowScale * penumbraScale;

        // Light offset direction for ellipse orientation
        float dx = projX - pos.x;
        float dz = projZ - pos.z;
        float offsetDist = sqrtf( dx * dx + dz * dz );

        // Ellipse stretch proportional to height — linear ramp from 1.0 to max
        float stretchFactor = 1.0f;
        if ( height > 0.5f && offsetDist > 0.01f )
        {
            stretchFactor = 1.0f + ( height / Cfg().shadowMaxHeight ) * ( Cfg().shadowEllipseMaxStretch - 1.0f );
        }

        // Build model matrix: translate → rotate to terrain normal → rotate to light dir → scale ellipse
        Matrix4 model = Matrix4::Translate( projX, groundY + Cfg().shadowOffset, projZ );

        // Tilt to terrain normal
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

        // Rotate ellipse to align stretch with light offset direction, then scale
        if ( stretchFactor > 1.01f && offsetDist > 0.01f )
        {
            float dirX = dx / offsetDist;
            float dirZ = dz / offsetDist;
            float yawDeg = atan2f( dirX, dirZ ) * ( 180.0f / 3.14159265f );
            model = model * Matrix4::RotateAxis( yawDeg, 0.0f, 1.0f, 0.0f );
            model = model * Matrix4::Scale( shadowRadius, 1.0f, shadowRadius * stretchFactor );
        }
        else
        {
            model = model * Matrix4::Scale( shadowRadius );
        }

        // Append instance data: 16 floats for mat4 + 1 float for alpha
        for ( int c = 0; c < 16; ++c )
        {
            instanceData.push_back( model.m[c] );
        }
        instanceData.push_back( alpha );
    }

    int visibleCount = static_cast<int>( instanceData.size() ) / FLOATS_PER_INSTANCE;

    if ( visibleCount == 0 )
    {
        return;
    }

    // Upload instance data
    glBindBuffer( GL_ARRAY_BUFFER, m_shadowInstanceVBO );
    int requiredBytes = visibleCount * FLOATS_PER_INSTANCE * static_cast<int>( sizeof( float ) );
    if ( visibleCount > m_shadowInstanceCapacity )
    {
        m_shadowInstanceCapacity = visibleCount + 64;
        glBufferData( GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>( m_shadowInstanceCapacity ) * FLOATS_PER_INSTANCE * static_cast<GLsizeiptr>( sizeof( float ) ),
                      nullptr,
                      GL_DYNAMIC_DRAW );
    }
    glBufferSubData( GL_ARRAY_BUFFER, 0, requiredBytes, instanceData.data() );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // Draw all shadows in a single instanced call
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( -1.0f, -1.0f );
    glDisable( GL_CULL_FACE );

    m_shadowShader->Use();
    m_shadowShader->SetMat4( "uView", view );
    m_shadowShader->SetMat4( "uProjection", proj );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, m_shadowTexture );
    m_shadowShader->SetInt( "uShadowTex", 0 );

    glBindVertexArray( m_shadowMesh->GetVAO() );
    glDrawArraysInstanced( GL_TRIANGLES, 0, m_shadowMesh->GetVertexCount(), visibleCount );
    glBindVertexArray( 0 );

    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_CULL_FACE );
    glDisable( GL_BLEND );
}


Vector3 GameModelCollection::GetModelPosition( int index )
{
    if ( index < 0 || index >= static_cast<int>( m_gameModels.size() ) )
    {
        throw std::runtime_error( "No game model exists at the specified index.  (GameModelCollection::GetModelPosition)" );
    }

    return m_gameModels[index].GetPosition();
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


void GameModelCollection::BuildShadowResources()
{
    // Unit XZ quad with UVs: 2 triangles, 6 vertices, position(3) + texcoord(2) = 5 floats each
    // Quad spans [-1,+1] in XZ so that Scale(radius) sizes it correctly
    float verts[] = {
        // pos(x,y,z)    uv(u,v)
        -1.0f,
        0.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        -1.0f,
        1.0f,
        0.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        0.0f,
        -1.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        -1.0f,
        0.0f,
        1.0f,
        0.0f,
        1.0f,
    };

    m_shadowMesh = std::make_unique<Mesh>( verts, 6, false, true );
    m_shadowShader = std::make_unique<Shader>( "SkullbonezData/shaders/shadow.vert",
                                               "SkullbonezData/shaders/shadow.frag" );

    // Set up instance attributes on the shadow mesh's VAO
    glGenBuffers( 1, &m_shadowInstanceVBO );
    glBindVertexArray( m_shadowMesh->GetVAO() );
    glBindBuffer( GL_ARRAY_BUFFER, m_shadowInstanceVBO );

    // Instance attribute layout: mat4 at locations 3-6, float alpha at location 7
    int stride = 17 * static_cast<int>( sizeof( float ) );

    for ( int col = 0; col < 4; ++col )
    {
        GLuint loc = 3 + static_cast<GLuint>( col );
        glEnableVertexAttribArray( loc );
        glVertexAttribPointer( loc, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( col * 4 * sizeof( float ) ) ) );
        glVertexAttribDivisor( loc, 1 );
    }

    // Alpha at location 7 (offset = 16 floats = 64 bytes)
    glEnableVertexAttribArray( 7 );
    glVertexAttribPointer( 7, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>( static_cast<intptr_t>( 16 * sizeof( float ) ) ) );
    glVertexAttribDivisor( 7, 1 );

    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // Generate soft Gaussian shadow texture
    int texSize = Cfg().shadowTextureSize;
    std::vector<unsigned char> texData( texSize * texSize );
    float center = ( texSize - 1 ) * 0.5f;

    for ( int y = 0; y < texSize; ++y )
    {
        for ( int x = 0; x < texSize; ++x )
        {
            float dx = ( x - center ) / center; // -1 to +1
            float dy = ( y - center ) / center;
            float dist = sqrtf( dx * dx + dy * dy );
            // Smooth Gaussian falloff clamped to circle
            float val = 0.0f;
            if ( dist < 1.0f )
            {
                float g = expf( -( dist * dist ) / ( 2.0f * 0.28f * 0.28f ) );
                val = g * ( 1.0f - dist * dist ); // Gaussian × radial fade for clean edge
            }
            texData[y * texSize + x] = static_cast<unsigned char>( val * 255.0f );
        }
    }

    glGenTextures( 1, &m_shadowTexture );
    glBindTexture( GL_TEXTURE_2D, m_shadowTexture );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, texSize, texSize, 0, GL_RED, GL_UNSIGNED_BYTE, texData.data() );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glBindTexture( GL_TEXTURE_2D, 0 );
}


void GameModelCollection::ResetGLResources()
{
    m_shadowMesh.reset();
    m_shadowShader.reset();
    if ( m_shadowTexture )
    {
        glDeleteTextures( 1, &m_shadowTexture );
        m_shadowTexture = 0;
    }
    if ( m_shadowInstanceVBO )
    {
        glDeleteBuffers( 1, &m_shadowInstanceVBO );
        m_shadowInstanceVBO = 0;
    }
    m_shadowInstanceCapacity = 0;
}
