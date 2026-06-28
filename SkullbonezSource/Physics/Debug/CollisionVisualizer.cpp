/*
File: SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp
Purpose:
  Builds debug drawing for collision shapes and contact diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  AABB (Axis-Aligned Bounding Box): Box aligned to world axes, used as a cheap
  broadphase/debug volume.
  Asset system: Runtime-owned registry borrowed to resolve the debug shader
  source while the visualizer owns the backend shader handle.
  Render resource factory: Borrowed renderer capability that creates the debug
  shader, primitive meshes, and dynamic hull buffer for the active backend.
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  GPU (Graphics Processing Unit): Backend-owned device executing draw work.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  UV: Texture coordinate pair carried by shared primitive builders.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/Debug/CollisionVisualizer.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
// =============================================================================
// COLLISION VISUALIZER (CollisionVisualizer.cpp)
// =============================================================================
//
// Debug renderer for the V-key collision/sleep-state view.
//
// This path deliberately stays separate from normal textured model rendering:
//   - normal rendering draws material-like balls and boxes;
//   - collision visualization draws the same collision volumes with state colors.
//
// The primitive coordinates are shared through PrimitiveMeshBuilder.h so
// sphere tessellation, box winding, and local orientation cannot drift between the
// two renderers. GPU resources, shaders, and instance payloads remain separate
// because this renderer needs a per-instance color in addition to each model matrix.
// =============================================================================


#include "CollisionVisualizer.h"
#include "../../Assets/AssetSystem.h"
#include "../ConvexHullShape.h"
#include "../../GameObjects/GameModelCollection.h"
#include "../../GameObjects/GameModel.h"
#include "../../Rendering/IRenderCommandContext.h"
#include "../../Rendering/IRenderResourceFactory.h"
#include "../../Rendering/PrimitiveMeshBuilder.h"
#include "../../Core/Common.h"

#include <algorithm>
#include <array>
#include <variant>


using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Rendering;

static constexpr int COLLISION_INSTANCE_FLOATS = 20;
static constexpr int HULL_MAX_TRIANGLE_VERTICES =
    ConvexHullShape::MAX_FACES * ( ConvexHullShape::MAX_FACE_VERTICES - 2 ) * 3;
static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + COLLISION_INSTANCE_FLOATS;
static std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> sHullDebugVertexData = {};

CollisionVisualizer::~CollisionVisualizer()
{
    ResetResources();
}


void CollisionVisualizer::SetClipPlane( float x, float y, float z, float w )
{
    // The render loop uses this during water reflection passes so the debug solids
    // obey the same reflection clipping as the normal model renderer.
    m_clipPlane[0] = x;
    m_clipPlane[1] = y;
    m_clipPlane[2] = z;
    m_clipPlane[3] = w;
}


void CollisionVisualizer::SetAlphaOverride( float alpha )
{
    m_alphaOverride = alpha;
}


void CollisionVisualizer::ResetResources( IRenderResourceFactory* renderResources )
{
    // Backend resources are context/device-owned. Explicit runtime teardown
    // supplies the live factory; the destructor may run after backend shutdown,
    // so a null factory only clears CPU-side handles.
    m_shader.reset();
    if ( renderResources )
    {
        if ( m_sphereInstMesh )
        {
            renderResources->DestroyInstancedMesh( m_sphereInstMesh );
        }
        if ( m_boxInstMesh )
        {
            renderResources->DestroyInstancedMesh( m_boxInstMesh );
        }
        if ( m_hullDynamicVB )
        {
            renderResources->DestroyDynamicVB( m_hullDynamicVB );
        }
    }
    m_sphereInstMesh = 0;
    m_boxInstMesh = 0;
    m_hullDynamicVB = 0;
    m_sphereVertexCount = 0;
    m_boxVertexCount = 0;
}


void CollisionVisualizer::BuildSphereMesh( IRenderResourceFactory& renderResources )
{
    // The collision shader only reads position and normal from the static vertex
    // buffer. UVs are generated by the shared primitive builder for the normal
    // renderer, then intentionally dropped here.
    constexpr int slices = 25;
    constexpr int stacks = 25;
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks ) * 6 );

    PrimitiveMeshes::EmitUnitSphere(
        slices,
        stacks,
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz } ); } );

    m_sphereVertexCount = PrimitiveMeshes::SphereTriangleVertexCount( slices, stacks );

    // Static attributes occupy shader locations 0-1: position and normal.
    // Instance attributes start at location 3, leaving location 2 unused so the
    // layout stays compatible with the normal renderer's position/normal/uv slots.
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    m_sphereInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                            m_sphereVertexCount,
                                                            6,
                                                            MAX_GAME_MODELS,
                                                            INSTANCE_FLOATS,
                                                            3,
                                                            instanceAttribSizes,
                                                            5,
                                                            staticAttribSizes,
                                                            2 );
}


void CollisionVisualizer::BuildBoxMesh( IRenderResourceFactory& renderResources )
{
    // Same shared unit cube as RenderHelper::BuildBoxMesh(), packed for the
    // collision shader's smaller static layout: position plus face normal.
    std::vector<float> verts;
    verts.reserve( PrimitiveMeshes::BoxTriangleVertexCount() * 6 );

    PrimitiveMeshes::EmitUnitBox(
        [&]( const PrimitiveMeshes::VertexPNUV& vertex )
        { verts.insert( verts.end(), { vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz } ); } );

    m_boxVertexCount = PrimitiveMeshes::BoxTriangleVertexCount();

    // Instance layout is mat4 + rgba:
    //   locations 3-6: model matrix columns
    //   location 7:    debug color
    int staticAttribSizes[] = { 3, 3 };
    int instanceAttribSizes[] = { 4, 4, 4, 4, 4 };
    m_boxInstMesh = renderResources.CreateInstancedMesh( verts.data(),
                                                         m_boxVertexCount,
                                                         6,
                                                         MAX_GAME_MODELS,
                                                         INSTANCE_FLOATS,
                                                         3,
                                                         instanceAttribSizes,
                                                         5,
                                                         staticAttribSizes,
                                                         2 );
}


void CollisionVisualizer::EnsureResources( const SkullbonezCore::Assets::AssetSystem& assets,
                                           IRenderResourceFactory& renderResources )
{
    // Resource creation is lazy so toggling the visualizer off has no startup cost.
    // The shader and both primitive meshes are created together on the first visible
    // frame, then reused until ResetResources() is called.
    if ( !m_shader )
    {
        // Lifetime: the visualizer owns its shader handle, while shader source
        // lookup comes from the Run-owned asset registry borrowed for this pass.
        m_shader = assets.CreateShader( renderResources, "shader.collision_visualizer" );
    }
    if ( m_sphereInstMesh == 0 )
    {
        BuildSphereMesh( renderResources );
    }
    if ( m_boxInstMesh == 0 )
    {
        BuildBoxMesh( renderResources );
    }
    if ( m_hullDynamicVB == 0 )
    {
        int attribs[] = { 3, 3, 4, 4, 4, 4, 4 };
        m_hullDynamicVB = renderResources.CreateDynamicVB( attribs, 7, HULL_MAX_TRIANGLE_VERTICES );
    }
}


void CollisionVisualizer::AppendInstance( std::vector<float>& out, const Matrix4& model, const Color& color )
{
    // The instance buffer is a tightly packed stream of:
    //   16 floats: model matrix
    //    4 floats: rgba debug color
    //
    // Render() builds separate sphere and box streams because each shape has its
    // own static mesh handle but the same instance payload format.
    const float* md = model.Data();
    out.insert( out.end(), md, md + 16 );
    out.push_back( color.r );
    out.push_back( color.g );
    out.push_back( color.b );
    out.push_back( color.a );
}


void CollisionVisualizer::Update( float dt, GameModelCollection& models )
{
    // This mirrors per-frame contact/sleep state into a visual fade cache. The
    // cache is for rendering only; the solver never reads these values back.
    // GameModelCollection records whether each object contacted another object
    // during the current physics step. This visualizer keeps a small amount of
    // temporal state so contact flashes do not disappear in a single frame.
    //
    // Color state:
    //   sleeping object: collision flash forced to zero; sleep palette wins
    //   contact object:  collision amount snaps to 1.0
    //   otherwise:       collision amount fades back toward 0.0 over FADE_DURATION
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
    // Sleeping objects are grouped into islands by the solver. The color palette
    // uses that island id so stacks/resting piles are easy to distinguish at a
    // glance. Group size is also used for a special single-sphere color below.
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
    // Awake objects fade between green and red:
    //   green = no recent contact
    //   red   = contact this frame or still fading from one
    //
    // Sleeping objects use a separate palette by sleep island so stable groups are
    // visible even when they are no longer generating active contact flashes.
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
        if ( !model.IsBox() && modelIndex < static_cast<int>( m_sleepGroupSizes.size() ) &&
             m_sleepGroupSizes[modelIndex] <= 1 )
        {
            return yellow;
        }

        const std::vector<int>& islandIds = models.GetSleepIslandVisualIds();
        const int islandId = modelIndex < static_cast<int>( islandIds.size() ) && islandIds[modelIndex] != 0
                                 ? islandIds[modelIndex]
                                 : modelIndex + 1;
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


void CollisionVisualizer::DrawInstances( IRenderCommandContext& renderCommands,
                                         uint32_t mesh,
                                         int vertexCount,
                                         const std::vector<float>& instanceData )
{
    // Skip empty shape batches. This keeps mixed scenes cheap when one primitive
    // type is absent, and it avoids uploading an empty instance buffer.
    const int instanceCount = static_cast<int>( instanceData.size() ) / INSTANCE_FLOATS;
    if ( mesh == 0 || vertexCount <= 0 || instanceCount <= 0 )
    {
        return;
    }

    renderCommands.UploadInstanceData( mesh, instanceData.data(), static_cast<int>( instanceData.size() ) );
    renderCommands.DrawInstancedMesh( mesh, vertexCount, instanceCount );
}


void CollisionVisualizer::DrawHullInstance( IRenderCommandContext& renderCommands,
                                            const ConvexHullShape& hull,
                                            const Matrix4& model,
                                            const Color& color )
{
    if ( m_hullDynamicVB == 0 )
    {
        return;
    }

    float instanceData[INSTANCE_FLOATS] = {};
    const float* md = model.Data();
    std::copy( md, md + 16, instanceData );
    instanceData[16] = color.r;
    instanceData[17] = color.g;
    instanceData[18] = color.b;
    instanceData[19] = color.a;

    int vertexCount = 0;
    auto emitVertex = [&]( uint16_t index, const Vector3& normal )
    {
        if ( vertexCount >= HULL_MAX_TRIANGLE_VERTICES )
        {
            return;
        }

        const Vector3 p = hull.GetPosition() + hull.GetVertex( index );
        float* out = &sHullDebugVertexData[static_cast<size_t>( vertexCount ) * HULL_DYNAMIC_FLOATS_PER_VERTEX];
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
        out[3] = normal.x;
        out[4] = normal.y;
        out[5] = normal.z;
        std::copy( instanceData, instanceData + INSTANCE_FLOATS, out + 6 );
        ++vertexCount;
    };

    for ( uint16_t f = 0; f < hull.GetFaceCount(); ++f )
    {
        const ConvexHullFace& face = hull.GetFace( f );
        if ( face.indexCount < 3 )
        {
            continue;
        }

        const uint16_t root = hull.GetFaceIndex( face.firstIndex );
        for ( uint8_t i = 1; i + 1 < face.indexCount; ++i )
        {
            const uint16_t b = hull.GetFaceIndex( face.firstIndex + i );
            const uint16_t c = hull.GetFaceIndex( face.firstIndex + i + 1 );
            emitVertex( root, face.normalLocal );
            emitVertex( b, face.normalLocal );
            emitVertex( c, face.normalLocal );
        }
    }

    if ( vertexCount > 0 )
    {
        renderCommands.UploadAndDrawDynamicVB( m_hullDynamicVB, sHullDebugVertexData.data(), vertexCount );
    }
}


void CollisionVisualizer::Render( IRenderCommandContext& renderCommands,
                                  GameModelCollection& models,
                                  const Matrix4& view,
                                  const Matrix4& proj,
                                  const float lightPos[4] )
{
    if ( !m_enabled || models.GetModelCount() <= 0 )
    {
        return;
    }

    BuildSleepGroupSizes( models );

    m_sphereInstanceData.clear();
    m_boxInstanceData.clear();

    // Build primitive streams from the authoritative collision shape. Hulls are
    // drawn after shader constants are bound because each hull emits transient
    // triangle data instead of reusing a cached static mesh.
    const int modelCount = models.GetModelCount();
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models.GetModelAtIndex( i );
        Color color = ComputeModelColor( i, models );
        if ( m_alphaOverride >= 0.0f )
        {
            color.a = m_alphaOverride;
        }
        if ( model.IsBox() )
        {
            AppendInstance( m_boxInstanceData, model.GetModelMatrix(), color );
        }
        else if ( !model.IsConvexHull() )
        {
            AppendInstance( m_sphereInstanceData, model.GetModelMatrix(), color );
        }
    }

    // The fragment shader expects light position in view space. Keep the same
    // transform used by the normal lit path so collision solids respond to camera
    // movement and directional/positional light modes consistently.
    float viewLightPos[4];
    for ( int i = 0; i < 3; ++i )
    {
        viewLightPos[i] = view.m[i] * lightPos[0] + view.m[i + 4] * lightPos[1] + view.m[i + 8] * lightPos[2] +
                          view.m[i + 12] * lightPos[3];
    }
    viewLightPos[3] = lightPos[3];

    m_shader->Use();
    m_shader->SetMat4( "uView", view );
    m_shader->SetMat4( "uProjection", proj );
    m_shader->SetVec4( "uClipPlane", m_clipPlane[0], m_clipPlane[1], m_clipPlane[2], m_clipPlane[3] );
    m_shader->SetVec4( "uLightPosition", viewLightPos[0], viewLightPos[1], viewLightPos[2], viewLightPos[3] );

    const bool translucent = m_alphaOverride >= 0.0f && m_alphaOverride < 1.0f;
    renderCommands.SetBlend( translucent );
    if ( translucent )
    {
        renderCommands.SetBlendFunc( BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha );
        renderCommands.SetDepthWrite( false );
    }
    // Draw-call attribution is owned by the render pass that called us. Opening
    // trace scopes here would reacquire the global renderer service.
    DrawInstances( renderCommands, m_sphereInstMesh, m_sphereVertexCount, m_sphereInstanceData );
    DrawInstances( renderCommands, m_boxInstMesh, m_boxVertexCount, m_boxInstanceData );
    for ( int i = 0; i < modelCount; ++i )
    {
        GameModel& model = models.GetModelAtIndex( i );
        if ( !model.IsConvexHull() )
        {
            continue;
        }

        const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &model.GetCollisionShape() );
        if ( !hull )
        {
            continue;
        }

        Color color = ComputeModelColor( i, models );
        if ( m_alphaOverride >= 0.0f )
        {
            color.a = m_alphaOverride;
        }
        DrawHullInstance(
            renderCommands,
            *hull,
            Matrix4::Translate( model.GetPosition() ) * Matrix4::FromQuaternion( model.GetOrientation() ),
            color );
    }
    if ( translucent )
    {
        renderCommands.SetDepthWrite( true );
    }
    renderCommands.SetBlend( false );
}
