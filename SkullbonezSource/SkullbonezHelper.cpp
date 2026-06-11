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
std::unique_ptr<IShader> SkullbonezHelper::shadowDepthShader;
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
uint32_t SkullbonezHelper::pineInstMesh = 0;
int SkullbonezHelper::pineVertexCount = 0;
std::vector<float> SkullbonezHelper::pineInstanceData;

static constexpr int INSTANCE_MATRIX_FLOATS = 16;
static constexpr int INSTANCE_TINT_FLOATS = 4;
static constexpr int INSTANCE_FLOATS = INSTANCE_MATRIX_FLOATS + INSTANCE_TINT_FLOATS;
static constexpr int PRIMITIVE_SHAPE_MESH = 0;
static constexpr int PRIMITIVE_SHAPE_SPHERE = 1;

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
    shadowDepthShader.reset();
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
    if ( pineInstMesh != 0 )
    {
        Gfx().DestroyInstancedMesh( pineInstMesh );
        pineInstMesh = 0;
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


void SkullbonezHelper::EnsureShadowDepthShader()
{
    if ( !shadowDepthShader )
    {
        // One shared instanced depth shader is enough for balls, boxes, and pine
        // visuals because all three meshes expose the same static attributes and
        // per-instance model/tint layout. The fragment output is irrelevant; the
        // depth attachment is the shadow map product.
        shadowDepthShader = Gfx().CreateShader( "shaders/shadow_depth_instanced" );
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


void SkullbonezHelper::DrawSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow )
{
    const int objectStyle = ObjectStyleForShader( cinematic );
    const bool useLowPolySphereMesh = objectStyle == 6;
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
    sphereShader->SetInt( "uObjectStyle", objectStyle );
    sphereShader->SetInt( "uPrimitiveShape", PRIMITIVE_SHAPE_SPHERE );

    // Low-poly spheres still cast real shadows onto terrain, but they do not
    // receive object shadows. The shadow map is single and terrain-sized, so
    // ball-on-ball receiver shadows alias badly across the large flat facets
    // used by the low-poly beachball style.
    const bool receiveSphereShadows = shadow && shadow->objectsReceive && !useLowPolySphereMesh;
    ApplyShadowReceiverUniforms( *sphereShader, shadow, receiveSphereShadows, true );
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


void SkullbonezHelper::DrawShadowDepthSphereBatchBegin( const Matrix4& view, const Matrix4& proj, const CinematicRenderConfig* cinematic )
{
    // Match the visible sphere mesh selection. If a low-poly style is active,
    // the depth pass also uses the faceted mesh, which prevents a smooth sphere
    // shadow from appearing under a visibly low-poly ball.
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

    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    // These are light-space view/projection matrices, not the camera matrices.
    // The shader transforms each instance into the light's clip space and writes
    // normal depth, which later receivers compare against their own light-space
    // fragment depth.
    shadowDepthShader->SetMat4( "uView", view );
    shadowDepthShader->SetMat4( "uProjection", proj );
    shadowDepthShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    sphereInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthSphereBatchModel( const Matrix4& model )
{
    DrawSphereBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthSphereBatchEnd()
{
    int instanceCount = static_cast<int>( sphereInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && activeSphereInstMesh != 0 )
    {
        // Upload only the compact per-instance stream, then issue one instanced
        // draw for every sphere caster. This keeps the shadow pass draw-call
        // count predictable even in scenes with hundreds of balls.
        Gfx().UploadInstanceData( activeSphereInstMesh, sphereInstanceData.data(), static_cast<int>( sphereInstanceData.size() ) );
        Gfx().DrawInstancedMesh( activeSphereInstMesh, activeSphereVertexCount, instanceCount );
    }
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


void SkullbonezHelper::DrawBoxBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow )
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
    sphereShader->SetInt( "uPrimitiveShape", PRIMITIVE_SHAPE_MESH );
    ApplyShadowReceiverUniforms( *sphereShader, shadow, shadow ? shadow->objectsReceive : false, true );
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


void SkullbonezHelper::DrawShadowDepthBoxBatchBegin( const Matrix4& view, const Matrix4& proj )
{
    if ( boxInstMesh == 0 )
    {
        BuildBoxMesh();
    }
    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    // Box casters use the same unit-cube mesh and model matrices as the visible
    // box pass. Rotation and half-extents are already baked into the model matrix,
    // so the shadow-map silhouette naturally follows tilted or elongated boxes.
    shadowDepthShader->SetMat4( "uView", view );
    shadowDepthShader->SetMat4( "uProjection", proj );
    shadowDepthShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    boxInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthBoxBatchModel( const Matrix4& model )
{
    DrawBoxBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthBoxBatchEnd()
{
    int instanceCount = static_cast<int>( boxInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && boxInstMesh != 0 )
    {
        // This draw is the box-caster fix point: if a scene has boxes and shadow
        // maps are active, their depth is written here before terrain/objects
        // sample the map in the forward pass.
        Gfx().UploadInstanceData( boxInstMesh, boxInstanceData.data(), static_cast<int>( boxInstanceData.size() ) );
        Gfx().DrawInstancedMesh( boxInstMesh, boxVertexCount, instanceCount );
    }
}


void SkullbonezHelper::BuildPineMesh()
{
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::PineTriangleVertexCount() * 8 );

    PrimitiveMeshes::EmitUnitPinePyramid( [&]( const PrimitiveMeshes::VertexPNUV& vertex )
                                          { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v } ); } );

    pineVertexCount = PrimitiveMeshes::PineTriangleVertexCount();

    int staticAttribSizes[] = { 3, 3, 2 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    pineInstMesh = Gfx().CreateInstancedMesh( verts.data(), pineVertexCount, 8, MAX_GAME_MODELS, INSTANCE_FLOATS, 3, instanceAttribSizes, 5, staticAttribSizes, 3 );

    pineInstanceData.reserve( MAX_GAME_MODELS * INSTANCE_FLOATS );
}


void SkullbonezHelper::DrawPineBatchBegin( const Matrix4& view, const Matrix4& proj, const float lightPos[4], bool isTransparent, const CinematicRenderConfig* cinematic, const ShadowFrameData* shadow )
{
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh();
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
    sphereShader->SetInt( "uPrimitiveShape", PRIMITIVE_SHAPE_MESH );
    ApplyShadowReceiverUniforms( *sphereShader, shadow, shadow ? shadow->objectsReceive : false, true );
    pineInstanceData.clear();
}


void SkullbonezHelper::DrawPineBatchModel( const Matrix4& model, float tintR, float tintG, float tintB, float colorOverride )
{
    const float* md = model.Data();
    pineInstanceData.insert( pineInstanceData.end(), md, md + INSTANCE_MATRIX_FLOATS );
    pineInstanceData.push_back( tintR );
    pineInstanceData.push_back( tintG );
    pineInstanceData.push_back( tintB );
    pineInstanceData.push_back( colorOverride );
}


void SkullbonezHelper::DrawPineBatchEnd()
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 )
    {
        Gfx().UploadInstanceData( pineInstMesh, pineInstanceData.data(), static_cast<int>( pineInstanceData.size() ) );
        Gfx().DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
    Gfx().SetBlend( false );
}


void SkullbonezHelper::DrawShadowDepthPineBatchBegin( const Matrix4& view, const Matrix4& proj )
{
    if ( pineInstMesh == 0 )
    {
        BuildPineMesh();
    }
    EnsureShadowDepthShader();
    shadowDepthShader->Use();
    // Pine visuals are authored as box-backed scene objects with a special
    // material mode. They get their own depth mesh so the shadow map receives
    // the pointed tree/pyramid silhouette instead of the underlying physics box.
    shadowDepthShader->SetMat4( "uView", view );
    shadowDepthShader->SetMat4( "uProjection", proj );
    shadowDepthShader->SetVec4( "uClipPlane", sClipPlane[0], sClipPlane[1], sClipPlane[2], sClipPlane[3] );
    pineInstanceData.clear();
}


void SkullbonezHelper::DrawShadowDepthPineBatchModel( const Matrix4& model )
{
    DrawPineBatchModel( model, 1.0f, 1.0f, 1.0f, 0.0f );
}


void SkullbonezHelper::DrawShadowDepthPineBatchEnd()
{
    int instanceCount = static_cast<int>( pineInstanceData.size() ) / INSTANCE_FLOATS;
    if ( instanceCount > 0 && pineInstMesh != 0 )
    {
        Gfx().UploadInstanceData( pineInstMesh, pineInstanceData.data(), static_cast<int>( pineInstanceData.size() ) );
        Gfx().DrawInstancedMesh( pineInstMesh, pineVertexCount, instanceCount );
    }
}


void SkullbonezHelper::StateSetup()
{
    // Initial GL state is now set by RenderBackendGL::Init()
    // This method is retained for any additional state setup needed after backend init
}
