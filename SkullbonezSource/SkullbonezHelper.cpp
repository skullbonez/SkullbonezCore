// --- Includes ---
#include "SkullbonezHelper.h"
#include "SkullbonezProfiler.h"
#include "SkullbonezIRenderBackend.h"
#include <vector>
#include <cmath>


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
    // Generate a unit sphere with normals and texcoords (8 floats per vertex).
    // Local mesh frame is pre-rotated +90° about Y so it naturally matches the
    // engine's physics orientation (no per-instance visual yaw shim required).
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

            // 4 corners of the quad.
            // Bake +90 yaw in local space: (x, z) -> (z, -x).
            // This rotates the generated sphere's theta=0 meridian so the texture
            // seam/pole frame matches the engine's roll/pole convention. With this
            // baked into the mesh, poles track physics orientation naturally and we
            // can remove the per-instance runtime RotY90 compatibility shim.
            float x00 = sinf( phi0 ) * sinf( theta0 ), y00 = cosf( phi0 ), z00 = -sinf( phi0 ) * cosf( theta0 );
            float x01 = sinf( phi0 ) * sinf( theta1 ), y01 = cosf( phi0 ), z01 = -sinf( phi0 ) * cosf( theta1 );
            float x10 = sinf( phi1 ) * sinf( theta0 ), y10 = cosf( phi1 ), z10 = -sinf( phi1 ) * cosf( theta0 );
            float x11 = sinf( phi1 ) * sinf( theta1 ), y11 = cosf( phi1 ), z11 = -sinf( phi1 ) * cosf( theta1 );

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
// Renders unit cubes [-1,1]³ scaled by half-extents via the model matrix.
// Uses the same lit_textured_instanced shader as spheres so lighting is
// consistent. The cube has outward-facing normals and simple planar UV.
//
// =============================================================================


void SkullbonezHelper::BuildBoxMesh()
{
    // A unit cube [-1,1]³ (6 faces × 2 triangles × 3 vertices = 36 vertices).
    // Each vertex: pos(3), normal(3), uv(2) = 8 floats.
    //
    //   Face layout (CCW winding when viewed from outside):
    //     +X face: normal ( 1, 0, 0)
    //     -X face: normal (-1, 0, 0)
    //     +Y face: normal ( 0, 1, 0)
    //     -Y face: normal ( 0,-1, 0)
    //     +Z face: normal ( 0, 0, 1)
    //     -Z face: normal ( 0, 0,-1)

    struct CubeFace
    {
        float nx, ny, nz;
        float v0[3], v1[3], v2[3], v3[3]; // 4 corners (CCW: v0→v1→v2, v0→v2→v3)
    };

    // clang-format off
    CubeFace faces[6] = {
        { 1, 0, 0, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1} }, // +X
        {-1, 0, 0, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}, {-1,-1,-1} }, // -X
        { 0, 1, 0, {-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1} }, // +Y
        { 0,-1, 0, {-1,-1, 1}, {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1} }, // -Y
        { 0, 0, 1, {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1} }, // +Z
        { 0, 0,-1, { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1} }  // -Z
    };
    // clang-format on

    std::vector<float> verts;
    verts.reserve( 36 * 8 );

    for ( int f = 0; f < 6; ++f )
    {
        const CubeFace& fc = faces[f];
        float nx = fc.nx, ny = fc.ny, nz = fc.nz;

        // UV corners for the face (simple planar mapping)
        float uv[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
        const float* v[4] = { fc.v0, fc.v1, fc.v2, fc.v3 };

        // Triangle 1: v0, v1, v2
        verts.insert( verts.end(), { v[0][0], v[0][1], v[0][2], nx, ny, nz, uv[0][0], uv[0][1] } );
        verts.insert( verts.end(), { v[1][0], v[1][1], v[1][2], nx, ny, nz, uv[1][0], uv[1][1] } );
        verts.insert( verts.end(), { v[2][0], v[2][1], v[2][2], nx, ny, nz, uv[2][0], uv[2][1] } );

        // Triangle 2: v0, v2, v3
        verts.insert( verts.end(), { v[0][0], v[0][1], v[0][2], nx, ny, nz, uv[0][0], uv[0][1] } );
        verts.insert( verts.end(), { v[2][0], v[2][1], v[2][2], nx, ny, nz, uv[2][0], uv[2][1] } );
        verts.insert( verts.end(), { v[3][0], v[3][1], v[3][2], nx, ny, nz, uv[3][0], uv[3][1] } );
    }

    boxVertexCount = 36;

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
    Gfx().DrawLines( verts.data(), vertCount, r, g, b, viewProj.Data() );
}
