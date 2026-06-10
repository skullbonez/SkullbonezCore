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
uint32_t SkullbonezHelper::lowPolySphereInstMesh = 0;
int SkullbonezHelper::lowPolySphereVertexCount = 0;
uint32_t SkullbonezHelper::activeSphereInstMesh = 0;
int SkullbonezHelper::activeSphereVertexCount = 0;
uint32_t SkullbonezHelper::boxInstMesh = 0;
int SkullbonezHelper::boxVertexCount = 0;
std::vector<float> SkullbonezHelper::boxInstanceData;

static constexpr int INSTANCE_MATRIX_FLOATS = 16;
static constexpr int INSTANCE_TINT_FLOATS = 4;
static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_TINT_FLOATS;

static void ApplySceneLightUniforms( IShader& shader )
{
    const auto& light = Cfg().sceneLight;
    shader.SetVec4( "uLightAmbient", light.colorR, light.colorG, light.colorB, light.colorA );
    shader.SetVec4( "uLightDiffuse", light.colorR, light.colorG, light.colorB, light.colorA );
}

static void ApplyBatchLightUniforms( IShader& shader, const float lightPos[4], const CinematicRenderConfig* cinematicOverride )
{
    if ( lightPos[3] == 0.0f )
    {
        const CinematicRenderConfig& cinematic = cinematicOverride ? *cinematicOverride : Cfg().cinematicRender;
        shader.SetVec4( "uLightAmbient", 0.28f, 0.15f, 0.06f, 1.0f );
        shader.SetVec4( "uLightDiffuse",
                        cinematic.sunColorR * 2.35f,
                        cinematic.sunColorG * 2.35f,
                        cinematic.sunColorB * 2.35f,
                        1.0f );
        return;
    }

    ApplySceneLightUniforms( shader );
}

static int ObjectStyleForShader( const CinematicRenderConfig* cinematicOverride )
{
    return cinematicOverride ? cinematicOverride->objectStyle : Cfg().cinematicRender.objectStyle;
}

void SkullbonezHelper::SetClipPlane( float x, float y, float z, float w )
{
    sClipPlane[0] = x;
    sClipPlane[1] = y;
    sClipPlane[2] = z;
    sClipPlane[3] = w;
}


void SkullbonezHelper::ResetRenderResources()
{
    sphereShader.reset();
    if ( sphereInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( sphereInstMesh );
        sphereInstMesh = 0;
    }
    if ( lowPolySphereInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( lowPolySphereInstMesh );
        lowPolySphereInstMesh = 0;
    }
    if ( boxInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( boxInstMesh );
        boxInstMesh = 0;
    }
    activeSphereInstMesh = 0;
    activeSphereVertexCount = 0;
}


void SkullbonezHelper::EnsureSphereShader()
{
    if ( !sphereShader )
    {
        sphereShader = Gfx().CreateShader( "shaders/lit_textured_instanced" );
        sphereShader->Use();
        ApplySceneLightUniforms( *sphereShader );
        sphereShader->SetVec4( "uMaterialAmbient", 0.2f, 0.2f, 0.2f, 1.0f );
        sphereShader->SetVec4( "uMaterialDiffuse", 0.8f, 0.8f, 0.8f, 1.0f );
    }
}


void SkullbonezHelper::EnsureSphereMesh()
{
    if ( sphereInstMesh == 0 )
    {
        BuildSphereMesh( 25, 25 );
    }
    EnsureSphereShader();
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
    // Instance layout: 4 attributes for mat4 plus RGBA tint/override, starting at location 3.
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    sphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), sphereVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::BuildLowPolySphereMesh( int slices, int stacks )
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 8 );

    PrimitiveMeshes::EmitUnitSphereFlat( slices, stacks, [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                         { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    lowPolySphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    lowPolySphereInstMesh = Gfx().CreateInstancedMesh( verts.data(), lowPolySphereVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 3 );

    sphereInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic )
{
    const bool useLowPolySphereMesh = ObjectStyleForShader( cinematic ) == 6;
    if ( useLowPolySphereMesh )
    {
        if ( lowPolySphereInstMesh == 0 )
        {
            BuildLowPolySphereMesh( 12, 7 );
        }
        activeSphereInstMesh = lowPolySphereInstMesh;
        activeSphereVertexCount = lowPolySphereVertexCount;
    }
    else
    {
        if ( sphereInstMesh == 0 )
        {
            BuildSphereMesh( 25, 25 );
        }
        activeSphereInstMesh = sphereInstMesh;
        activeSphereVertexCount = sphereVertexCount;
    }
    EnsureSphereShader();

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
    ApplyBatchLightUniforms( *sphereShader, lightPos, cinematic );
    sphereShader->SetMat4( "uView", view );
    sphereShader->SetMat4( "uProjection", proj );
    sphereShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    sphereShader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );
    sphereShader->SetInt( "uObjectStyle", ObjectStyleForShader( cinematic ) );
    sphereInstanceData.clear();
}


void SkullbonezHelper::DrawSphereBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    const float* md = model.Data();
    sphereInstanceData.insert( sphereInstanceData.end(), md, md + INSTANCE_MATRIX_FLOATS );
    sphereInstanceData.push_back( tintR );
    sphereInstanceData.push_back( tintG );
    sphereInstanceData.push_back( tintB );
    sphereInstanceData.push_back( colorOverride );
}


void SkullbonezHelper::DrawSphereBatchEnd()
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        Gfx().UploadInstanceData( activeSphereInstMesh, sphereInstanceData.data(), static_cast<int>( sphereInstanceData.size() ) );
        Gfx().DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
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
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    boxInstMesh = Gfx().CreateInstancedMesh( verts.data(), boxVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 3 );

    boxInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawBoxBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic )
{
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }

    // Reuse sphere shader (same vertex layout, same lighting model).
    EnsureSphereShader();

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
    ApplyBatchLightUniforms( *sphereShader, lightPos, cinematic );
    sphereShader->SetMat4( "uView", view );
    sphereShader->SetMat4( "uProjection", proj );
    sphereShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    sphereShader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );
    sphereShader->SetInt( "uObjectStyle", ObjectStyleForShader( cinematic ) );
    boxInstanceData.clear();
}


void SkullbonezHelper::DrawBoxBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    const float* md = model.Data();
    boxInstanceData.insert( boxInstanceData.end(), md, md + INSTANCE_MATRIX_FLOATS );
    boxInstanceData.push_back( tintR );
    boxInstanceData.push_back( tintG );
    boxInstanceData.push_back( tintB );
    boxInstanceData.push_back( colorOverride );
}


void SkullbonezHelper::DrawBoxBatchEnd()
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
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
