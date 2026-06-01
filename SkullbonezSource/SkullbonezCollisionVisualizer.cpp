// =============================================================================
// COLLISION VISUALIZER (SkullbonezCollisionVisualizer.cpp)
// =============================================================================


// --- Includes ---
#include "SkullbonezCollisionVisualizer.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezCommon.h"

#include <algorithm>
#include <cmath>


// --- Usings ---
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;


CollisionVisualizer::~CollisionVisualizer()
{
    ResetResources();
}


void CollisionVisualizer::SetClipPlane( float x, float y, float z, float w )
{
    m_clipPlane[0] = x;
    m_clipPlane[1] = y;
    m_clipPlane[2] = z;
    m_clipPlane[3] = w;
}


void CollisionVisualizer::ResetResources()
{
    m_shader.reset();
    if ( IsGfxReady() )
    {
        if ( m_sphereInstMesh )
        {
            Gfx().DestroyInstancedMesh( m_sphereInstMesh );
        }
        if ( m_boxInstMesh )
        {
            Gfx().DestroyInstancedMesh( m_boxInstMesh );
        }
    }
    m_sphereInstMesh = 0;
    m_boxInstMesh = 0;
    m_sphereVertexCount = 0;
    m_boxVertexCount = 0;
}


void CollisionVisualizer::BuildSphereMesh()
{
    constexpr int slices = 25;
    constexpr int stacks = 25;
    std::vector<float> verts;
    verts.reserve( slices * stacks * 6 * 6 );

    auto emitVertex = [&]( float x, float y, float z )
    {
        verts.push_back( x );
        verts.push_back( y );
        verts.push_back( z );
        verts.push_back( x );
        verts.push_back( y );
        verts.push_back( z );
    };

    for ( int i = 0; i < stacks; ++i )
    {
        float phi0 = _PI * static_cast<float>( i ) / static_cast<float>( stacks );
        float phi1 = _PI * static_cast<float>( i + 1 ) / static_cast<float>( stacks );

        for ( int j = 0; j < slices; ++j )
        {
            float theta0 = _2PI * static_cast<float>( j ) / static_cast<float>( slices );
            float theta1 = _2PI * static_cast<float>( j + 1 ) / static_cast<float>( slices );

            float x00 = sinf( phi0 ) * sinf( theta0 ), y00 = cosf( phi0 ), z00 = -sinf( phi0 ) * cosf( theta0 );
            float x01 = sinf( phi0 ) * sinf( theta1 ), y01 = cosf( phi0 ), z01 = -sinf( phi0 ) * cosf( theta1 );
            float x10 = sinf( phi1 ) * sinf( theta0 ), y10 = cosf( phi1 ), z10 = -sinf( phi1 ) * cosf( theta0 );
            float x11 = sinf( phi1 ) * sinf( theta1 ), y11 = cosf( phi1 ), z11 = -sinf( phi1 ) * cosf( theta1 );

            emitVertex( x00, y00, z00 );
            emitVertex( x11, y11, z11 );
            emitVertex( x10, y10, z10 );

            emitVertex( x00, y00, z00 );
            emitVertex( x01, y01, z01 );
            emitVertex( x11, y11, z11 );
        }
    }

    m_sphereVertexCount = slices * stacks * 6;
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    m_sphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), m_sphereVertexCount, 6, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 2 );
}


void CollisionVisualizer::BuildBoxMesh()
{
    struct CubeFace
    {
        float nx, ny, nz;
        float v0[3], v1[3], v2[3], v3[3];
    };

    // clang-format off
    CubeFace faces[6] = {
        { 1, 0, 0, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1} },
        {-1, 0, 0, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}, {-1,-1,-1} },
        { 0, 1, 0, {-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1} },
        { 0,-1, 0, {-1,-1, 1}, {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1} },
        { 0, 0, 1, {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} },
        { 0, 0,-1, { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} }
    };
    // clang-format on

    std::vector<float> verts;
    verts.reserve( 36 * 6 );

    auto emitVertex = [&]( const float* v, float nx, float ny, float nz )
    {
        verts.push_back( v[0] );
        verts.push_back( v[1] );
        verts.push_back( v[2] );
        verts.push_back( nx );
        verts.push_back( ny );
        verts.push_back( nz );
    };

    for ( int f = 0; f < 6; ++f )
    {
        const CubeFace& face = faces[f];
        const float* v[4] = { face.v0, face.v1, face.v2, face.v3 };
        emitVertex( v[0], face.nx, face.ny, face.nz );
        emitVertex( v[1], face.nx, face.ny, face.nz );
        emitVertex( v[2], face.nx, face.ny, face.nz );

        emitVertex( v[0], face.nx, face.ny, face.nz );
        emitVertex( v[2], face.nx, face.ny, face.nz );
        emitVertex( v[3], face.nx, face.ny, face.nz );
    }

    m_boxVertexCount = 36;
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    m_boxInstMesh = Gfx().CreateInstancedMesh( verts.data(), m_boxVertexCount, 6, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 2 );
}


void CollisionVisualizer::EnsureResources()
{
    if ( !m_shader )
    {
        m_shader = Gfx().CreateShader( "shaders/collision_visualizer" );
    }
    if ( m_sphereInstMesh == 0 )
    {
        BuildSphereMesh();
    }
    if ( m_boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }
}


void CollisionVisualizer::AppendInstance( std::vector<float>& out, const Matrix4& model, const Color& color )
{
    const float* md = model.Data();
    out.insert( out.end(), md, md + 16 );
    out.push_back( color.r );
    out.push_back( color.g );
    out.push_back( color.b );
    out.push_back( color.a );
}


void CollisionVisualizer::Update( float dt, GameModelCollection& models )
{
    const int modelCount = models.GetModelCount();
    if ( static_cast<int>( m_models.size() ) != modelCount )
    {
        m_models.assign( modelCount, TrackedModel() );
    }

    const std::vector<uint8_t>& contacts = models.GetCollisionVisualContacts();
    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const float fadeStep = ( FADE_DURATION > 0.0f ) ? ( dt / FADE_DURATION ) : 1.0f;

    for ( int i = 0; i < modelCount; ++i )
    {
        const bool sleeping = i < static_cast<int>( sleepStates.size() ) && sleepStates[i] != 0;
        const bool contact = i < static_cast<int>( contacts.size() ) && contacts[i] != 0;

        if ( sleeping )
        {
            m_models[i].collisionAmount = 0.0f;
        }
        else if ( contact )
        {
            m_models[i].collisionAmount = 1.0f;
        }
        else
        {
            m_models[i].collisionAmount = (std::max)( 0.0f, m_models[i].collisionAmount - fadeStep );
        }
    }
}


void CollisionVisualizer::BuildSleepGroupSizes( GameModelCollection& models )
{
    const int modelCount = models.GetModelCount();
    m_sleepGroupSizes.assign( modelCount, 1 );

    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const std::vector<int>& islandIds = models.GetSleepIslandVisualIds();

    for ( int i = 0; i < modelCount; ++i )
    {
        if ( i >= static_cast<int>( sleepStates.size() ) || sleepStates[i] == 0 )
        {
            continue;
        }

        const int islandId = i < static_cast<int>( islandIds.size() ) ? islandIds[i] : 0;
        int count = 0;
        for ( int j = 0; j < modelCount; ++j )
        {
            if ( j >= static_cast<int>( sleepStates.size() ) || sleepStates[j] == 0 )
            {
                continue;
            }

            const int otherIslandId = j < static_cast<int>( islandIds.size() ) ? islandIds[j] : 0;
            if ( islandId != 0 ? otherIslandId == islandId : j == i )
            {
                ++count;
            }
        }
        m_sleepGroupSizes[i] = (std::max)( 1, count );
    }
}


CollisionVisualizer::Color CollisionVisualizer::ComputeModelColor( int modelIndex, GameModelCollection& models ) const
{
    static constexpr Color green = { 0.05f, 0.78f, 0.18f, 1.0f };
    static constexpr Color red = { 1.0f, 0.05f, 0.02f, 1.0f };
    static constexpr Color yellow = { 1.0f, 0.86f, 0.05f, 1.0f };
    static constexpr Color sleepingPalette[3] = {
        { 0.52f, 0.22f, 0.95f, 1.0f },
        { 1.0f, 0.22f, 0.67f, 1.0f },
        { 0.05f, 0.42f, 1.0f, 1.0f },
    };

    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const bool sleeping = modelIndex < static_cast<int>( sleepStates.size() ) && sleepStates[modelIndex] != 0;
    if ( sleeping )
    {
        GameModel& model = models.GetModelAtIndex( modelIndex );
        if ( !model.IsBox() && modelIndex < static_cast<int>( m_sleepGroupSizes.size() ) && m_sleepGroupSizes[modelIndex] <= 1 )
        {
            return yellow;
        }

        const std::vector<int>& islandIds = models.GetSleepIslandVisualIds();
        const int islandId = modelIndex < static_cast<int>( islandIds.size() ) && islandIds[modelIndex] != 0 ? islandIds[modelIndex] : modelIndex + 1;
        return sleepingPalette[( islandId - 1 ) % 3];
    }

    const float t = modelIndex < static_cast<int>( m_models.size() ) ? m_models[modelIndex].collisionAmount : 0.0f;
    return {
        green.r + ( red.r - green.r ) * t,
        green.g + ( red.g - green.g ) * t,
        green.b + ( red.b - green.b ) * t,
        1.0f,
    };
}


void CollisionVisualizer::DrawInstances( uint32_t mesh, int vertexCount, const std::vector<float>& instanceData )
{
    const int instanceCount = static_cast<int>( instanceData.size() ) / INSTANCE_FLOATS;
    if ( mesh == 0 || vertexCount <= 0 || instanceCount <= 0 )
    {
        return;
    }

    Gfx().UploadInstanceData( mesh, instanceData.data(), static_cast<int>( instanceData.size() ) );
    Gfx().DrawInstancedMesh( mesh, vertexCount, instanceCount );
}


void CollisionVisualizer::Render( GameModelCollection& models, const Matrix4& view, const Matrix4& proj, const float lightPos[4] )
{
    if ( !m_enabled || models.GetModelCount() <= 0 )
    {
        return;
    }

    EnsureResources();
    BuildSleepGroupSizes( models );

    m_sphereInstanceData.clear();
    m_boxInstanceData.clear();

    const int modelCount = models.GetModelCount();
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models.GetModelAtIndex( i );
        const Color color = ComputeModelColor( i, models );
        if ( model.IsBox() )
        {
            AppendInstance( m_boxInstanceData, model.GetModelMatrix(), color );
        }
        else
        {
            AppendInstance( m_sphereInstanceData, model.GetModelMatrix(), color );
        }
    }

    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = view.m[i] * lightPos[0] + view.m[i + 4] * lightPos[1] + view.m[i + 8] * lightPos[2] + view.m[i + 12] * lightPos[3];
    }
    viewLightPos[3] = lightPos[3];

    m_shader->Use();
    m_shader->SetMat4( "uView", view );
    m_shader->SetMat4( "uProjection", proj );
    m_shader->SetVec4( "uClipPlane", m_clipPlane[0], m_clipPlane[1], m_clipPlane[2], m_clipPlane[3] );
    m_shader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );

    Gfx().SetBlend( false );
    DrawInstances( m_sphereInstMesh, m_sphereVertexCount, m_sphereInstanceData );
    DrawInstances( m_boxInstMesh, m_boxVertexCount, m_boxInstanceData );
}
