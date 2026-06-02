// --- Includes ---
#include "SkullbonezHelper.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezPrimitiveMeshBuilder.h"

#include <vector>


// --- Usings ---
using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;


std::unique_ptr<IShader> SkullbonezHelper::sphereShader;
uint32_t SkullbonezHelper::sphereInstMesh = 0;
int SkullbonezHelper::sphereVertexCount = 0;
std::vector<float> SkullbonezHelper::sphereInstanceData;
uint32_t SkullbonezHelper::boxInstMesh = 0;
int SkullbonezHelper::boxVertexCount = 0;
std::vector<float> SkullbonezHelper::boxInstanceData;

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
    if ( boxInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( boxInstMesh );
        boxInstMesh = 0;
    }
}


void SkullbonezHelper::EnsureSphereMesh()
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
        sphereShader = Gfx().CreateShader( "shaders/lit_textured_instanced" );
        sphereShader->Use();
        sphereShader->SetVec4( "uLightAmbient", 1.0f, 0.5f, 0.5f, 1.0f );
        sphereShader->SetVec4( "uLightDiffuse", 1.0f, 0.5f, 0.5f, 1.0f );
        sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
    }
}


void SkullbonezHelper::BuildSphereMesh( int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphere( slices, stacks, [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                     { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static layout: 3 attributes (pos3, normal3, uv2) at locations 0-2
    int staticAttribSizes[] = { 3, 3, 2 };
    // Instance layout: 4 attributes (4xvec4 for mat4 = 16 floats), starting at location 3
    int instanceAttribSizes[] = { 4, 4, 4, 4 };
    sphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), sphereVertexCount, 8, MAX_GAME_MODELS, 16, 3, instanceAttribSizes, 4, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * 16 );
}


void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent )
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
        sphereShader = Gfx().CreateShader( "shaders/lit_textured_instanced" );
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


// =============================================================================
// BOX INSTANCED RENDERING
// =============================================================================
//
// Renders unit cubes [-1,1]^3 scaled by half-extents via the model matrix.
// Uses the same lit_textured_instanced shader as spheres so lighting is
// consistent. The cube has outward-facing normals and simple planar UV.
//
// =============================================================================


void SkullbonezHelper::BuildBoxMesh()
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitBox( [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                  { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4 };
    boxInstMesh = Gfx().CreateInstancedMesh( verts.data(), boxVertexCount, 8, MAX_GAME_MODELS, 16, 3, instanceAttribSizes, 4, staticAttribSizes, 3 );

    boxInstanceData.reserve( MAX_GAME_MODELS * 16 );
}


void SkullbonezHelper::DrawBoxBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent )
{
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }

    // Reuse sphere shader (same vertex layout, same lighting model)
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
        sphereShader = Gfx().CreateShader( "shaders/lit_textured_instanced" );
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
    boxInstanceData.clear();
}


void SkullbonezHelper::DrawBoxBatchModel( const Matrix4& model )
{
    const float* md = model.Data();
    boxInstanceData.insert( boxInstanceData.end(), md, md + 16 );
}


void SkullbonezHelper::DrawBoxBatchEnd()
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / 16;
    if ( instanceCount > 0 )
    {
        Gfx().UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        Gfx().DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
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
    if ( lines.empty() )
    {
        return;
    }

    // Pack line endpoints: each pair becomes 2 vec3 values.
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
    Gfx().DrawLines( verts.data(), vertCount, r, g, b, viewProj.Data() );
}
