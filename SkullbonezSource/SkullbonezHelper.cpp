// --- Includes ---
#include "SkullbonezHelper.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include <vector>
#include <cmath>
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


std::unique_ptr<IShader> SkullbonezHelper::sphereShader;
uint32_t SkullbonezHelper::sphereInstMesh = 0;
int SkullbonezHelper::sphereVertexCount = 0;
std::vector<float> SkullbonezHelper::sphereInstanceData;
std::unique_ptr<IShader> SkullbonezHelper::debugLineShader;
unsigned int SkullbonezHelper::debugLineVAO = 0;
unsigned int SkullbonezHelper::debugLineVBO = 0;

void SkullbonezHelper::SetClipPlane( float x, float y, float z, float w )
{
    sClipPlane[0] = x;
    sClipPlane[1] = y;
    sClipPlane[2] = z;
    sClipPlane[3] = w;
}


void SkullbonezHelper::ResetGLResources()
{
    sphereShader.reset();
    if ( sphereInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( sphereInstMesh );
        sphereInstMesh = 0;
    }
    debugLineShader.reset();
    if ( debugLineVBO != 0 )
    {
        glDeleteBuffers( 1, &debugLineVBO );
        debugLineVBO = 0;
    }
    if ( debugLineVAO != 0 )
    {
        glDeleteVertexArrays( 1, &debugLineVAO );
        debugLineVAO = 0;
    }
}


void SkullbonezHelper::BuildSphereMesh( int slices, int stacks )
{
    // Generate a unit sphere with normals and texcoords (8 floats per vertex)
    std::vector<float> verts;
    verts.reserve( slices * stacks * 6 * 8 );

    for ( int i = 0; i < stacks; ++i )
    {
        float phi0 = _PI * static_cast<float>( i ) / static_cast<float>( stacks );
        float phi1 = _PI * static_cast<float>( i + 1 ) / static_cast<float>( stacks );

        for ( int j = 0; j < slices; ++j )
        {
            float theta0 = _2PI * static_cast<float>( j ) / static_cast<float>( slices );
            float theta1 = _2PI * static_cast<float>( j + 1 ) / static_cast<float>( slices );

            // 4 corners of the quad
            float x00 = sinf( phi0 ) * cosf( theta0 ), y00 = cosf( phi0 ), z00 = sinf( phi0 ) * sinf( theta0 );
            float x01 = sinf( phi0 ) * cosf( theta1 ), y01 = cosf( phi0 ), z01 = sinf( phi0 ) * sinf( theta1 );
            float x10 = sinf( phi1 ) * cosf( theta0 ), y10 = cosf( phi1 ), z10 = sinf( phi1 ) * sinf( theta0 );
            float x11 = sinf( phi1 ) * cosf( theta1 ), y11 = cosf( phi1 ), z11 = sinf( phi1 ) * sinf( theta1 );

            float u0 = static_cast<float>( j ) / static_cast<float>( slices ), v0 = static_cast<float>( i ) / static_cast<float>( stacks );
            float u1 = static_cast<float>( j + 1 ) / static_cast<float>( slices ), v1 = static_cast<float>( i + 1 ) / static_cast<float>( stacks );

            // Triangle 1: (0,0) → (1,1) → (1,0)  (CCW viewed from outside)
            verts.insert( verts.end(), { x00, y00, z00, x00, y00, z00, u0, v0 } );
            verts.insert( verts.end(), { x11, y11, z11, x11, y11, z11, u1, v1 } );
            verts.insert( verts.end(), { x10, y10, z10, x10, y10, z10, u0, v1 } );

            // Triangle 2: (0,0) → (0,1) → (1,1)  (CCW viewed from outside)
            verts.insert( verts.end(), { x00, y00, z00, x00, y00, z00, u0, v0 } );
            verts.insert( verts.end(), { x01, y01, z01, x01, y01, z01, u1, v0 } );
            verts.insert( verts.end(), { x11, y11, z11, x11, y11, z11, u1, v1 } );
        }
    }

    sphereVertexCount = slices * stacks * 6;

    // Static layout: 3 attributes (pos3, normal3, uv2) at locations 0-2
    int staticAttribSizes[] = { 3, 3, 2 };
    // Instance layout: 4 attributes (4×vec4 for mat4 = 16 floats), starting at location 3
    int instanceAttribSizes[] = { 4, 4, 4, 4 };
    sphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), sphereVertexCount, 8, MAX_GAME_MODELS, 16, 3, instanceAttribSizes, 4, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * 16 );
}


void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent )
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
        sphereShader = Gfx().CreateShader(
            "SkullbonezData/shaders/lit_textured_instanced.vert",
            "SkullbonezData/shaders/lit_textured_instanced.frag" );
        sphereShader->Use();
        sphereShader->SetVec4( "uLightAmbient", 1.0f, 0.5f, 0.5f, 1.0f );
        sphereShader->SetVec4( "uLightDiffuse", 1.0f, 0.5f, 0.5f, 1.0f );
        sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
    }

    if ( isTransparent )
    {
        Gfx().SetBlend( true );
    }

    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = view.m[i] * lightPos[0] + view.m[i + 4] * lightPos[1] + view.m[i + 8] * lightPos[2] + view.m[i + 12] * lightPos[3];
    }
    viewLightPos[3] = lightPos[3];

    sphereShader->Use();
    sphereShader->SetMat4( "uView", view );
    sphereShader->SetMat4( "uProjection", proj );
    sphereShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    sphereShader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );
    sphereInstanceData.clear();
}


void SkullbonezHelper::DrawSphereBatchModel( const Matrix4& model )
{
    const float* md = model.Data();
    sphereInstanceData.insert( sphereInstanceData.end(), md, md + 16 );
}


void SkullbonezHelper::DrawSphereBatchEnd()
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / 16;
    if ( instanceCount > 0 )
    {
        Gfx().UploadInstanceData( sphereInstMesh, sphereInstanceData.data(), static_cast<int>( sphereInstanceData.size() ) );
        Gfx().DrawInstancedMesh( sphereInstMesh, sphereVertexCount, instanceCount );
    }
    Gfx().SetBlend( false );
}


void SkullbonezHelper::StateSetup()
{
    // Initial GL state is now set by RenderBackendGL::Init()
    // This method is retained for any additional state setup needed after backend init
}


void SkullbonezHelper::DrawDebugVectors(
    const Matrix4& viewProj,
    const std::vector<std::pair<Vector3, Vector3>>& lines,
    float r,
    float g,
    float b )
{
    // GL-only — skip on DX11/DX12
    if ( strstr( Gfx().GetRendererName(), "OpenGL" ) == nullptr )
    {
        return;
    }
    if ( lines.empty() )
    {
        return;
    }

    // Lazy-init VAO/VBO
    if ( debugLineVAO == 0 )
    {
        glGenVertexArrays( 1, &debugLineVAO );
        glGenBuffers( 1, &debugLineVBO );
        glBindVertexArray( debugLineVAO );
        glBindBuffer( GL_ARRAY_BUFFER, debugLineVBO );
        // Allocate a generous streaming buffer (2048 vec3 endpoints)
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( 2048 * 3 * sizeof( float ) ), nullptr, GL_DYNAMIC_DRAW );
        glEnableVertexAttribArray( 0 );
        glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof( float ), reinterpret_cast<void*>( 0 ) );
        glBindVertexArray( 0 );
    }

    // Lazy-init shader
    if ( !debugLineShader )
    {
        debugLineShader = Gfx().CreateShader( "SkullbonezData/shaders/debug_line.vert", "SkullbonezData/shaders/debug_line.frag" );
    }

    // Pack line endpoints: each pair → 2 × vec3 = 6 floats
    std::vector<float> verts;
    verts.reserve( lines.size() * 6 );
    for ( const auto& seg : lines )
    {
        verts.push_back( seg.first.x );
        verts.push_back( seg.first.y );
        verts.push_back( seg.first.z );
        verts.push_back( seg.second.x );
        verts.push_back( seg.second.y );
        verts.push_back( seg.second.z );
    }

    int vertCount = static_cast<int>( lines.size() * 2 );

    // Upload
    glBindBuffer( GL_ARRAY_BUFFER, debugLineVBO );
    GLsizeiptr needed = static_cast<GLsizeiptr>( verts.size() * sizeof( float ) );
    GLsizeiptr capacity = static_cast<GLsizeiptr>( 2048 * 3 * sizeof( float ) );
    if ( needed > capacity )
    {
        // Orphan and reallocate
        glBufferData( GL_ARRAY_BUFFER, needed, nullptr, GL_DYNAMIC_DRAW );
        capacity = needed;
    }
    glBufferSubData( GL_ARRAY_BUFFER, 0, needed, verts.data() );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    // Draw — disable depth test so lines render through opaque ball geometry
    Gfx().SetDepthTest( false );
    debugLineShader->Use();
    debugLineShader->SetMat4( "uViewProj", viewProj );
    debugLineShader->SetVec4( "uColor", r, g, b, 1.0f );

    glBindVertexArray( debugLineVAO );
    glLineWidth( 2.0f );
    glDrawArrays( GL_LINES, 0, vertCount );
    glLineWidth( 1.0f );
    glBindVertexArray( 0 );
    Gfx().SetDepthTest( true );
}
